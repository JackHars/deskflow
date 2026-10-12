/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SelectedPeerMailbox.h"
#include <array>
#include <barrier>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace deskflow::windowshare {
class SelectedPeerTestAccess
{
public:
  static SelectedPeer create(
      std::uint64_t server, std::uint64_t recipient, std::uint64_t generation, AuthenticatedPeerTicket ticket,
      std::shared_ptr<const std::atomic_bool> revoked
  )
  {
    return {server, recipient, generation, std::move(ticket), std::move(revoked)};
  }
};

struct SelectedPeerMailboxTestAccess
{
  using Clock = std::chrono::steady_clock;
  static bool replace(SelectedPeerMailbox &mailbox, const SelectedPeer &peer, Clock::time_point now)
  {
    return mailbox.replaceAt(peer, now);
  }
  static bool live(const SelectedPeerMailbox::Observation &observation, Clock::time_point now)
  {
    return observation.liveAt(now);
  }
  static void revision(SelectedPeerMailbox &mailbox, std::uint64_t revision)
  {
    std::lock_guard lock(mailbox.m_mutex);
    mailbox.m_revision = revision;
  }
};
} // namespace deskflow::windowshare

using namespace deskflow;
using namespace deskflow::windowshare;
using namespace std::chrono_literals;

namespace {
using Access = SelectedPeerMailboxTestAccess;
const auto start = Access::Clock::time_point(1000ms);

void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

struct Peer
{
  AuthenticatedPeerIssuer issuer;
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  SelectedPeer snapshot;

  explicit Peer(std::uint8_t identity)
  {
    std::array<std::uint8_t, 32> fingerprint;
    fingerprint.fill(identity);
    const auto ticket = issuer.issue(fingerprint);
    check(ticket.has_value(), "Cannot issue fixture TLS ticket");
    snapshot = SelectedPeerTestAccess::create(identity, identity + 10, identity + 20, *ticket, revoked);
  }
};

void emptyAndInvalid()
{
  SelectedPeerMailbox mailbox;
  const auto empty = mailbox.read();
  check(!empty.closed && !empty.selected && empty.revision == 0, "Mailbox began with peer authority");
  check(!SelectedPeer{}.live() && !mailbox.readAfter(empty.revision), "Default peer or unchanged read became live");
  check(!mailbox.replace(SelectedPeer{}), "Mailbox accepted missing identity");
  const auto changed = mailbox.readAfter(empty.revision);
  check(changed && changed->revision > empty.revision && !changed->selected, "Invalid publication did not clear");
  mailbox.close();
  const auto closed = mailbox.read();
  check(closed.closed && !closed.selected && !mailbox.clear(), "Empty closure was not terminal");
  mailbox.close();
  check(!mailbox.readAfter(closed.revision), "Repeated close changed terminal state");
}

void freshnessAndRefresh()
{
  Peer peer(1);
  SelectedPeerMailbox mailbox;
  check(Access::replace(mailbox, peer.snapshot, start), "Live peer was rejected");
  const auto first = mailbox.read();
  check(first.selected && Access::live(*first.selected, start + 249ms), "Observation expired before its budget");
  check(!Access::live(*first.selected, start - 1ms), "Future-dated evidence became live");
  check(Access::replace(mailbox, peer.snapshot, start + 200ms), "Same selection refresh was rejected");
  const auto fresh = mailbox.readAfter(first.revision);
  check(
      fresh && fresh->selected && fresh->revision > first.revision && fresh->selected->revision() == fresh->revision &&
          fresh->selected->peer().sameSelection(first.selected->peer()),
      "Refresh failed to publish a coherent new observation"
  );
  check(Access::live(*first.selected, start + 249ms), "Same live selection prematurely invalidated retained read");
  check(!Access::live(*first.selected, start + 250ms), "Refresh extended a retained observation's lifetime");
  check(Access::live(*fresh->selected, start + 250ms), "Refresh failed to renew the new observation");
  check(!Access::live(*fresh->selected, start + 450ms), "Stalled publisher retained authority indefinitely");
  check(
      mailbox.read().selected.has_value() && !mailbox.readAfter(fresh->revision),
      "Expiry incorrectly altered publication revision or removed evidence needed for revocation"
  );
}

void replacementAndRevocation()
{
  Peer first(1), second(2);
  SelectedPeerMailbox mailbox;
  check(Access::replace(mailbox, first.snapshot, start), "Initial peer was rejected");
  const auto old = *mailbox.read().selected;
  check(Access::replace(mailbox, second.snapshot, start + 1ms), "Replacement peer was rejected");
  const auto replacement = *mailbox.read().selected;
  check(!Access::live(old, start + 2ms), "Replacement left previous mailbox observation live");
  check(first.snapshot.live(), "Mailbox replacement revoked the independently owned server selection");
  check(Access::live(replacement, start + 2ms), "Replacement observation was not live");
  check(mailbox.clear(), "Open mailbox rejected clear");
  check(!Access::live(replacement, start + 2ms) && !mailbox.read().selected, "Clear retained peer authority");

  check(Access::replace(mailbox, first.snapshot, start + 3ms), "Cleared mailbox could not publish a new selection");
  const auto selected = *mailbox.read().selected;
  check(!Access::live(old, start + 4ms), "Republishing a previous peer revived an old observation");
  first.revoked->store(true, std::memory_order_release);
  check(!Access::live(selected, start + 4ms), "Server selection revocation was not observed immediately");
  check(!Access::replace(mailbox, first.snapshot, start + 4ms), "Revoked selection was accepted");
  check(!mailbox.read().selected, "Invalid replacement left previous evidence current");

  check(Access::replace(mailbox, second.snapshot, start + 5ms), "Live TLS fixture was rejected");
  const auto authenticated = *mailbox.read().selected;
  const auto revision = mailbox.read().revision;
  second.issuer.revoke();
  check(!Access::live(authenticated, start + 6ms), "TLS revocation did not fence a retained observation");
  const auto stale = mailbox.readAfter(revision - 1);
  check(stale && stale->revision == revision && stale->selected, "Read hid externally revoked publication changes");

  // Identical numeric fields are insufficient: a fresh selection token must
  // invalidate the previous observation even while the old peer remains live.
  Peer third(3);
  check(Access::replace(mailbox, third.snapshot, start + 7ms), "Tuple fixture was rejected");
  const auto before = *mailbox.read().selected;
  const auto newToken = std::make_shared<std::atomic_bool>(false);
  const auto sameFields = SelectedPeerTestAccess::create(
      third.snapshot.serverIncarnation(), third.snapshot.recipient(), third.snapshot.generation(),
      third.snapshot.ticket(), newToken
  );
  check(Access::replace(mailbox, sameFields, start + 8ms), "New token with same fields was rejected");
  check(!Access::live(before, start + 9ms), "Numeric tuple reuse revived the old selection token");
}

void closureAndLifetime()
{
  Peer peer(1);
  auto mailbox = std::make_shared<SelectedPeerMailbox>();
  auto publisher = mailbox;
  check(Access::replace(*mailbox, peer.snapshot, start), "Lifetime fixture was rejected");
  const auto retained = *mailbox->read().selected;
  mailbox->close();
  const auto terminal = mailbox->read();
  mailbox.reset();
  check(!Access::live(retained, start + 1ms), "Close did not revoke a retained observation");
  check(!publisher->replace(peer.snapshot) && !publisher->clear(), "Lingering producer reopened closed mailbox");
  check(publisher->read().closed && !publisher->read().selected, "Closed mailbox revived old data");
  check(!publisher->readAfter(terminal.revision), "Denied publication advanced a closed mailbox");

  std::optional<SelectedPeerMailbox::Observation> orphaned;
  {
    SelectedPeerMailbox owner;
    check(Access::replace(owner, peer.snapshot, start), "Destructor fixture was rejected");
    orphaned = owner.read().selected;
  }
  check(orphaned && !Access::live(*orphaned, start + 1ms), "Destructor left retained mailbox authority live");
}

void revisionExhaustion()
{
  Peer peer(1);
  for (const bool clear : {false, true}) {
    SelectedPeerMailbox mailbox;
    Access::revision(mailbox, std::numeric_limits<std::uint64_t>::max() - 2);
    check(Access::replace(mailbox, peer.snapshot, start), "Final usable revision was denied");
    const auto last = mailbox.read();
    check(last.revision == std::numeric_limits<std::uint64_t>::max() - 1, "Fixture revision did not advance");
    check(!(clear ? mailbox.clear() : mailbox.replace(peer.snapshot)), "Exhausted revision was accepted");
    const auto closed = mailbox.readAfter(last.revision);
    check(
        closed && closed->closed && !closed->selected &&
            closed->revision == std::numeric_limits<std::uint64_t>::max() && !Access::live(*last.selected, start + 1ms),
        "Revision exhaustion wrapped or retained authority"
    );
    check(!mailbox.replace(peer.snapshot) && !mailbox.clear(), "Exhausted mailbox reopened");
  }
}

void concurrentPublicationAndClose()
{
  Peer first(1), second(2);
  auto mailbox = std::make_shared<SelectedPeerMailbox>();
  check(mailbox->replace(first.snapshot), "Concurrent fixture was rejected");
  std::barrier begin(4);
  std::atomic<unsigned> published{0};
  std::atomic_bool closed{false}, failed{false};
  std::thread producer([mailbox, &first, &second, &begin, &published, &closed, &failed] {
    begin.arrive_and_wait();
    for (unsigned i = 0; i < 5000; ++i) {
      if (!mailbox->replace(i % 2 ? first.snapshot : second.snapshot))
        break;
      if (i % 7 == 0)
        mailbox->clear();
      published.fetch_add(1, std::memory_order_release);
    }
    while (!closed.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (unsigned i = 0; i < 100; ++i)
      if (mailbox->replace(first.snapshot) || mailbox->clear())
        failed.store(true, std::memory_order_release);
  });
  const auto reader = [mailbox, &first, &second, &begin, &closed, &failed] {
    begin.arrive_and_wait();
    std::uint64_t revision = 0;
    std::array<std::optional<SelectedPeerMailbox::Observation>, 8> retained;
    unsigned count = 0;
    while (!closed.load(std::memory_order_acquire)) {
      const auto read = mailbox->read();
      if (read.revision < revision || (read.closed && read.selected))
        failed.store(true, std::memory_order_release);
      revision = read.revision;
      if (read.selected) {
        if (read.selected->revision() != read.revision || !(read.selected->peer().sameSelection(first.snapshot) ||
                                                            read.selected->peer().sameSelection(second.snapshot)))
          failed.store(true, std::memory_order_release);
        retained[count++ % retained.size()] = read.selected;
        // Exercise the independent revocation atomics during writer activity.
        (void)read.selected->live();
      }
      (void)mailbox->readAfter(revision);
    }
    const auto final = mailbox->read();
    if (!final.closed || final.selected)
      failed.store(true, std::memory_order_release);
    for (const auto &read : retained)
      if (read && read->live())
        failed.store(true, std::memory_order_release);
  };
  std::thread firstReader(reader), secondReader(reader);
  std::thread closer([mailbox, &begin, &published, &closed] {
    begin.arrive_and_wait();
    while (published.load(std::memory_order_acquire) < 100)
      std::this_thread::yield();
    mailbox->close();
    closed.store(true, std::memory_order_release);
  });
  producer.join();
  firstReader.join();
  secondReader.join();
  closer.join();
  check(!failed.load(std::memory_order_acquire), "Concurrent publication/read/close violated mailbox invariants");
}
} // namespace

int main()
{
  try {
    emptyAndInvalid();
    freshnessAndRefresh();
    replacementAndRevocation();
    closureAndLifetime();
    revisionExhaustion();
    concurrentPublicationAndClose();
    std::cout << "PASS: bounded selected-peer mailbox, immutable freshness, same-selection refresh, immediate "
                 "replacement/TLS/selection/close revocation, terminal overflow, retained lifetime, concurrent "
                 "publication/read/close\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
