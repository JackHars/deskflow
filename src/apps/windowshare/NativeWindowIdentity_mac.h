/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <QString>

namespace deskflow::windowshare::native {

// Experimental HIServices SPI. It identifies the native window containing an
// AX element; it does not establish drag ownership, focus or input authority.
bool axWindowIdentityAvailable() noexcept;
AXError axWindowId(AXUIElementRef element, CGWindowID &window) noexcept;

// Returns a retained, unique AX window from this process's actual window list.
// No title/geometry fallback, activation, placement, input or permission prompt.
AXUIElementRef
copyExactAXWindow(pid_t process, CGWindowID window, QString *diagnostic = nullptr, bool *retryable = nullptr);

} // namespace deskflow::windowshare::native
