/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInputCoordinator.h"
#include "InputEvidence.h"
#include "LifecycleDiagnostics.h"
#include "OriginalInputBatchBudget.h"
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
KeyboardRouteBinding forwarding(const LocalInputLease &lease)
{
  return {{lease.session, lease.epoch}, lease.keyboardRouteGeneration, lease.focusRequest,
          lease.focusRevision,          lease.geometryRevision,        lease.scope};
}
bool preparedTargetMatches(
    const PreparedInputFocus &prepared, const LocalInputLease &lease, const LocalInputTarget &target
)
{
  const auto &value = prepared.snapshot();
  return prepared.live() && value.owner == Owner{lease.session, lease.epoch} &&
         value.geometry == lease.geometryRevision && value.focusRequest == lease.focusRequest &&
         value.focusRevision == lease.focusRevision && value.viewerConnection == target.viewerConnection &&
         value.viewerPeerFingerprint == target.viewerPeerFingerprint;
}
} // namespace
struct OriginalInputCoordinator::Shared
{
  Shared()
  {
    inputEvidenceInitialize(InputEvidenceQueue::CoordinatorOperations, this, capacity);
  }
  enum class Phase
  {
    Requested,
    FocusPreparing,
    Preparing,
    Mapped,
    Ready,
    Queued,
    Sent,
    Revoked
  };
  struct Owner
  {
    std::uint64_t id = 0;
    SelectedPeer selected;
    OriginalInputEpoch capture;
    std::optional<LocalInputOffer> offer;
    std::optional<LocalInputLease> lease;
    std::optional<LocalInputTarget> target;
    std::optional<InputCutoffSeal> seal;
    std::optional<PreparedInputFocus> prepared;
    bool prepareFocus = false, hostQuiesced = false, emptyDecline = false;
    bool active = false, revoked = false, hostReleased = true, coreReleased = true, scrolling = false;
    std::atomic<std::uint64_t> diagnosticRepeatTicket{0};
    OriginalInputBatchBudget deliveryBudget;
  };
  struct Operation
  {
    Operation(
        std::uint64_t id, OriginalInputCaptured input, OriginalInputRouteDecision decision, Clock::time_point expires,
        std::shared_ptr<Owner> inputOwner, Phase state
    )
        : ticket(id),
          captured(std::move(input)),
          route(std::move(decision)),
          deadline(expires),
          owner(std::move(inputOwner)),
          phase(state),
          diagnosticDeadline(expires),
          initialPreparation(!owner->active)
    {
    }
    void trace(unsigned stage, const char *step, const char *state, bool failure = false)
    {
      const auto *key = std::get_if<OriginalInputKey>(&captured.input.record.payload);
      const bool repeat = key && key->transition == OriginalKeyTransition::Repeat;
      if (!initialPreparation && !failure) {
        if (!repeat)
          return;
        std::uint64_t unused = 0;
        owner->diagnosticRepeatTicket.compare_exchange_strong(unused, ticket);
        if (owner->diagnosticRepeatTicket.load() != ticket)
          return;
      }
      if (diagnosticStages.fetch_or(1u << stage) & (1u << stage))
        return;
      const auto now = Clock::now();
      lifecycleDiagnostic(
          "Core input", step, state, "Original input handoff progress",
          {{"ticket", QString::number(ticket)},
           {"owner", QString::number(owner->id)},
           {"captureSequence", QString::number(captured.input.record.sequence)},
           {"repeat", repeat},
           {"initialPreparation", initialPreparation},
           {"elapsedMs", qint64(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count())},
           {"initialBudgetRemainingMs",
            qint64(std::chrono::duration_cast<std::chrono::milliseconds>(diagnosticDeadline - now).count())}}
      );
    }
    std::uint64_t ticket = 0;
    OriginalInputCaptured captured;
    OriginalInputRouteDecision route;
    Clock::time_point deadline;
    std::shared_ptr<Owner> owner;
    std::optional<InputCutoffObservation> observation;
    Phase phase = Phase::Requested;
    const Clock::time_point started = Clock::now(), diagnosticDeadline;
    const bool initialPreparation;
    std::atomic<unsigned> diagnosticStages{0};
  };
  mutable std::mutex mutex;
  bool closed = false, available = false, dispatcherCreated = false;
  // Called only under mutex, which stop() also holds before retiring the
  // QObject context. At most one queued wake crosses to the private thread.
  std::function<void()> wake;
  bool wakeQueued = false;
  bool mappedOutputDrained = true, mappedDrainRequested = false;
  std::shared_ptr<Owner> mappedOwner;
  Clock::time_point mappedStarted{};
  std::size_t mappedRecords = 0;
  std::uint64_t nextTicket = 0, nextOwner = 0;
  LocalInputPeerPolicy policy;
  std::shared_ptr<Owner> owner;
  std::deque<std::shared_ptr<Operation>> operations;
  // At most one complete mapped batch may await private output, plus the
  // Server's single waiting preparation. The latter cannot replace receipts
  // until the preceding batch has been sent.
  static constexpr std::size_t capacity = inputCutoffMaximumBatch + 1;
  static bool live(const Owner &owner)
  {
    return !owner.revoked && owner.selected.live() && owner.capture.live() &&
           (owner.seal ? owner.seal->live() : !owner.prepared || owner.prepared->live());
  }
  void revoke(const char *reason = "Input authority was revoked")
  {
    if (owner && !owner->revoked)
      lifecycleDiagnostic(
          "Core input", "input.owner", "revoked", reason,
          {{"owner", QString::number(owner->id)},
           {"active", owner->active},
           {"selectedLive", owner->selected.live()},
           {"captureLive", owner->capture.live()},
           {"sealLive", owner->seal && owner->seal->live()},
           {"preparedLive", owner->prepared && owner->prepared->live()}}
      );
    inputEvidenceInvalidate(InputEvidenceLoss::PipelineRevoked);
    if (owner)
      owner->revoked = true;
    for (const auto &operation : operations) {
      operation->owner->revoked = true;
      operation->phase = Phase::Revoked;
    }
  }
};

class OriginalInputCoordinator::Dispatcher final : public OriginalInputDispatcher
{
public:
  Dispatcher(std::shared_ptr<Shared> state, Backend backend) : m_state(std::move(state)), m_backend(std::move(backend))
  {
    inputEvidenceInitialize(InputEvidenceQueue::CoordinatorBatch, this, inputCutoffMaximumBatch);
    inputEvidenceInitialize(InputEvidenceQueue::CoordinatorPreview, this, inputCutoffMaximumBatch);
  }
  ~Dispatcher() override
  {
    abort();
  }
  Result reserve(
      const OriginalInputCaptured &captured, const OriginalInputRouteDecision &route, const SelectedPeer &selected,
      std::uint64_t existingOwner, Clock::time_point deadline
  ) override
  {
    std::unique_lock lock(m_state->mutex);
    if (m_state->closed || !m_state->available || !m_state->policy.windowSharePeer(selected.ticket().fingerprint())) {
      diagnose(0, "Code admission or peer policy is unavailable", captured, route);
      return existingOwner || m_state->owner || m_cutoff ? Result::revoked() : Result::refused();
    }
    while (!m_state->operations.empty() && m_state->operations.front()->phase == Shared::Phase::Sent)
      inputEvidenceApply(InputEvidenceQueue::CoordinatorOperations, m_state.get(), 0, 1, [&] {
        m_state->operations.pop_front();
      });
    if (m_preparing || m_reserved || !captured.input.live() || !selected.live() || !route.pointKnown ||
        route.stamp != OriginalInputStamp{captured.input.record.epoch, captured.input.record.sequence, 1} ||
        captured.translation.parts != 1 || captured.translation.uncertain || deadline <= Clock::now() ||
        route.recipient != selected.recipient() || route.server != selected.serverIncarnation() ||
        route.ticket.incarnation() != selected.ticket().incarnation() ||
        route.ticket.fingerprint() != selected.ticket().fingerprint() ||
        m_state->operations.size() >= Shared::capacity || m_state->nextTicket == UINT64_MAX) {
      diagnose(1, "Capture, selection, route or reservation boundary is invalid", captured, route);
      return Result::revoked();
    }
    auto owner = m_state->owner;
    const auto *key = std::get_if<OriginalInputKey>(&captured.input.record.payload);
    const auto *button = std::get_if<OriginalInputButton>(&captured.input.record.payload);
    const bool acquisition =
        (key && key->transition == OriginalKeyTransition::Down && !key->repeat) || (button && button->down);
    if ((!owner || (!existingOwner && acquisition)) && !captured.transition.permitsAcquisition(captured.input)) {
      diagnose(2, "The control has no observed release followed by a fresh press", captured, route);
      if (owner) {
        m_state->revoke("Captured acquisition has no fresh press transition");
        return Result::revoked();
      }
      return Result::refused();
    }
    if (owner && (!Shared::live(*owner) || !owner->selected.sameSelection(selected) ||
                  !owner->capture.sameEpoch(captured.input.epoch))) {
      m_state->revoke("Capture epoch or selected recipient changed");
      return Result::revoked();
    }
    if (!owner) {
      if (existingOwner || m_state->nextOwner == UINT64_MAX)
        return Result::revoked();
      owner = std::make_shared<Shared::Owner>();
      owner->id = ++m_state->nextOwner;
      owner->selected = selected;
      owner->capture = captured.input.epoch;
      // Only a fresh button acquisition may activate an unfocused Viewer.
      // Keyboard, hover and scroll attempts cannot change native focus.
      owner->prepareFocus = button && button->down && m_backend.preparationCapability && m_backend.prepare &&
                            m_backend.prepared && m_backend.preparationState && m_backend.preparationOutcome &&
                            m_backend.promote && m_backend.cancelPreparation;
      m_state->owner = owner;
    }
    if (existingOwner && existingOwner != owner->id)
      return Result::revoked();
    diagnose(3, "Original input preparation was requested", captured, route);
    m_preparing = std::make_shared<Shared::Operation>(
        ++m_state->nextTicket, captured, route, deadline, owner,
        owner->prepareFocus && !owner->active ? Shared::Phase::FocusPreparing : Shared::Phase::Requested
    );
    m_preparing->trace(0, "input.handoff", "started");
    inputEvidenceApply(InputEvidenceQueue::CoordinatorOperations, m_state.get(), 1, 0, [&] {
      m_state->operations.push_back(m_preparing);
    });
    const auto ticket = m_preparing->ticket;
    const bool prepared = owner->active && !m_batch.empty() && m_batch.front().capture == route.stamp;
    lock.unlock();
    // Consume independent batch point receipts in source order without a
    // timer or network round trip for each key, scroll or drag record.
    if (!prepared)
      return Result::pending(ticket);
    auto result = poll(ticket);
    // Only Pending publishes a preparation ticket from reserve(). A terminal
    // inline result has no preceding Server preparation; the reservation's
    // nonce still retains this operation's exact identity for commit/cancel.
    if (result.state != State::Pending)
      result.ticket = 0;
    return result;
  }
  Result poll(std::uint64_t ticket) override
  {
    std::shared_ptr<Shared::Operation> operation;
    std::optional<LocalInputLease> lease;
    std::optional<LocalInputTarget> target;
    bool active = false;
    bool focusPreparing = false;
    {
      std::lock_guard lock(m_state->mutex);
      operation = m_preparing;
      if (!operation || operation->ticket != ticket)
        return Result::revoked(ticket);
      if (m_state->closed || !Shared::live(*operation->owner) || !operation->captured.input.live() ||
          Clock::now() >= operation->deadline || operation->phase == Shared::Phase::Revoked) {
        operation->trace(9, "input.handoff", Clock::now() >= operation->deadline ? "expired" : "revoked", true);
        // A Host that declined before reserving any Viewer/native lease can
        // explicitly acknowledge that empty handoff. Preserve ordinary input
        // after that exact completion, rather than dropping every fresh key
        // merely because this paired desktop currently has no exported window.
        const auto &owner = *operation->owner;
        const bool declined = !m_state->closed && (owner.emptyDecline || (owner.offer && !owner.lease)) &&
                              !owner.active && owner.hostReleased && owner.coreReleased && owner.selected.live() &&
                              operation->captured.input.live() && Clock::now() < operation->deadline;
        operation->owner->revoked = true;
        operation->phase = Shared::Phase::Revoked;
        m_preparing.reset();
        return declined ? Result::refused(ticket) : Result::revoked(ticket);
      }
      // A previous packet must be accepted by the private transport before its
      // point observation is replaced. The old prefix stays sealed throughout.
      if (!orderedBefore(operation))
        return Result::pending(ticket);
      lease = operation->owner->lease;
      target = operation->owner->target;
      active = operation->owner->active;
      focusPreparing = operation->phase == Shared::Phase::FocusPreparing;
      if (!focusPreparing && (!lease || !target || !operation->owner->hostQuiesced))
        return Result::pending(ticket);
      if (operation->phase == Shared::Phase::Ready) {
        if (!operation->owner->active || !operation->observation || !operation->observation->live()) {
          operation->owner->revoked = true;
          operation->phase = Shared::Phase::Revoked;
          m_preparing.reset();
          return Result::revoked(ticket);
        }
        m_reserved = std::exchange(m_preparing, {});
        operation->trace(10, "input.reservation", "accepted");
        return Result::accepted({operation->owner->id, ticket}, ticket);
      }
    }
    if (focusPreparing)
      return prepareFocus(operation);
    const auto selected = operation->owner->selected;
    if (m_cutoff && m_cutoffOwner != operation->owner) {
      const bool resumed = retireCutoff();
      if (!current(operation))
        return Result::revoked(ticket);
      if (!resumed)
        return Result::pending(ticket);
    }
    if (!m_cutoff) {
      const bool quiesced = m_preparation ? m_preparationOwner == operation->owner
                                          : m_backend.quiesce(selected, operation->route, !active);
      if (!current(operation) || !quiesced) {
        cancelPreparation(ticket);
        return Result::revoked(ticket);
      }
      const auto deadline = std::min(operation->deadline, Clock::now() + inputCutoffMaximumLifetime);
      const bool preparationOwned = m_preparation.has_value();
      {
        std::lock_guard lock(m_state->mutex);
        // Reserve cleanup ownership before the write can reenter Qt and
        // retire the Host offer. A returned empty value means no wire attempt.
        if (!currentLocked(operation))
          return Result::revoked(ticket);
        operation->owner->coreReleased = false;
      }
      const auto cutoff = m_preparation && operation->owner->prepared
                              ? m_backend.promote(selected, *operation->owner->prepared, forwarding(*lease), deadline)
                              : m_backend.start(
                                    selected, operation->route.stamp, forwarding(*lease), target->viewerConnection,
                                    {operation->route.x, operation->route.y}, deadline
                                );
      operation->trace(5, "input.cutoff", cutoff ? "started" : "failed");
      // Even a result returned after cancellation retains exact cleanup
      // ownership. Never discard the new binding just because reentry revoked
      // its preparation during the backend write.
      if (cutoff) {
        m_cutoff = cutoff;
        m_cutoffTicket = ticket;
        m_cutoffOwner = operation->owner;
        std::lock_guard lock(m_state->mutex);
        operation->owner->coreReleased = false;
      } else if (!preparationOwned) {
        std::lock_guard lock(m_state->mutex);
        operation->owner->coreReleased = true;
      }
      if (!current(operation) || !cutoff) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
    } else if (m_cutoffTicket != ticket) {
      if (!m_seal || !m_seal->live()) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
      if (m_batch.empty()) {
        std::size_t deliveryLimit = 4;
        // A new batch revokes the preceding point receipts. Wait for their
        // ordered peer dispatch, not merely a successful local socket write.
        // Otherwise fresh 250 ms receipts can sit behind native AX work in
        // the peer's socket queue and expire before their own delivery check.
        {
          std::lock_guard lock(m_state->mutex);
          for (const auto &prior : m_state->operations) {
            if (prior == operation)
              break;
            if (prior->phase != Shared::Phase::Sent)
              return Result::pending(ticket);
          }
          if (!m_state->mappedOutputDrained) {
            m_state->mappedDrainRequested = true;
            if (m_state->wake)
              m_state->wake();
            return Result::pending(ticket);
          }
          deliveryLimit = operation->owner->deliveryBudget.limit();
        }
        const auto maximumDeadline = Clock::now() + inputCutoffMaximumLifetime;
        std::vector<InputCutoffMapItem> items{
            {operation->route.stamp,
             {operation->route.x, operation->route.y},
             std::min(operation->deadline, maximumDeadline),
             operation->route.kind == OriginalInputRouteKind::Motion}
        };
        const auto first = std::find_if(m_preview.begin(), m_preview.end(), [&](const auto &item) {
          return item.route.stamp == operation->route.stamp;
        });
        if (first != m_preview.end()) {
          for (auto next = std::next(first); next != m_preview.end() && items.size() < deliveryLimit; ++next) {
            if (!next->captured.input.live() || !next->selected.sameSelection(selected) ||
                !next->captured.input.epoch.sameEpoch(operation->captured.input.epoch) ||
                next->deadline <= Clock::now())
              break;
            items.push_back(
                {next->route.stamp,
                 {next->route.x, next->route.y},
                 std::min(next->deadline, maximumDeadline),
                 next->route.kind == OriginalInputRouteKind::Motion}
            );
          }
        }
        const auto bindings = m_backend.batch(selected, *m_seal, items);
        if (!current(operation) || !bindings || bindings->size() != items.size()) {
          cancelPreparation(ticket);
          maintain();
          return Result::revoked(ticket);
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
          const auto &binding = (*bindings)[i];
          if (!continuesInputCutoffSeal(m_seal->binding(), binding) || binding.capture != items[i].capture) {
            cancelPreparation(ticket);
            maintain();
            return Result::revoked(ticket);
          }
          inputEvidenceApply(InputEvidenceQueue::CoordinatorBatch, this, 1, 0, [&] {
            m_batch.push_back({binding, items[i].capture, items[i].point, items[i].deadline});
          });
        }
      }
      const auto cutoff = !m_batch.empty() && m_batch.front().capture == operation->route.stamp &&
                                  m_batch.front().point == InputCutoffPoint{operation->route.x, operation->route.y}
                              ? std::optional(m_batch.front().binding)
                              : std::nullopt;
      // The origin seal remains an exact cleanup address even if a reentrant
      // cancellation occurs during the fresh map write.
      if (cutoff) {
        m_cutoff = cutoff;
        m_cutoffTicket = ticket;
      }
      if (!current(operation) || !cutoff) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
    }
    const auto cutoff = *m_cutoff;
    const auto observed = m_backend.observe(selected, cutoff);
    if (!current(operation) || m_cutoff != cutoff) {
      cancelPreparation(ticket);
      maintain();
      return Result::revoked(ticket);
    }
    if (!observed) {
      const auto state = m_backend.state(selected, cutoff);
      if (!current(operation) || state == InputCutoffState::Denied || state == InputCutoffState::Resuming ||
          state == InputCutoffState::Resumed) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
      return Result::pending(ticket);
    }
    const auto &snapshot = observed->snapshot();
    if (!observed->live() || !observed->seal() || !observed->seal()->live() ||
        (m_seal && observed->seal()->binding() != m_seal->binding()) || snapshot.binding != cutoff ||
        snapshot.binding.capture != operation->route.stamp || snapshot.binding.forwarding != forwarding(*lease) ||
        snapshot.binding.viewerConnection != target->viewerConnection ||
        snapshot.viewerPeerFingerprint != target->viewerPeerFingerprint ||
        snapshot.point != InputCutoffPoint{operation->route.x, operation->route.y} ||
        observed->deadline() > operation->deadline) {
      cancelPreparation(ticket);
      return Result::revoked(ticket);
    }
    {
      std::lock_guard lock(m_state->mutex);
      if (!currentLocked(operation))
        return Result::revoked(ticket);
      m_seal = observed->seal();
      operation->owner->seal = m_seal;
      operation->trace(6, "input.cutoff", "mapped");
      // The validated mapped receipt consumes the preparation gate. From
      // this point only the real origin seal owns backend cleanup.
      operation->owner->prepared.reset();
      m_preparation.reset();
      m_preparationOwner.reset();
      m_preparationCanceling = false;
      operation->observation = observed;
      operation->phase = operation->owner->active ? Shared::Phase::Ready : Shared::Phase::Mapped;
      if (!m_batch.empty() && m_batch.front().binding == cutoff)
        inputEvidenceApply(InputEvidenceQueue::CoordinatorBatch, this, 0, 1, [&] { m_batch.pop_front(); });
      if (operation->owner->active) {
        m_reserved = std::exchange(m_preparing, {});
        return Result::accepted({operation->owner->id, ticket}, ticket);
      }
    }
    return Result::pending(ticket);
  }
  void cancelPreparation(std::uint64_t ticket) noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    if (m_preparing && m_preparing->ticket == ticket) {
      m_preparing->owner->revoked = true;
      m_preparing->phase = Shared::Phase::Revoked;
      m_preparing.reset();
      m_negotiationDeadline.reset();
    }
    if (m_reserved && m_reserved->ticket == ticket) {
      m_reserved->owner->revoked = true;
      m_reserved->phase = Shared::Phase::Revoked;
      m_reserved.reset();
    }
  }
  bool commit(const Reservation &reservation) override
  {
    std::lock_guard lock(m_state->mutex);
    if (!matches(reservation) || m_state->closed || !Shared::live(*m_reserved->owner) || !m_reserved->owner->active ||
        !m_reserved->captured.input.live() || !m_reserved->observation || !m_reserved->observation->live() ||
        Clock::now() >= m_reserved->deadline)
      return false;
    m_reserved->phase = Shared::Phase::Queued;
    m_reserved->trace(11, "input.delivery", "queued");
    if (const auto *scroll = std::get_if<OriginalInputScroll>(&m_reserved->captured.input.record.payload);
        scroll && scroll->continuous)
      m_reserved->owner->scrolling =
          scroll->phase != OriginalScrollPhase::Cancelled && scroll->momentum != OriginalMomentumPhase::Ended;
    m_reserved.reset();
    if (m_state->wake)
      m_state->wake();
    return true;
  }
  void cancel(const Reservation &reservation) noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    if (matches(reservation)) {
      m_reserved->owner->revoked = true;
      m_reserved->phase = Shared::Phase::Revoked;
      m_reserved.reset();
    }
  }
  void release(std::uint64_t owner) noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    if (m_state->owner && m_state->owner->id == owner)
      m_state->revoke("Core dispatcher released the input owner");
  }
  bool live(std::uint64_t owner) const noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    return !m_state->closed && m_state->owner && m_state->owner->id == owner && m_state->owner->active &&
           Shared::live(*m_state->owner) &&
           (!m_reserved || (m_reserved->observation && m_reserved->observation->live()));
  }
  bool presentsCursor() const noexcept override
  {
    return true;
  }
  bool legacyAcquisitionReady(std::uint64_t recipient) const noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    // This includes an inactive/revoked owner whose exact Host or Core
    // release is still outstanding. No unrelated legacy write may reopen
    // one side of that two-party handoff.
    return !m_state->owner || m_state->owner->selected.recipient() != recipient;
  }
  bool canCoalesceHoverMotion() const noexcept override
  {
    std::lock_guard lock(m_state->mutex);
    return !m_state->closed && m_state->owner && m_state->owner->active && Shared::live(*m_state->owner) &&
           !m_state->owner->scrolling && !m_preparing && !m_reserved && m_batch.empty();
  }
  void preview(std::span<const Preview> values) override
  {
    inputEvidenceApply(InputEvidenceQueue::CoordinatorPreview, this, 0, m_preview.size(), [&] { m_preview.clear(); });
    std::lock_guard lock(m_state->mutex);
    if (m_state->closed || !m_state->owner || !m_state->owner->active || !Shared::live(*m_state->owner))
      return;
    for (const auto &value : values) {
      if (m_preview.size() == inputCutoffMaximumBatch || !value.captured.input.live() ||
          !m_state->owner->selected.sameSelection(value.selected) ||
          !m_state->owner->capture.sameEpoch(value.captured.input.epoch) || !value.route.pointKnown ||
          value.route.stamp !=
              OriginalInputStamp{value.captured.input.record.epoch, value.captured.input.record.sequence, 1} ||
          value.captured.translation.parts != 1 || value.captured.translation.uncertain ||
          (!m_preview.empty() && value.route.stamp.sequence <= m_preview.back().route.stamp.sequence))
        break;
      inputEvidenceApply(InputEvidenceQueue::CoordinatorPreview, this, 1, 0, [&] { m_preview.push_back(value); });
    }
  }
  void maintain() noexcept override
  {
    if ((!m_cutoff && !m_preparation) || m_maintaining || m_inspecting)
      return;
    const auto owner = m_preparation ? m_preparationOwner : m_cutoffOwner;
    bool cleanup = false;
    {
      std::lock_guard lock(m_state->mutex);
      cleanup = m_state->closed || !Shared::live(*owner);
    }
    if (!cleanup) {
      m_inspecting = true;
      try {
        if (m_preparation) {
          const auto state = m_backend.preparationState(owner->selected, *m_preparation);
          cleanup = state == InputPreparationState::Failed || state == InputPreparationState::Released;
          if (state == InputPreparationState::Released) {
            const auto outcome = m_backend.preparationOutcome(owner->selected, *m_preparation);
            std::lock_guard lock(m_state->mutex);
            owner->emptyDecline =
                !owner->offer && !m_cutoff &&
                (outcome == InputPreparationOutcome::NoTarget || outcome == InputPreparationOutcome::Unsupported);
          }
        } else {
          const auto state = m_backend.state(owner->selected, m_seal ? m_seal->binding() : *m_cutoff);
          cleanup = state == InputCutoffState::Denied || state == InputCutoffState::Resuming ||
                    state == InputCutoffState::Resumed;
        }
      } catch (...) {
        cleanup = true;
      }
      m_inspecting = false;
      {
        std::lock_guard lock(m_state->mutex);
        cleanup = cleanup || m_state->closed || !Shared::live(*owner);
      }
      if (cleanup) {
        std::lock_guard lock(m_state->mutex);
        owner->revoked = true;
      }
    }
    if (cleanup) {
      try {
        (void)retireCutoff();
      } catch (...) {
        std::lock_guard lock(m_state->mutex);
        owner->revoked = true;
      }
    }
  }
  void abort() noexcept override
  {
    {
      std::lock_guard lock(m_state->mutex);
      m_state->closed = true;
      m_state->revoke();
      m_preparing.reset();
      m_reserved.reset();
    }
    maintain();
  }

private:
  // First occurrence of each preparation boundary, on the Core thread only.
  // Keep diagnostics bounded and outside native capture; no event text/data.
  void diagnose(
      unsigned stage, const char *message, const OriginalInputCaptured &captured,
      const OriginalInputRouteDecision &route
  )
  {
    const auto bit = 1u << stage;
    if (!inputEvidence() || (m_diagnosticStages & bit))
      return;
    m_diagnosticStages |= bit;
    lifecycleDiagnostic(
        "Core input", "input.preparation", "observed", message,
        {{"stage", int(stage)},
         {"available", m_state->available},
         {"closed", m_state->closed},
         {"captureLive", captured.input.live()},
         {"epoch", QString::number(captured.input.record.epoch)},
         {"sequence", QString::number(captured.input.record.sequence)},
         {"parts", int(captured.translation.parts)},
         {"uncertain", captured.translation.uncertain},
         {"pointKnown", route.pointKnown},
         {"routeKind", int(route.kind)},
         {"acquisitionObserved", captured.transition.permitsAcquisition(captured.input)}}
    );
  }
  unsigned m_diagnosticStages = 0;
  Result prepareFocus(const std::shared_ptr<Shared::Operation> &operation)
  {
    const auto ticket = operation->ticket;
    const auto owner = operation->owner;
    if (!m_preparation) {
      const auto generation = m_backend.preparationCapability(owner->selected);
      if (!current(operation))
        return Result::revoked(ticket);
      if (!generation) {
        // Negotiation creates no gate and changes no owner. A bounded wait
        // permits an older peer to continue ordinary input without confusing
        // silence with a native release acknowledgement.
        if (!m_negotiationDeadline)
          m_negotiationDeadline = std::min(operation->deadline, Clock::now() + std::chrono::milliseconds(100));
        if (Clock::now() < *m_negotiationDeadline)
          return Result::pending(ticket);
        std::lock_guard lock(m_state->mutex);
        owner->emptyDecline = owner->revoked = true;
        operation->phase = Shared::Phase::Revoked;
        m_preparing.reset();
        m_negotiationDeadline.reset();
        return Result::refused(ticket);
      }
      m_negotiationDeadline.reset();
      // Existing authorized legacy releases precede the preparation request
      // and its EI prefix. The source click itself is still withheld.
      const bool quiesced = m_backend.quiesce(owner->selected, operation->route, true);
      if (!current(operation) || !quiesced) {
        cancelPreparation(ticket);
        return Result::revoked(ticket);
      }
      {
        std::lock_guard lock(m_state->mutex);
        if (!currentLocked(operation))
          return Result::revoked(ticket);
        owner->coreReleased = false;
      }
      const auto binding = m_backend.prepare(
          owner->selected, operation->route.stamp, {operation->route.x, operation->route.y}, operation->deadline
      );
      operation->trace(1, "input.focus-preparation", binding ? "started" : "failed");
      if (binding) {
        m_preparation = binding;
        m_preparationOwner = owner;
        std::lock_guard lock(m_state->mutex);
        owner->coreReleased = false;
      } else {
        std::lock_guard lock(m_state->mutex);
        owner->coreReleased = true;
      }
      // A returned binding remains the exact cleanup address even if the
      // synchronous write reentered cancellation or selection replacement.
      if (!current(operation) || !binding || binding->coreConnectionGeneration != *generation ||
          binding->capture != operation->route.stamp) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
    }
    if (m_preparationOwner != owner) {
      cancelPreparation(ticket);
      maintain();
      return Result::revoked(ticket);
    }
    const auto binding = *m_preparation;
    const auto prepared = m_backend.prepared(owner->selected, binding);
    if (!current(operation) || m_preparation != binding) {
      cancelPreparation(ticket);
      maintain();
      return Result::revoked(ticket);
    }
    if (prepared) {
      if (!prepared->live() || prepared->binding() != binding || prepared->deadline() > operation->deadline ||
          prepared->snapshot().point != InputCutoffPoint{operation->route.x, operation->route.y}) {
        cancelPreparation(ticket);
        maintain();
        return Result::revoked(ticket);
      }
      std::lock_guard lock(m_state->mutex);
      if (!currentLocked(operation))
        return Result::revoked(ticket);
      owner->prepared = prepared;
      operation->phase = Shared::Phase::Requested;
      operation->trace(2, "input.focus-preparation", "completed");
      return Result::pending(ticket);
    }
    const auto state = m_backend.preparationState(owner->selected, binding);
    if (!current(operation) || state == InputPreparationState::Failed) {
      cancelPreparation(ticket);
      maintain();
      return Result::revoked(ticket);
    }
    if (state == InputPreparationState::Released) {
      const auto outcome = m_backend.preparationOutcome(owner->selected, binding);
      const bool declined =
          outcome == InputPreparationOutcome::NoTarget || outcome == InputPreparationOutcome::Unsupported;
      if (!retirePreparation())
        return Result::revoked(ticket);
      std::lock_guard lock(m_state->mutex);
      owner->emptyDecline = declined;
      owner->revoked = true;
      operation->phase = Shared::Phase::Revoked;
      m_preparing.reset();
      return declined ? Result::refused(ticket) : Result::revoked(ticket);
    }
    return Result::pending(ticket);
  }
  bool retirePreparation()
  {
    if (!m_preparation)
      return true;
    if (m_maintaining)
      return false;
    m_maintaining = true;
    struct Reset
    {
      bool &value;
      ~Reset()
      {
        value = false;
      }
    } reset{m_maintaining};
    const auto binding = *m_preparation;
    const auto owner = m_preparationOwner;
    auto state = m_backend.preparationState(owner->selected, binding);
    if (state != InputPreparationState::Released) {
      if (!m_preparationCanceling) {
        m_preparationCanceling = true;
        // This owns both the preparation and any linked pending promotion.
        // DICX is only valid after a validated Sealed receipt transfers it.
        if (!m_backend.cancelPreparation(owner->selected, binding, Clock::now() + inputCutoffMaximumLifetime))
          return false;
      }
      state = m_backend.preparationState(owner->selected, binding);
    }
    // A dead TLS peer, failed provider or endpoint EOF is not a native drain.
    // Keep the recipient barred unless the exact release is acknowledged.
    if (state != InputPreparationState::Released || m_preparation != binding)
      return false;
    {
      std::lock_guard lock(m_state->mutex);
      owner->coreReleased = true;
    }
    m_preparation.reset();
    m_preparationOwner.reset();
    m_preparationCanceling = false;
    m_cutoff.reset();
    m_cutoffOwner.reset();
    m_cutoffTicket = 0;
    return true;
  }
  bool orderedBefore(const std::shared_ptr<Shared::Operation> &operation) const
  {
    for (const auto &prior : m_state->operations) {
      if (prior == operation)
        return true;
      if (prior->phase != Shared::Phase::Queued && prior->phase != Shared::Phase::Sent)
        return false;
    }
    return false;
  }
  bool currentLocked(const std::shared_ptr<Shared::Operation> &operation) const
  {
    return m_preparing == operation && m_state->owner == operation->owner && !m_state->closed &&
           Shared::live(*operation->owner) && operation->captured.input.live() && Clock::now() < operation->deadline &&
           operation->phase != Shared::Phase::Revoked && orderedBefore(operation);
  }
  bool current(const std::shared_ptr<Shared::Operation> &operation) const
  {
    std::lock_guard lock(m_state->mutex);
    return currentLocked(operation);
  }
  bool retireCutoff()
  {
    if (m_preparation)
      return retirePreparation();
    if (!m_cutoff)
      return true;
    if (m_maintaining)
      return false;
    m_maintaining = true;
    struct Reset
    {
      bool &value;
      ~Reset()
      {
        value = false;
      }
    } reset{m_maintaining};
    const auto binding = m_seal ? m_seal->binding() : *m_cutoff;
    const auto owner = m_cutoffOwner;
    // Selection revocation does not retarget cleanup to the next selection.
    // Transport loss revokes delivery but does not acknowledge native drain.
    bool finished = false;
    {
      if (!m_resuming) {
        m_resuming = true;
        if (!m_backend.resume(owner->selected, binding)) {
          std::lock_guard lock(m_state->mutex);
          owner->revoked = true;
          return false;
        }
      }
      const auto state = m_backend.state(owner->selected, binding);
      finished = state == InputCutoffState::Resumed;
      if (state == InputCutoffState::Denied) {
        std::lock_guard lock(m_state->mutex);
        owner->revoked = true;
      }
    }
    if (!finished || !m_cutoff || (m_seal ? m_seal->binding() : *m_cutoff) != binding)
      return false;
    {
      std::lock_guard lock(m_state->mutex);
      owner->coreReleased = true;
    }
    m_cutoff.reset();
    m_seal.reset();
    inputEvidenceApply(InputEvidenceQueue::CoordinatorBatch, this, 0, m_batch.size(), [&] { m_batch.clear(); });
    inputEvidenceApply(InputEvidenceQueue::CoordinatorPreview, this, 0, m_preview.size(), [&] { m_preview.clear(); });
    m_cutoffOwner.reset();
    m_cutoffTicket = 0;
    m_resuming = false;
    return true;
  }
  bool matches(Reservation reservation) const
  {
    return m_reserved && m_reserved->owner->id == reservation.owner && m_reserved->ticket == reservation.nonce &&
           m_reserved->phase == Shared::Phase::Ready;
  }
  std::shared_ptr<Shared> m_state;
  Backend m_backend;
  std::shared_ptr<Shared::Operation> m_preparing, m_reserved;
  std::optional<InputCutoffBinding> m_cutoff;
  std::optional<InputCutoffSeal> m_seal;
  std::optional<InputPreparationBinding> m_preparation;
  std::shared_ptr<Shared::Owner> m_preparationOwner;
  std::optional<Clock::time_point> m_negotiationDeadline;
  bool m_preparationCanceling = false;
  struct BatchPoint
  {
    InputCutoffBinding binding;
    OriginalInputStamp capture;
    InputCutoffPoint point;
    Clock::time_point deadline;
  };
  std::deque<BatchPoint> m_batch;
  std::vector<Preview> m_preview;
  std::shared_ptr<Shared::Owner> m_cutoffOwner;
  std::uint64_t m_cutoffTicket = 0;
  bool m_resuming = false, m_maintaining = false, m_inspecting = false;
};

OriginalInputCoordinator::OriginalInputCoordinator(
    LocalInputChannel &channel, std::shared_ptr<SelectedPeerMailbox> selected, LocalInputPeerPolicy policy,
    Providers providers
)
    : m_shared(std::make_shared<Shared>()),
      m_channel(&channel),
      m_providers(std::move(providers))
{
  if (channel.thread() != QThread::currentThread() || policy.empty() || channel.onInputTarget || channel.onReleased)
    throw std::invalid_argument("Original input requires an unowned private channel and explicit peer policy");
  m_shared->policy = policy;
  const QPointer<OriginalInputCoordinator> alive(this);
  m_preflight = LocalInputPreflight::core(
      channel, std::move(selected), std::move(policy),
      {[alive](const auto &) { return alive && alive->admitted(); },
       [alive](const auto &lease) { return alive && alive->deliveryReady(lease); },
       {},
       [alive](const auto &lease, auto) { return alive && alive->release(lease); },
       [alive](const auto &lease) { return alive && alive->handoffReady(lease); }},
      [alive](const LocalInputLease &lease, Clock::time_point deadline) {
        if (!alive)
          return;
        std::lock_guard lock(alive->m_shared->mutex);
        if (!alive->m_shared->owner || alive->m_shared->operations.empty())
          return;
        auto &owner = *alive->m_shared->owner;
        if (!Shared::live(owner) || owner.selected.serverIncarnation() != lease.coreServerIncarnation ||
            owner.selected.recipient() != lease.coreRecipient ||
            owner.selected.generation() != lease.coreSelectionGeneration || owner.capture.id() != lease.captureEpoch ||
            !owner.target ||
            (owner.prepareFocus &&
             (!owner.prepared || !preparedTargetMatches(*owner.prepared, lease, *owner.target)))) {
          owner.revoked = true;
          return;
        }
        owner.lease = lease;
        owner.hostQuiesced = true;
        auto &operation = *alive->m_shared->operations.front();
        operation.deadline = std::min(operation.deadline, deadline);
        operation.phase = Shared::Phase::Preparing;
        operation.trace(4, "input.host-quiescence", "completed");
      },
      [alive](const LocalInputLease &lease, const LocalInputTarget &target) {
        if (!alive)
          return;
        std::lock_guard lock(alive->m_shared->mutex);
        if (!alive->m_shared->owner || alive->m_shared->owner->target) {
          if (alive->m_shared->owner)
            alive->m_shared->owner->revoked = true;
          return;
        }
        const auto &owner = *alive->m_shared->owner;
        if (owner.prepareFocus && (!owner.prepared || !preparedTargetMatches(*owner.prepared, lease, target))) {
          alive->m_shared->owner->revoked = true;
          return;
        }
        alive->m_shared->owner->target = target;
        alive->m_shared->owner->lease = lease;
      }
  );
  channel.onReleased = [alive](const LocalInputOffer &offer) {
    if (!alive)
      return;
    std::lock_guard lock(alive->m_shared->mutex);
    const auto owner = alive->m_shared->owner;
    if (!owner || owner->offer != offer)
      return; // Retired releases cannot affect a newer owner.
    owner->revoked = true;
    owner->hostReleased = true;
  };
  m_shared->wake = [this] {
    if (m_shared->wakeQueued)
      return;
    m_shared->wakeQueued = true;
    if (!QMetaObject::invokeMethod(
            this,
            [this] {
              {
                std::lock_guard lock(m_shared->mutex);
                m_shared->wakeQueued = false;
                if (m_shared->closed)
                  return;
              }
              tick();
            },
            Qt::QueuedConnection
        ))
      m_shared->wakeQueued = false;
  };
  QObject::connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
  m_timer.start(4);
}
OriginalInputCoordinator::~OriginalInputCoordinator()
{
  stop();
}
std::shared_ptr<OriginalInputDispatcher> OriginalInputCoordinator::createDispatcher(Backend backend)
{
  return dispatcherFactory()(std::move(backend));
}
OriginalInputCoordinator::DispatcherFactory OriginalInputCoordinator::dispatcherFactory() const
{
  return [state = m_shared](Backend backend) -> std::shared_ptr<OriginalInputDispatcher> {
    std::lock_guard lock(state->mutex);
    if (state->closed || state->dispatcherCreated || !backend.quiesce || !backend.start || !backend.refresh ||
        !backend.batch || !backend.observe || !backend.state || !backend.resume)
      return {};
    state->dispatcherCreated = true;
    return std::make_shared<Dispatcher>(state, std::move(backend));
  };
}
bool OriginalInputCoordinator::admitted() const
{
  return m_channel && m_channel->supportsMappedInput() && m_providers.codeAdmission && m_providers.codeAdmission();
}
bool OriginalInputCoordinator::deliveryReady(const LocalInputLease &lease) const
{
  std::lock_guard lock(m_shared->mutex);
  if (m_shared->closed || !m_shared->owner || m_shared->owner->lease != lease || !Shared::live(*m_shared->owner))
    return false;
  if (m_shared->owner->active)
    return m_shared->owner->seal && m_shared->owner->seal->live();
  return !m_shared->operations.empty() && m_shared->operations.front()->observation &&
         m_shared->operations.front()->observation->live();
}
bool OriginalInputCoordinator::handoffReady(const LocalInputLease &lease) const
{
  std::shared_ptr<Shared::Operation> operation;
  {
    std::lock_guard lock(m_shared->mutex);
    if (m_shared->closed || !m_shared->owner || m_shared->owner->lease != lease || !Shared::live(*m_shared->owner) ||
        m_shared->operations.empty())
      return false;
    operation = m_shared->operations.front();
  }
  return m_providers.handoffBoundary && m_providers.handoffBoundary(operation->captured) &&
         operation->captured.input.live();
}
bool OriginalInputCoordinator::release(const LocalInputLease &lease)
{
  std::lock_guard lock(m_shared->mutex);
  if (m_shared->owner && (!m_shared->owner->lease || m_shared->owner->lease == lease)) {
    m_shared->owner->lease = lease;
    m_shared->owner->offer = static_cast<const LocalInputOffer &>(lease);
    m_shared->revoke("Private input preflight released the owner");
  }
  return true; // Local queued output is disabled before the ordered private revoke.
}
void OriginalInputCoordinator::stop()
{
  m_timer.stop();
  {
    std::lock_guard lock(m_shared->mutex);
    m_shared->closed = true;
    m_shared->wake = {};
    m_shared->available = false;
    m_shared->revoke();
  }
  if (m_channel) {
    m_channel->onInputTarget = {};
    m_channel->onReleased = {};
  }
  // Prior channel callbacks may synchronously delete this coordinator. Do not
  // access any member after entering the retained preflight's stop callback.
  if (m_preflight)
    m_preflight->stop();
}
void OriginalInputCoordinator::tick()
{
  const QPointer<OriginalInputCoordinator> alive(this);
  for (std::size_t count = 0; count < Shared::capacity; ++count)
    if (!tickOne() || !alive)
      return;
}
bool OriginalInputCoordinator::tickOne()
{
  const SlowOperationDiagnostic timing("Core input", "input.coordinator-service");
  const QPointer<OriginalInputCoordinator> alive(this);
  bool available = false;
  try {
    available = admitted();
  } catch (...) {
  }
  if (!alive)
    return false;
  bool drainRequested = false;
  {
    std::lock_guard lock(m_shared->mutex);
    drainRequested = m_shared->mappedDrainRequested;
  }
  if (available && drainRequested && m_channel) {
    const bool drained = m_channel->mappedOutputDrained();
    if (!alive)
      return false;
    if (drained) {
      std::lock_guard lock(m_shared->mutex);
      if (m_shared->mappedOwner && m_shared->mappedRecords) {
        auto &budget = m_shared->mappedOwner->deliveryBudget;
        const auto previous = budget.limit();
        const auto elapsed = Clock::now() - m_shared->mappedStarted;
        budget.observe(m_shared->mappedRecords, std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
        if (budget.limit() != previous)
          lifecycleDiagnostic(
              "Core input", "input.delivery-budget", "observed", "Adjusted mapped prefix to native dispatch time",
              {{"records", qint64(m_shared->mappedRecords)},
               {"elapsedUs", qint64(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count())},
               {"nextMaximumRecords", qint64(budget.limit())}}
          );
      }
      m_shared->mappedOwner.reset();
      m_shared->mappedRecords = 0;
      m_shared->mappedOutputDrained = true;
      m_shared->mappedDrainRequested = false;
    }
  }
  std::shared_ptr<Shared::Operation> operation;
  bool revoke = false;
  {
    std::lock_guard lock(m_shared->mutex);
    m_shared->available = available && !m_shared->closed;
    if (m_shared->owner && (!available || m_shared->closed || !Shared::live(*m_shared->owner))) {
      m_shared->revoke("Private coordinator lost admission or owner lifetime");
      revoke = true;
    }
    while (!m_shared->operations.empty() && m_shared->operations.front()->phase == Shared::Phase::Sent)
      inputEvidenceApply(InputEvidenceQueue::CoordinatorOperations, m_shared.get(), 0, 1, [&] {
        m_shared->operations.pop_front();
      });
    if (!m_shared->operations.empty()) {
      operation = m_shared->operations.front();
      if (Clock::now() >= operation->deadline || operation->phase == Shared::Phase::Revoked) {
        operation->trace(9, "input.handoff", Clock::now() >= operation->deadline ? "expired" : "revoked", true);
        m_shared->revoke("Private coordinator retired an expired or revoked operation");
        revoke = true;
      }
    }
  }
  if (revoke) {
    m_preflight->cancel(LocalInputRevocation::AuthorityLost);
    if (!alive)
      return false;
    std::lock_guard lock(m_shared->mutex);
    inputEvidenceApply(InputEvidenceQueue::CoordinatorOperations, m_shared.get(), 0, m_shared->operations.size(), [&] {
      m_shared->operations.clear();
    });
    if (m_shared->owner && m_shared->owner->hostReleased && m_shared->owner->coreReleased)
      m_shared->owner.reset();
    return false;
  }
  if (!operation)
    return false;
  Shared::Phase phase;
  std::optional<LocalInputLease> lease;
  std::optional<InputCutoffObservation> observation;
  bool active;
  {
    std::lock_guard lock(m_shared->mutex);
    phase = operation->phase;
    lease = operation->owner->lease;
    observation = operation->observation;
    active = operation->owner->active;
  }
  if (phase == Shared::Phase::Requested) {
    if (active) {
      std::lock_guard lock(m_shared->mutex);
      operation->phase = Shared::Phase::Preparing;
    } else if (m_preflight->phase() == LocalInputPreflight::Phase::Idle) {
      operation->trace(3, "input.host-offer", "started");
      const bool begun = m_preflight->begin(operation->captured.input.epoch, ForwardingScope::All);
      if (!alive)
        return false;
      const auto offer = m_preflight->offer();
      std::lock_guard lock(m_shared->mutex);
      if (offer) {
        operation->owner->offer = offer;
        operation->owner->hostReleased = false;
      }
      if (!begun)
        operation->owner->revoked = true;
    }
    return false;
  }
  if (phase == Shared::Phase::Mapped && lease) {
    if (!observation || !observation->live()) {
      std::lock_guard lock(m_shared->mutex);
      operation->owner->revoked = true;
      return false;
    }
    if (m_preflight->phase() == LocalInputPreflight::Phase::Quiesced) {
      operation->trace(7, "input.native-preparation", "started");
      (void)m_preflight->continuePreparation(*lease);
    }
    if (!alive)
      return false;
    if (m_preflight->phase() == LocalInputPreflight::Phase::Active) {
      std::lock_guard lock(m_shared->mutex);
      operation->owner->active = true;
      operation->phase = Shared::Phase::Ready;
      operation->trace(8, "input.handoff", "ready");
    }
    return false;
  }
  if (phase != Shared::Phase::Queued || !lease)
    return false;
  bool sent = false;
  const auto sendingStarted = Clock::now();
  if (observation && observation->live() && admitted() && alive && m_channel &&
      m_preflight->phase() == LocalInputPreflight::Phase::Active) {
    const auto &point = observation->snapshot();
    const auto deadlineNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(observation->deadline().time_since_epoch()).count();
    if (deadlineNs > 0)
      sent = m_channel->sendMappedOriginal(
          *lease, operation->captured.input.record,
          {point.sourceX, point.sourceY, point.sourceWidth, point.sourceHeight, static_cast<std::uint64_t>(deadlineNs)}
      );
  }
  if (!alive)
    return false;
  operation->trace(12, "input.delivery", sent ? "sent" : "failed", !sent);
  std::lock_guard lock(m_shared->mutex);
  operation->phase = sent ? Shared::Phase::Sent : Shared::Phase::Revoked;
  if (sent) {
    if (!m_shared->mappedRecords || m_shared->mappedOwner != operation->owner) {
      m_shared->mappedOwner = operation->owner;
      m_shared->mappedStarted = sendingStarted;
      m_shared->mappedRecords = 0;
    }
    ++m_shared->mappedRecords;
    m_shared->mappedOutputDrained = false;
    // Start the ordered barrier without waiting for the next input to arrive.
    // Otherwise idle time contaminates the measured service cost and every
    // new prefix pays an avoidable extra scheduling turn. Only its Pong can
    // mark output drained; later writes still require a later barrier.
    m_shared->mappedDrainRequested = true;
  }
  if (!sent)
    operation->owner->revoked = true;
  return sent;
}

} // namespace deskflow::windowshare
