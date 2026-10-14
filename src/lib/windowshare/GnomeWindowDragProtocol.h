/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <cstdint>

namespace deskflow::windowshare {
// Multiplexed on the inherited, authenticated viewport socket. This protocol
// has its own request counter and closed schema. It confers no input authority.
enum class GnomeWindowDragAction
{
  Place,
  Hold,
  Verify,
  Finish,
  Cancel,
  Watch,
  Unwatch
};
struct GnomeWindowDragRequest
{
  std::uint64_t request = 0, drag = 0, sequence = 0;
  QString association;
  GnomeWindowDragAction action = GnomeWindowDragAction::Place;
  int x = 0, y = 0;
  bool operator==(const GnomeWindowDragRequest &) const = default;
};
struct GnomeWindowDragReply
{
  GnomeWindowDragRequest binding;
  bool ok = false;
  QRect frame;
  std::uint32_t revision = 0;
};
struct GnomeWindowDragEvent
{
  enum class Phase
  {
    Begin,
    Motion,
    End,
    Cancel
  };
  QString association;
  std::uint64_t grab = 0, sequence = 0;
  Phase phase = Phase::Cancel;
  int x = 0, y = 0;
  QRect frame;
  bool buttonReleased = false;
};
bool isGnomeWindowDrag(const QJsonObject &object);
QString gnomeWindowDragActionName(GnomeWindowDragAction action);
QJsonObject gnomeWindowDragJson(const GnomeWindowDragRequest &);
QJsonObject gnomeWindowDragJson(const GnomeWindowDragReply &);
QJsonObject gnomeWindowDragJson(const GnomeWindowDragEvent &);
GnomeWindowDragRequest parseGnomeWindowDragRequest(const QJsonObject &);
GnomeWindowDragReply parseGnomeWindowDragReply(const QJsonObject &);
GnomeWindowDragEvent parseGnomeWindowDragEvent(const QJsonObject &);
} // namespace deskflow::windowshare
