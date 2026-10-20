/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "KeyboardRoute.h"
#include <QTimer>
#include <chrono>
#include <functional>
#include <optional>

namespace deskflow::windowshare {

// Schedules idle inspection only; never authorizes input. A successful native
// delivery already inspected this exact owner. Repeat that inspection when
// idle, scheduled 500 ms after the last delivery, rather than competing with the
// next event's mandatory fresh checks. Permission/topology polling is separate.
class NativeInputWatchdog final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto interval = std::chrono::milliseconds(500);
  explicit NativeInputWatchdog(std::function<void()> inspect, std::function<Clock::time_point()> now = Clock::now)
      : m_inspect(std::move(inspect)),
        m_now(std::move(now))
  {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] { expire(); });
  }
  void completed(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch)
  {
    clear();
    if (!validKeyboardRouteBinding(binding) || binding.scope != ForwardingScope::All || !captureEpoch)
      return;
    m_binding = binding;
    m_epoch = captureEpoch;
    m_checked = m_now();
    m_timer.start(interval);
  }
  bool covered(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
  {
    const auto now = m_now();
    return m_binding == binding && m_epoch == captureEpoch && now >= m_checked && now < m_checked + interval;
  }
  void clear()
  {
    m_timer.stop();
    m_binding.reset();
    m_epoch = 0;
  }
  void expire()
  {
    if (!m_binding)
      return;
    const auto remaining = m_checked + interval - m_now();
    if (remaining > Clock::duration::zero()) {
      // A queued timeout from an earlier delivery cannot inspect too early.
      m_timer.start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
      return;
    }
    clear();
    const auto inspect = m_inspect;
    inspect(); // May destroy this object; no access after the callback.
  }

private:
  QTimer m_timer;
  std::function<void()> m_inspect;
  std::function<Clock::time_point()> m_now;
  std::optional<KeyboardRouteBinding> m_binding;
  std::uint64_t m_epoch = 0;
  Clock::time_point m_checked;
};
} // namespace deskflow::windowshare
