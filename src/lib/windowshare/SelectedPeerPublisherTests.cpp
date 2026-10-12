/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SelectedPeerMailbox.h"
#include "SelectedPeerPublisher.h"
#include "base/EventQueueTimer.h"
#include "base/IEventQueue.h"
#include <iostream>
#include <stdexcept>

namespace deskflow::windowshare {
class SelectedPeerTestAccess
{
public:
  static SelectedPeer make(const AuthenticatedPeerTicket &ticket, std::shared_ptr<const std::atomic_bool> revoked)
  {
    return SelectedPeer(1, 2, 3, ticket, std::move(revoked));
  }
};
} // namespace deskflow::windowshare

using namespace deskflow;
using namespace deskflow::windowshare;
namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

class Events final : public IEventQueue
{
public:
  int loop() override
  {
    return 0;
  }
  void adoptBuffer(IEventQueueBuffer *) override
  {
  }
  bool getEvent(Event &, double) override
  {
    return false;
  }
  bool dispatchEvent(const Event &event) override
  {
    const auto copy = handler;
    if (!copy)
      return false;
    copy(event);
    return true;
  }
  void addEvent(Event &&) override
  {
    throw std::runtime_error("Publisher must not enqueue startup work");
  }
  EventQueueTimer *newTimer(double duration, void *newTarget) override
  {
    require(duration == 0.020, "Unexpected publication cadence");
    require(!timer, "Timer was not cleaned up");
    target = newTarget;
    timer = std::make_unique<EventQueueTimer>();
    return timer.get();
  }
  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return nullptr;
  }
  void deleteTimer(EventQueueTimer *value) override
  {
    require(value == timer.get(), "Wrong timer removed");
    timer.reset();
  }
  void addHandler(EventTypes type, void *value, const EventHandler &callback) override
  {
    require(type == EventTypes::Timer && value == target, "Wrong publication handler");
    handler = callback;
    if (failRegistration)
      throw std::runtime_error("Fixture registration failure");
  }
  void removeHandler(EventTypes type, void *value) override
  {
    require(type == EventTypes::Timer && value == target, "Wrong handler removed");
    handler = {};
  }
  void removeHandlers(void *) override
  {
  }
  void waitForReady() const override
  {
    throw std::runtime_error("Publisher must not block awaiting startup");
  }
  void *getSystemTarget() override
  {
    return nullptr;
  }
  void fire()
  {
    const Event event(EventTypes::Timer, target);
    dispatchEvent(event);
  }
  EventHandler handler;
  void *target = nullptr;
  std::unique_ptr<EventQueueTimer> timer;
  bool failRegistration = false;
};

struct Fixture
{
  AuthenticatedPeerIssuer issuer;
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  SelectedPeer peer;
  Fixture()
  {
    AuthenticatedPeerTicket::Fingerprint fingerprint{};
    fingerprint.fill(0x2a);
    const auto ticket = issuer.issue(fingerprint);
    require(ticket.has_value(), "Cannot create fixture ticket");
    peer = SelectedPeerTestAccess::make(*ticket, revoked);
  }
};

void timerReadinessAndStop()
{
  Events events;
  Fixture fixture;
  const auto mailbox = std::make_shared<SelectedPeerMailbox>();
  int reads = 0;
  SelectedPeerPublisher publisher(events, mailbox, [&]() -> std::optional<SelectedPeer> {
    ++reads;
    return fixture.peer;
  });
  require(reads == 0 && !mailbox->read().selected, "Published before core dispatch readiness");
  events.fire();
  const auto selected = mailbox->read().selected;
  require(reads == 1 && selected && selected->live(), "Missing dispatched selection");
  const auto late = events.handler;
  publisher.stop();
  require(
      !selected->live() && !mailbox->read().selected && !events.timer && !events.handler,
      "Stop did not revoke before cleanup"
  );
  late(Event(EventTypes::Timer));
  require(reads == 1 && !mailbox->read().selected, "Late timer revived stopped publication");
  publisher.stop();
}

void revocationBeforeNextTimer()
{
  Events events;
  Fixture fixture;
  const auto mailbox = std::make_shared<SelectedPeerMailbox>();
  SelectedPeerPublisher publisher(events, mailbox, [&] { return std::optional(fixture.peer); });
  events.fire();
  const auto selected = mailbox->read().selected;
  require(selected && selected->live(), "Initial selection missing");
  fixture.issuer.revoke();
  require(!selected->live(), "TLS loss waited for core timer");
  events.fire();
  require(!mailbox->read().selected, "Core republished revoked TLS ticket");
}

void readerFailureAndDestruction()
{
  Events events;
  Fixture fixture;
  const auto mailbox = std::make_shared<SelectedPeerMailbox>();
  int reads = 0;
  SelectedPeerPublisher publisher(events, mailbox, [&]() -> std::optional<SelectedPeer> {
    if (++reads == 2)
      throw std::runtime_error("Fixture reader failure");
    return fixture.peer;
  });
  events.fire();
  const auto selected = mailbox->read().selected;
  events.fire();
  require(selected && !selected->live() && !mailbox->read().selected, "Reader failure retained authority");
  events.fire();
  require(reads == 2, "Failed reader was reactivated");
  publisher.stop();

  std::unique_ptr<SelectedPeerPublisher> self;
  self = std::make_unique<SelectedPeerPublisher>(events, mailbox, [&] {
    self.reset();
    return std::optional(fixture.peer);
  });
  events.fire();
  require(!self && !mailbox->read().selected && !events.timer, "Reentrant destruction republished authority");
}

void restartAndTerminalClose()
{
  Events events;
  Fixture fixture;
  const auto mailbox = std::make_shared<SelectedPeerMailbox>();
  auto publisher =
      std::make_unique<SelectedPeerPublisher>(events, mailbox, [&] { return std::optional(fixture.peer); });
  events.fire();
  const auto previous = mailbox->read().selected;
  const auto late = events.handler;
  publisher.reset();
  publisher = std::make_unique<SelectedPeerPublisher>(events, mailbox, [&] { return std::optional(fixture.peer); });
  events.fire();
  const auto current = mailbox->read().selected;
  late(Event(EventTypes::Timer));
  require(previous && !previous->live() && current && current->live(), "Restart revived old publication");
  mailbox->close();
  require(!current->live(), "Session closure did not immediately revoke retained value");
  events.fire();
  require(mailbox->read().closed && !mailbox->read().selected, "Publisher reopened terminal mailbox");
}

void failedRegistrationCleansUp()
{
  Events events;
  events.failRegistration = true;
  const auto mailbox = std::make_shared<SelectedPeerMailbox>();
  bool failed = false;
  try {
    SelectedPeerPublisher publisher(events, mailbox, [] { return std::optional<SelectedPeer>{}; });
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(
      failed && !events.timer && !events.handler && !mailbox->read().selected,
      "Failed registration left publication resources"
  );
}
} // namespace

int main()
{
  try {
    timerReadinessAndStop();
    revocationBeforeNextTimer();
    readerFailureAndDestruction();
    restartAndTerminalClose();
    failedRegistrationCleansUp();
    std::cout << "Selected peer publisher tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
