/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragPreviewProtocol.h"
#include <QSet>

namespace deskflow::windowshare {
namespace {
void require(bool condition)
{
  if (!condition)
    throw std::runtime_error("Invalid window drag preview control");
}
void fields(const QJsonObject &object, const QSet<QString> &expected)
{
  require(object.size() == expected.size());
  for (auto it = object.begin(); it != object.end(); ++it)
    require(expected.contains(it.key()));
}
double number(const QJsonObject &object, const char *key, double low, double high, bool integral = false)
{
  const auto value = object.value(QLatin1String(key));
  const auto n = value.toDouble();
  require(value.isDouble() && std::isfinite(n) && n >= low && n <= high && (!integral || std::floor(n) == n));
  return n;
}
std::uint64_t id(const QJsonObject &object, const char *key)
{
  return static_cast<std::uint64_t>(number(object, key, 1, 9007199254740991., true));
}
int coordinate(const QJsonObject &object, const char *key)
{
  return static_cast<int>(number(object, key, INT32_MIN, INT32_MAX, true));
}
} // namespace
QJsonObject windowDragPreviewJson(const WindowDragPreviewCommand &value)
{
  static constexpr const char *names[] = {"move", "hold", "drop", "cancel"};
  const auto kind = static_cast<unsigned>(value.kind);
  require(kind < 4);
  QJsonObject result{
      {"kind", QLatin1String(names[kind])},
      {"drag", double(value.drag)},
      {"sequence", double(value.sequence)},
      {"geometry", double(value.geometry)},
      {"x", value.x},
      {"y", value.y},
      {"anchorX", value.anchorX},
      {"anchorY", value.anchorY}
  };
  (void)parseWindowDragPreviewCommand(result);
  return result;
}
WindowDragPreviewCommand parseWindowDragPreviewCommand(const QJsonObject &object)
{
  fields(object, {"kind", "drag", "sequence", "geometry", "x", "y", "anchorX", "anchorY"});
  WindowDragPreviewCommand result;
  const auto kind = object.value("kind");
  if (kind == QJsonValue("move"))
    result.kind = WindowDragPreviewCommand::Kind::Move;
  else if (kind == QJsonValue("hold"))
    result.kind = WindowDragPreviewCommand::Kind::Hold;
  else if (kind == QJsonValue("drop"))
    result.kind = WindowDragPreviewCommand::Kind::Drop;
  else {
    require(kind == QJsonValue("cancel"));
    result.kind = WindowDragPreviewCommand::Kind::Cancel;
  }
  result.drag = id(object, "drag");
  result.sequence = id(object, "sequence");
  result.geometry = id(object, "geometry");
  result.x = coordinate(object, "x");
  result.y = coordinate(object, "y");
  result.anchorX = number(object, "anchorX", 0, 1);
  result.anchorY = number(object, "anchorY", 0, 1);
  return result;
}
QJsonObject windowDragPreviewJson(const WindowDragPreviewReply &value)
{
  QJsonObject result{{"kind", "reply"}, {"command", windowDragPreviewJson(value.command)}, {"ok", value.ok}};
  if (value.ok) {
    result.insert(
        "frame", QJsonObject{
                     {"x", value.frame.x()},
                     {"y", value.frame.y()},
                     {"width", value.frame.width()},
                     {"height", value.frame.height()}
                 }
    );
    result.insert("paint", double(value.paint));
    result.insert("placement", double(value.placement));
  }
  (void)parseWindowDragPreviewReply(result);
  return result;
}
WindowDragPreviewReply parseWindowDragPreviewReply(const QJsonObject &object)
{
  require(
      object.value("kind") == QJsonValue("reply") && object.value("ok").isBool() && object.value("command").isObject()
  );
  WindowDragPreviewReply result;
  result.command = parseWindowDragPreviewCommand(object.value("command").toObject());
  result.ok = object.value("ok").toBool();
  fields(
      object, result.ok ? QSet<QString>{"kind", "command", "ok", "frame", "paint", "placement"}
                        : QSet<QString>{"kind", "command", "ok"}
  );
  if (result.ok) {
    require(object.value("frame").isObject());
    const auto frame = object.value("frame").toObject();
    fields(frame, {"x", "y", "width", "height"});
    const auto x = coordinate(frame, "x"), y = coordinate(frame, "y");
    const auto w = static_cast<int>(number(frame, "width", 1, INT32_MAX, true));
    const auto h = static_cast<int>(number(frame, "height", 1, INT32_MAX, true));
    require(std::int64_t(x) + w - 1 <= INT32_MAX && std::int64_t(y) + h - 1 <= INT32_MAX);
    result.frame = QRect(x, y, w, h);
    result.paint = id(object, "paint");
    result.placement = static_cast<std::uint32_t>(number(object, "placement", 1, INT32_MAX, true));
  }
  return result;
}
} // namespace deskflow::windowshare
