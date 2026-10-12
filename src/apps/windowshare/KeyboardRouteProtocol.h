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
inline constexpr auto inputRouteCapability = "input-route-v1";
QJsonArray keyboardRouteCapabilities(bool includeInput = false);
bool peerSupportsKeyboardRoute(const QJsonObject &negotiation);
bool peerSupportsInputRoute(const QJsonObject &negotiation);
MessageType forwardingRouteMessageType(const KeyboardRouteBinding &binding);
QJsonObject encodeKeyboardRouteControl(const KeyboardRouteControl &control);
KeyboardRouteControl parseKeyboardRouteControl(const Packet &packet);

} // namespace deskflow::windowshare
