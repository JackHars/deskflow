/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragMove.h"
#include <cmath>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
std::uint64_t id(const QJsonObject &object, const char *key)
{
  const auto value = object.value(QLatin1String(key));
  const double n = value.toDouble();
  if (!value.isDouble() || !std::isfinite(n) || n < 1 || n > 9007199254740991. || std::floor(n) != n)
    throw std::runtime_error("Invalid native move identity");
  return static_cast<std::uint64_t>(n);
}
} // namespace
QJsonObject windowDragMoveJson(const WindowDragMove &value)
{
  QJsonObject result{
      {"kind", "native-move"},
      {"geometry", double(value.geometry)},
      {"surface", double(value.surface)},
      {"paint", double(value.paint)},
      {"native", gnomeWindowDragJson(value.native)}
  };
  (void)parseWindowDragMove(result);
  return result;
}
WindowDragMove parseWindowDragMove(const QJsonObject &object)
{
  if (object.size() != 5 || object.value("kind") != QJsonValue("native-move") || !object.value("native").isObject())
    throw std::runtime_error("Invalid native move envelope");
  return {
      id(object, "geometry"), id(object, "surface"), id(object, "paint"),
      parseGnomeWindowDragEvent(object.value("native").toObject())
  };
}
bool WindowDragMoveGate::accept(const WindowDragMove &value)
{
  try {
    (void)windowDragMoveJson(value);
  } catch (...) {
    return false;
  }
  const auto &event = value.native;
  if (event.phase == GnomeWindowDragEvent::Phase::Begin) {
    if (m_active || event.grab <= m_lastGrab)
      return false;
    m_lastGrab = event.grab;
    m_active = value;
    return true;
  }
  if (!m_active || value.geometry != m_active->geometry || value.surface != m_active->surface ||
      event.association != m_active->native.association || event.grab != m_active->native.grab ||
      event.frame.size() != m_active->native.frame.size() || value.paint < m_active->paint ||
      event.sequence < m_active->native.sequence ||
      (event.sequence == m_active->native.sequence && event.phase != GnomeWindowDragEvent::Phase::Cancel))
    return false;
  m_active = value;
  if (event.phase != GnomeWindowDragEvent::Phase::Motion)
    m_active.reset();
  return true;
}
} // namespace deskflow::windowshare
