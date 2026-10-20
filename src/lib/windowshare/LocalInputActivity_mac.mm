/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputActivity.h"
#include "NativeSharingActivity_mac.h"

namespace deskflow::windowshare {
struct LocalInputActivity::State
{
  native::SharingActivity activity;
};
LocalInputActivity::LocalInputActivity() : m_state(std::make_unique<State>())
{
}
LocalInputActivity::~LocalInputActivity() = default;
bool LocalInputActivity::active() const
{
  return m_state->activity.active();
}
} // namespace deskflow::windowshare
