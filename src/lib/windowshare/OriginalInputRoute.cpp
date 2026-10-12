/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInputRoute.h"
#include <limits>

namespace deskflow::windowshare {
namespace {
OriginalInputRouteKind kind(const OriginalInputRecord &record) noexcept
{
  switch (record.payload.index()) {
  case 0:
    return OriginalInputRouteKind::Key;
  case 1:
    return OriginalInputRouteKind::Motion;
  case 2:
    return OriginalInputRouteKind::Button;
  case 3:
    return OriginalInputRouteKind::Scroll;
  default:
    return OriginalInputRouteKind::Other;
  }
}
} // namespace
bool OriginalInputRouted::live() const noexcept
{
  return input.live() && routeRevoked && !routeRevoked->load(std::memory_order_acquire) &&
         ((route.outcome != OriginalInputRouteOutcome::Dispatched &&
           route.outcome != OriginalInputRouteOutcome::Original) ||
          (route.server && route.recipient && route.ticket.live()));
}
OriginalInputRouteJoin::OriginalInputRouteJoin(OriginalInputEpoch epoch, std::shared_ptr<OriginalInputRoutes> routes)
    : m_epoch(std::move(epoch)),
      m_routes(std::move(routes))
{
}
bool OriginalInputRouteJoin::live() const noexcept
{
  return m_epoch.live() && m_routes && m_routes->live();
}
void OriginalInputRouteJoin::revoke() noexcept
{
  if (m_routes)
    m_routes->revoke();
}
OriginalInputRouteJoin::Slot *OriginalInputRouteJoin::slot(std::uint64_t sequence) noexcept
{
  if (!live() || sequence < m_next || sequence - m_next >= capacity) {
    revoke();
    return nullptr;
  }
  auto &result = m_slots[sequence % capacity];
  if (result.sequence && result.sequence != sequence) {
    revoke();
    return nullptr;
  }
  result.sequence = sequence;
  return &result;
}
bool OriginalInputRouteJoin::capture(OriginalInputCaptured captured, Clock::time_point now) noexcept
{
  const auto &record = captured.input.record;
  const auto &translation = captured.translation;
  if (!live() || !captured.input.live() || !captured.input.epoch.sameEpoch(m_epoch) ||
      m_captured == std::numeric_limits<std::uint64_t>::max() || record.sequence != m_captured + 1 ||
      translation.stamp != OriginalInputStamp{record.epoch, record.sequence, 0} ||
      translation.parts > maximumOriginalInputParts) {
    revoke();
    return false;
  }
  auto *entry = slot(record.sequence);
  if (!entry || entry->captured) {
    revoke();
    return false;
  }
  ++m_captured;
  entry->captured = std::move(captured);
  entry->deadline = now + timeout;
  return true;
}
bool OriginalInputRouteJoin::decision(OriginalInputRouteDecision decision, Clock::time_point now) noexcept
{
  const auto stamp = decision.stamp;
  if (!live() || stamp.epoch != m_epoch.id() || !stamp.sequence || !stamp.part ||
      stamp.part > maximumOriginalInputParts || stamp.sequence < m_lastDecisionSequence ||
      (stamp.sequence == m_lastDecisionSequence && stamp.part <= m_lastDecisionPart)) {
    revoke();
    return false;
  }
  m_lastDecisionSequence = stamp.sequence;
  m_lastDecisionPart = stamp.part;
  // Expanded/uncertain records can retire before the core translates
  // them. Their late decisions remain ordering-checked and cannot revive them.
  if (stamp.sequence < m_next) {
    const auto &retired = m_retired[stamp.sequence % capacity];
    if (retired.sequence != stamp.sequence || !retired.allowLate || stamp.part > retired.parts) {
      revoke();
      return false;
    }
    return true;
  }
  auto *entry = slot(stamp.sequence);
  if (!entry)
    return false;
  if (entry->captured && entry->captured->translation.parts == 1 && now >= entry->deadline) {
    revoke();
    return false;
  }
  if (!entry->decision)
    entry->decision = std::move(decision);
  else
    entry->decision->outcome = OriginalInputRouteOutcome::Expanded;
  return true;
}
std::optional<OriginalInputRouted> OriginalInputRouteJoin::pop(Clock::time_point now) noexcept
{
  if (!live())
    return {};
  auto &entry = m_slots[m_next % capacity];
  if (entry.sequence != m_next || !entry.captured)
    return {};
  const auto &translation = entry.captured->translation;
  OriginalInputRouteDecision result;
  result.stamp = translation.stamp;
  result.kind = kind(entry.captured->input.record);
  if (translation.uncertain)
    result.outcome = OriginalInputRouteOutcome::Uncorrelated;
  else if (!translation.parts)
    result.outcome = OriginalInputRouteOutcome::NoTranslation;
  else if (translation.parts > 1)
    result.outcome = OriginalInputRouteOutcome::Expanded;
  else if (now >= entry.deadline) {
    revoke();
    return {};
  } else if (!entry.decision)
    return {};
  else {
    result = *entry.decision;
    if (result.stamp.part != 1 || result.kind != kind(entry.captured->input.record))
      result.outcome = OriginalInputRouteOutcome::Uncorrelated;
    if ((result.outcome == OriginalInputRouteOutcome::Dispatched ||
         result.outcome == OriginalInputRouteOutcome::Original) &&
        (!result.server || !result.recipient || !result.ticket.live()))
      result.outcome = OriginalInputRouteOutcome::Unauthenticated;
  }
  OriginalInputRouted joined{entry.captured->input, std::move(result), m_routes->evidence()};
  m_retired[m_next % capacity] = {m_next, translation.parts, translation.uncertain || translation.parts > 1};
  entry = {};
  if (m_next == std::numeric_limits<std::uint64_t>::max()) {
    revoke();
    return {};
  }
  ++m_next;
  if (!live())
    return {};
  return joined;
}
} // namespace deskflow::windowshare
