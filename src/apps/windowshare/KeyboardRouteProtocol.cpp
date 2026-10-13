/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "KeyboardRouteProtocol.h"
#include "Transport.h"
#include <QSet>
#include <array>
#include <cmath>

namespace deskflow::windowshare {
namespace {
constexpr std::array actions{"request", "ack", "withdraw"};
constexpr std::array modes{"viewer", "original"};
constexpr std::array results{"none", "ready", "busy", "stale", "denied"};
constexpr std::array reasons{"none",         "focus",   "geometry",  "hidden", "returned",
                             "disconnected", "timeout", "authority", "failed"};
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <typename Enum, std::size_t N> QString name(Enum value, const std::array<const char *, N> &names)
{
  const auto index = static_cast<unsigned>(value);
  require(index < names.size(), "Invalid keyboard route enum");
  return QLatin1String(names[index]);
}
template <typename Enum, std::size_t N> Enum value(const QJsonValue &input, const std::array<const char *, N> &names)
{
  require(input.isString(), "Missing keyboard route enum");
  for (std::size_t index = 0; index < names.size(); ++index)
    if (input.toString() == QLatin1String(names[index]))
      return static_cast<Enum>(index);
  throw std::runtime_error("Unknown keyboard route enum");
}
std::uint64_t number(const QJsonValue &value)
{
  const auto n = value.toDouble(-1);
  require(
      value.isDouble() && std::isfinite(n) && n >= 1 && n <= 9007199254740991.0 && std::floor(n) == n,
      "Invalid keyboard route revision"
  );
  return static_cast<std::uint64_t>(n);
}
void validate(const KeyboardRouteControl &control)
{
  require(validKeyboardRouteBinding(control.binding), "Invalid keyboard route binding");
  name(control.action, actions);
  name(control.mode, modes);
  name(control.result, results);
  name(control.reason, reasons);
  require(
      !control.viewerConnection ||
          (control.binding.scope == ForwardingScope::All && control.action == KeyboardRouteAction::Acknowledge &&
           control.result == KeyboardRouteResult::Ready && control.mode == KeyboardRouteMode::Original),
      "Invalid viewer connection attestation"
  );
  if (control.action == KeyboardRouteAction::Request)
    require(
        control.result == KeyboardRouteResult::None && control.reason == KeyboardRouteReason::None,
        "Invalid keyboard route request"
    );
  else if (control.action == KeyboardRouteAction::Acknowledge)
    require(
        control.result != KeyboardRouteResult::None && control.reason == KeyboardRouteReason::None,
        "Invalid keyboard route acknowledgement"
    );
  else
    require(
        control.mode == KeyboardRouteMode::Original && control.result == KeyboardRouteResult::None &&
            control.reason != KeyboardRouteReason::None,
        "Invalid keyboard route withdrawal"
    );
}
} // namespace
QJsonArray keyboardRouteCapabilities(bool includeInput, bool includeMediaRecovery, bool includeMapped)
{
  QJsonArray capabilities{QLatin1String(keyboardRouteCapability)};
  if (includeInput)
    capabilities.append(QLatin1String(inputRouteCapability));
  if (includeMediaRecovery)
    capabilities.append(QLatin1String(mediaRecoveryCapability));
  if (includeInput && includeMapped)
    capabilities.append(QLatin1String(mappedInputCapability));
  return capabilities;
}
bool peerSupportsMediaRecovery(const QJsonObject &negotiation)
{
  // The shared parser enforces a bounded list of unique, well-formed names.
  return peerSupportsKeyboardRoute(negotiation) &&
         negotiation["capabilities"].toArray().contains(QLatin1String(mediaRecoveryCapability));
}
bool peerSupportsKeyboardRoute(const QJsonObject &negotiation)
{
  if (!negotiation.contains("capabilities"))
    return false;
  const auto value = negotiation["capabilities"];
  require(value.isArray() && value.toArray().size() <= 16, "Invalid capability negotiation");
  QSet<QString> seen;
  for (const auto &capability : value.toArray()) {
    require(capability.isString(), "Invalid capability name");
    const auto name = capability.toString();
    require(!name.isEmpty() && name.size() <= 64 && !seen.contains(name), "Invalid or duplicate capability");
    seen.insert(name);
  }
  return seen.contains(QLatin1String(keyboardRouteCapability));
}
bool peerSupportsInputRoute(const QJsonObject &negotiation)
{
  // Validate the complete list and require the base route protocol as well.
  return peerSupportsKeyboardRoute(negotiation) &&
         negotiation["capabilities"].toArray().contains(QLatin1String(inputRouteCapability));
}
bool peerSupportsMappedInput(const QJsonObject &negotiation)
{
  return peerSupportsInputRoute(negotiation) &&
         negotiation["capabilities"].toArray().contains(QLatin1String(mappedInputCapability));
}
MessageType forwardingRouteMessageType(const KeyboardRouteBinding &binding)
{
  require(validKeyboardRouteBinding(binding), "Invalid forwarding route binding");
  return binding.scope == ForwardingScope::All ? MessageType::InputRoute : MessageType::KeyboardRoute;
}
QJsonObject encodeKeyboardRouteControl(const KeyboardRouteControl &control)
{
  validate(control);
  const auto &binding = control.binding;
  QJsonObject object{
      {"generation", static_cast<qint64>(binding.generation)},
      {"focusRequest", static_cast<qint64>(binding.focusRequest)},
      {"focusRevision", static_cast<qint64>(binding.focusRevision)},
      {"geometry", static_cast<qint64>(binding.geometry)},
      {"action", name(control.action, actions)},
      {"mode", name(control.mode, modes)}
  };
  if (control.action == KeyboardRouteAction::Acknowledge)
    object.insert("result", name(control.result, results));
  else if (control.action == KeyboardRouteAction::Withdraw)
    object.insert("reason", name(control.reason, reasons));
  if (control.viewerConnection)
    object.insert("viewerConnection", QString::number(control.viewerConnection, 16).rightJustified(16, '0'));
  return object;
}
KeyboardRouteControl parseKeyboardRouteControl(const Packet &packet)
{
  require(
      (packet.type == MessageType::KeyboardRoute || packet.type == MessageType::InputRoute) &&
          packet.payload.size() <= 1024,
      "Invalid keyboard route control packet"
  );
  const auto object = parseObject(packet);
  KeyboardRouteControl control;
  control.binding = {
      {packet.session, packet.epoch},
      number(object["generation"]),
      number(object["focusRequest"]),
      number(object["focusRevision"]),
      number(object["geometry"])
  };
  control.binding.scope = packet.type == MessageType::InputRoute ? ForwardingScope::All : ForwardingScope::Keyboard;
  control.action = value<KeyboardRouteAction>(object["action"], actions);
  control.mode = value<KeyboardRouteMode>(object["mode"], modes);
  if (control.action == KeyboardRouteAction::Acknowledge)
    control.result = value<KeyboardRouteResult>(object["result"], results);
  else if (control.action == KeyboardRouteAction::Withdraw)
    control.reason = value<KeyboardRouteReason>(object["reason"], reasons);
  if (object.contains("viewerConnection")) {
    const auto text = object["viewerConnection"].toString();
    bool ok = false;
    control.viewerConnection = text.toULongLong(&ok, 16);
    require(
        ok && control.viewerConnection && text.size() == 16 &&
            text == QString::number(control.viewerConnection, 16).rightJustified(16, '0'),
        "Invalid viewer connection incarnation"
    );
  }
  validate(control);
  // Exact schema: native key codes, flags and unnegotiated extensions can never
  // sneak through this network-only forwarding control.
  require(object == encodeKeyboardRouteControl(control), "Unexpected keyboard route control fields");
  return control;
}
} // namespace deskflow::windowshare
