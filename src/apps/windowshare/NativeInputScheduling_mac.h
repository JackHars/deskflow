/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <pthread/qos.h>

namespace deskflow::windowshare::native {
// A hidden helper's synchronous native reads serve pending interactive input.
// Donate interactive QoS only for that operation, then end the exact override.
// This is scheduling advice: failure grants no authority and changes no lease.
class InputScheduling final
{
public:
  using Start = decltype(&pthread_override_qos_class_start_np);
  using End = decltype(&pthread_override_qos_class_end_np);
  explicit InputScheduling(
      Start start = pthread_override_qos_class_start_np, End end = pthread_override_qos_class_end_np
  )
      : m_end(end),
        m_override(start(pthread_self(), QOS_CLASS_USER_INTERACTIVE, 0))
  {
  }
  ~InputScheduling()
  {
    if (m_override)
      m_end(m_override);
  }
  InputScheduling(const InputScheduling &) = delete;
  InputScheduling &operator=(const InputScheduling &) = delete;
  bool active() const
  {
    return m_override != nullptr;
  }

private:
  End m_end;
  pthread_override_t m_override;
};
} // namespace deskflow::windowshare::native
