/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"
#include <QJsonArray>
#include <QJsonObject>

namespace deskflow::windowshare {

inline constexpr auto keyboardRouteCapability = "keyboard-route-v1";
QJsonArray keyboardRouteCapabilities();
bool peerSupportsKeyboardRoute(const QJsonObject &negotiation);
QJsonObject encodeKeyboardRouteControl(const KeyboardRouteControl &control);
KeyboardRouteControl parseKeyboardRouteControl(const Packet &packet);

} // namespace deskflow::windowshare
