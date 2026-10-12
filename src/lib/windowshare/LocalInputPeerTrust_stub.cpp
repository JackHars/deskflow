/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputPeerTrust.h"

namespace deskflow::windowshare {
struct LocalInputPeerTrust::Impl
{
};
LocalInputPeerTrust::LocalInputPeerTrust(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}
LocalInputPeerTrust::~LocalInputPeerTrust() = default;
std::unique_ptr<LocalInputPeerTrust> LocalInputPeerTrust::forPackagedPeer(LocalInputRole, QString *error)
{
  if (error)
    *error = QStringLiteral("Packaged original-input peer verification requires macOS");
  return nullptr;
}
QString LocalInputPeerTrust::executable() const
{
  return {};
}
bool LocalInputPeerTrust::verify(LocalInputProcess) const
{
  return false;
}
} // namespace deskflow::windowshare
