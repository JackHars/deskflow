/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "InputCutoff.h"
#include "LocalInputPreflight.h"
#include "OriginalInputBackend.h"
#include "OriginalInputDispatch.h"
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace deskflow::windowshare {

// Qt owns the private channel and preflight. Core owns the dispatcher and all
// backend callbacks. They exchange bounded, retained values; a Server, proxy,
// QObject or native event never crosses the thread boundary.
class OriginalInputCoordinator final : public QObject
{
public:
  using Clock = OriginalInputDispatcher::Clock;
  struct Providers
  {
    std::function<bool()> codeAdmission;
    // Exact source-side acquisition boundary. Missing/unsupported evidence
    // denies preparation; capture continuity alone is not a neutral boundary.
    std::function<bool(const OriginalInputCaptured &)> handoffBoundary;
  };
  using Backend = OriginalInputBackend;
  OriginalInputCoordinator(
      LocalInputChannel &channel, std::shared_ptr<SelectedPeerMailbox> selected, LocalInputPeerPolicy policy,
      Providers providers
  );
  ~OriginalInputCoordinator() override;
  // Call once on Core's event thread after Server exists. Neither construction
  // nor registration grants authority: every actual attempt runs preflight.
  std::shared_ptr<OriginalInputDispatcher> createDispatcher(Backend backend);
  using DispatcherFactory = std::function<std::shared_ptr<OriginalInputDispatcher>(Backend)>;
  DispatcherFactory dispatcherFactory() const;
  void stop();

private:
  struct Shared;
  class Dispatcher;
  void tick();
  bool tickOne();
  bool admitted() const;
  bool deliveryReady(const LocalInputLease &lease) const;
  bool handoffReady(const LocalInputLease &lease) const;
  bool release(const LocalInputLease &lease);
  std::shared_ptr<Shared> m_shared;
  QPointer<LocalInputChannel> m_channel;
  Providers m_providers;
  std::unique_ptr<LocalInputPreflight> m_preflight;
  QTimer m_timer;
};

} // namespace deskflow::windowshare
