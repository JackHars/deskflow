/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "GnomeProviderConfig.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace deskflow::windowshare {

// Owns a private D-Bus connection and one provider-owned EIS/viewer lifetime.
// It neither attributes input events nor grants original-input delivery.
// All D-Bus work runs on its own cancellable GLib context, never the input path.
class GnomeProviderClient final
{
public:
  struct Update
  {
    // Ownership transfers to the consumer, even when ready is false. Close any
    // descriptor that cannot be passed to libei. Descriptors never live in raw
    // EventQueue payloads and remain owned here until taken.
    int descriptor = -1;
    bool ready = false;
    bool closed = false;
    std::string error;
    std::string session;
    std::string viewer;
    std::uint32_t generation = 0;
  };

  // changed runs on the worker and must only wake the consumer. It must not
  // borrow platform state. Notifications are coalesced until takeUpdate().
  GnomeProviderClient(GnomeProviderConfig configuration, std::function<void()> changed);
  ~GnomeProviderClient();
  GnomeProviderClient(const GnomeProviderClient &) = delete;
  GnomeProviderClient &operator=(const GnomeProviderClient &) = delete;
  Update takeUpdate();
  bool live() const noexcept;
  // Terminal; cancellation interrupts pending D-Bus calls. No automatic
  // fallback/reconnect can silently replace the retained EIS/viewer identity.
  void stop() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
