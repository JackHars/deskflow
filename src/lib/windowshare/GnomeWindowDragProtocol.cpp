/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeWindowDragProtocol.h"
#include <QSet>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
void require(bool value)
{
  if (!value)
    throw std::runtime_error("Malformed native window drag message");
}
void fields(const QJsonObject &object, QSet<QString> expected, const char *kind)
{
  expected.unite({"protocol", "version", "kind"});
  require(object.size() == expected.size());
  for (auto it = object.begin(); it != object.end(); ++it)
    require(expected.contains(it.key()));
  require(
      isGnomeWindowDrag(object) && object.value("version") == QJsonValue(1) &&
      object.value("kind") == QJsonValue(QLatin1String(kind))
  );
}
QJsonObject envelope(const char *kind)
{
  return {{"protocol", "window-drag"}, {"version", 1}, {"kind", QLatin1String(kind)}};
}
std::uint64_t sequence(const QJsonObject &object, const char *key)
{
  const auto value = object.value(QLatin1String(key));
  require(value.isString());
  const auto bytes = value.toString().toLatin1();
  require(!bytes.isEmpty() && bytes.size() <= 16 && bytes[0] >= '1' && bytes[0] <= '9');
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(bytes.constData(), bytes.constData() + bytes.size(), result);
  require(
      parsed.ec == std::errc{} && parsed.ptr == bytes.constData() + bytes.size() && result &&
      result <= ((std::uint64_t{1} << 53) - 1)
  );
  return result;
}
int integer(const QJsonObject &object, const char *key, bool positive = false)
{
  const auto value = object.value(QLatin1String(key));
  const auto number = value.toDouble();
  require(
      value.isDouble() && std::isfinite(number) && std::floor(number) == number &&
      number >= (positive ? 1 : std::numeric_limits<int>::min()) && number <= std::numeric_limits<int>::max()
  );
  return static_cast<int>(number);
}
QString association(const QJsonObject &object)
{
  const auto text = object.value("association").toString();
  const auto prefix = QStringLiteral("org.deskflow.window.");
  require(
      text.startsWith(prefix) && text.size() == prefix.size() + 64 &&
      std::all_of(text.begin() + prefix.size(), text.end(), [](QChar c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })
  );
  return text;
}
QJsonObject frameJson(const QRect &frame)
{
  return {{"x", frame.x()}, {"y", frame.y()}, {"width", frame.width()}, {"height", frame.height()}};
}
QRect frame(const QJsonObject &object)
{
  require(object.value("frame").isObject());
  const auto value = object.value("frame").toObject();
  require(
      value.size() == 4 && value.contains("x") && value.contains("y") && value.contains("width") &&
      value.contains("height")
  );
  const auto x = integer(value, "x"), y = integer(value, "y"), w = integer(value, "width", true),
             h = integer(value, "height", true);
  require(
      std::int64_t(x) + w - 1 <= std::numeric_limits<int>::max() &&
      std::int64_t(y) + h - 1 <= std::numeric_limits<int>::max()
  );
  return {x, y, w, h};
}
} // namespace
bool isGnomeWindowDrag(const QJsonObject &object)
{
  return object.value("protocol") == QJsonValue("window-drag");
}
QString gnomeWindowDragActionName(GnomeWindowDragAction action)
{
  switch (action) {
  case GnomeWindowDragAction::Place:
    return QStringLiteral("place");
  case GnomeWindowDragAction::Hold:
    return QStringLiteral("hold");
  case GnomeWindowDragAction::Verify:
    return QStringLiteral("verify");
  case GnomeWindowDragAction::Finish:
    return QStringLiteral("finish");
  case GnomeWindowDragAction::Cancel:
    return QStringLiteral("cancel");
  case GnomeWindowDragAction::Watch:
    return QStringLiteral("watch");
  case GnomeWindowDragAction::Unwatch:
    return QStringLiteral("unwatch");
  }
  throw std::runtime_error("Unknown native window drag action");
}
QJsonObject gnomeWindowDragJson(const GnomeWindowDragRequest &value)
{
  auto result = envelope("request");
  result.insert("request", QString::number(value.request));
  result.insert("association", value.association);
  result.insert("drag", QString::number(value.drag));
  result.insert("sequence", QString::number(value.sequence));
  result.insert("action", gnomeWindowDragActionName(value.action));
  result.insert("x", value.x);
  result.insert("y", value.y);
  (void)parseGnomeWindowDragRequest(result);
  return result;
}
GnomeWindowDragRequest parseGnomeWindowDragRequest(const QJsonObject &object)
{
  fields(object, {"request", "association", "drag", "sequence", "action", "x", "y"}, "request");
  GnomeWindowDragRequest result;
  result.request = sequence(object, "request");
  result.drag = sequence(object, "drag");
  result.sequence = sequence(object, "sequence");
  result.association = association(object);
  result.x = integer(object, "x");
  result.y = integer(object, "y");
  bool found = false;
  for (auto candidate :
       {GnomeWindowDragAction::Place, GnomeWindowDragAction::Hold, GnomeWindowDragAction::Verify,
        GnomeWindowDragAction::Finish, GnomeWindowDragAction::Cancel, GnomeWindowDragAction::Watch,
        GnomeWindowDragAction::Unwatch}) {
    if (object.value("action") == QJsonValue(gnomeWindowDragActionName(candidate))) {
      result.action = candidate;
      found = true;
    }
  }
  require(found && (result.action == GnomeWindowDragAction::Place || (!result.x && !result.y)));
  return result;
}
QJsonObject gnomeWindowDragJson(const GnomeWindowDragReply &value)
{
  auto result = envelope("reply");
  result.insert("binding", gnomeWindowDragJson(value.binding));
  result.insert("ok", value.ok);
  if (value.ok && value.binding.action != GnomeWindowDragAction::Watch &&
      value.binding.action != GnomeWindowDragAction::Unwatch) {
    result.insert("frame", frameJson(value.frame));
    result.insert("revision", double(value.revision));
  }
  (void)parseGnomeWindowDragReply(result);
  return result;
}
GnomeWindowDragReply parseGnomeWindowDragReply(const QJsonObject &object)
{
  require(object.value("ok").isBool());
  const bool ok = object.value("ok").toBool();
  require(object.value("binding").isObject());
  GnomeWindowDragReply result;
  result.binding = parseGnomeWindowDragRequest(object.value("binding").toObject());
  const bool placed = ok && result.binding.action != GnomeWindowDragAction::Watch &&
                      result.binding.action != GnomeWindowDragAction::Unwatch;
  fields(
      object, placed ? QSet<QString>{"binding", "ok", "frame", "revision"} : QSet<QString>{"binding", "ok"}, "reply"
  );
  result.ok = ok;
  if (placed) {
    result.frame = frame(object);
    const auto revision = integer(object, "revision");
    require(revision >= 0);
    result.revision = static_cast<std::uint32_t>(revision);
    const auto action = result.binding.action;
    require(
        (action != GnomeWindowDragAction::Place && action != GnomeWindowDragAction::Hold &&
         action != GnomeWindowDragAction::Verify) ||
        revision > 0
    );
  }
  return result;
}
QJsonObject gnomeWindowDragJson(const GnomeWindowDragEvent &value)
{
  auto result = envelope("event");
  static constexpr const char *phases[] = {"begin", "motion", "end", "cancel"};
  const auto index = static_cast<unsigned>(value.phase);
  require(index < 4);
  result.insert("association", value.association);
  result.insert("grab", QString::number(value.grab));
  result.insert("sequence", QString::number(value.sequence));
  result.insert("phase", QLatin1String(phases[index]));
  result.insert("x", value.x);
  result.insert("y", value.y);
  result.insert("frame", frameJson(value.frame));
  result.insert("buttonReleased", value.buttonReleased);
  (void)parseGnomeWindowDragEvent(result);
  return result;
}
GnomeWindowDragEvent parseGnomeWindowDragEvent(const QJsonObject &object)
{
  fields(object, {"association", "grab", "sequence", "phase", "x", "y", "frame", "buttonReleased"}, "event");
  GnomeWindowDragEvent result;
  result.association = association(object);
  result.grab = sequence(object, "grab");
  result.sequence = sequence(object, "sequence");
  result.x = integer(object, "x");
  result.y = integer(object, "y");
  result.frame = frame(object);
  require(object.value("buttonReleased").isBool());
  result.buttonReleased = object.value("buttonReleased").toBool();
  const auto phase = object.value("phase");
  if (phase == QJsonValue("begin"))
    result.phase = GnomeWindowDragEvent::Phase::Begin;
  else if (phase == QJsonValue("motion"))
    result.phase = GnomeWindowDragEvent::Phase::Motion;
  else if (phase == QJsonValue("end"))
    result.phase = GnomeWindowDragEvent::Phase::End;
  else
    require(phase == QJsonValue("cancel"));
  require(result.buttonReleased == (result.phase == GnomeWindowDragEvent::Phase::End));
  return result;
}
} // namespace deskflow::windowshare
