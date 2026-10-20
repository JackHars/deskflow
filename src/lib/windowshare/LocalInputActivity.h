/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <memory>

namespace deskflow::windowshare {
// C++ owner for the Mac process activity. No native input hooks or authority.
class LocalInputActivity final
{
public:
  LocalInputActivity();
  ~LocalInputActivity();
  bool active() const;
  LocalInputActivity(const LocalInputActivity &) = delete;
  LocalInputActivity &operator=(const LocalInputActivity &) = delete;

private:
  struct State;
  std::unique_ptr<State> m_state;
};
} // namespace deskflow::windowshare
