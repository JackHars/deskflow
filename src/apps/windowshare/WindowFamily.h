/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <map>
#include <optional>

namespace deskflow::windowshare {
enum class WindowFamilyRole
{
  Sheet,
  Drawer,
  Dialog,
  Menu,
  Popover,
  Tooltip
};
struct WindowFamily
{
  Owner parent;
  std::uint64_t relation = 0;
  WindowFamilyRole role = WindowFamilyRole::Sheet;
  QPointF offset;
  bool operator==(const WindowFamily &) const = default;
  bool sameAttachment(const WindowFamily &other) const;
  bool blocksParent() const;
  bool acceptsInput() const;
  bool dismissible() const;
};
// Host-created session relationships, never native window IDs from the peer.
// A missing field denotes an independent window; malformed relationships throw.
std::optional<WindowFamily> parseWindowFamily(const QJsonObject &metadata);
QJsonObject windowFamilyJson(const WindowFamily &family);
QJsonArray windowFamilyCapabilities(QJsonArray capabilities, bool enabled = true);
bool peerSupportsWindowFamily(const QJsonObject &hello);

// Connection-scoped graph. A child can only attach to a live, exact parent;
// attachments never change and relation numbers cannot be replayed after close.
class WindowFamilies
{
public:
  void open(Owner owner, const std::optional<WindowFamily> &family);
  std::vector<Owner> close(Owner owner);
  bool blocked(Owner owner) const;
  Owner root(Owner owner) const;
  void clear();

private:
  struct Node
  {
    Owner owner;
    std::optional<WindowFamily> family;
  };
  std::map<std::uint64_t, Node> m_nodes;
  std::uint64_t m_relation = 0;
};
} // namespace deskflow::windowshare
