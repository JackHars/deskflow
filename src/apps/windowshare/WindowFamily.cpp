/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowFamily.h"
#include <cmath>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
constexpr const char *roles[] = {"sheet", "drawer", "dialog", "menu", "popover", "tooltip"};
constexpr auto capability = "window-family-v1";
std::uint64_t id(const QJsonValue &value)
{
  const auto number = value.toDouble();
  if (!value.isDouble() || !std::isfinite(number) || number < 1 || number > 9007199254740991. ||
      std::floor(number) != number)
    throw std::runtime_error("Invalid window family identity");
  return static_cast<std::uint64_t>(number);
}
} // namespace
bool WindowFamily::sameAttachment(const WindowFamily &other) const
{
  return parent == other.parent && relation == other.relation && role == other.role;
}
bool WindowFamily::blocksParent() const
{
  return role == WindowFamilyRole::Sheet || role == WindowFamilyRole::Dialog || dismissible();
}
bool WindowFamily::acceptsInput() const
{
  return role != WindowFamilyRole::Tooltip;
}
bool WindowFamily::dismissible() const
{
  return role == WindowFamilyRole::Menu || role == WindowFamilyRole::Popover;
}
std::optional<WindowFamily> parseWindowFamily(const QJsonObject &metadata)
{
  const auto value = metadata.value("family");
  if (value.isUndefined())
    return {};
  const auto object = value.toObject();
  if (!value.isObject() || object.size() != 6 || !object.contains("parentSession") || !object.contains("parentEpoch") ||
      !object.contains("relation") || !object.value("role").isString() || !object.value("x").isDouble() ||
      !object.value("y").isDouble())
    throw std::runtime_error("Invalid window family metadata");
  WindowFamily result;
  result.parent = {id(object.value("parentSession")), id(object.value("parentEpoch"))};
  result.relation = id(object.value("relation"));
  const auto role = object.value("role").toString();
  unsigned index = 0;
  while (index < std::size(roles) && role != QLatin1String(roles[index]))
    ++index;
  if (index == std::size(roles))
    throw std::runtime_error("Unsupported window family role");
  result.role = static_cast<WindowFamilyRole>(index);
  result.offset = {object.value("x").toDouble(), object.value("y").toDouble()};
  if (!std::isfinite(result.offset.x()) || !std::isfinite(result.offset.y()) || std::abs(result.offset.x()) > 65536 ||
      std::abs(result.offset.y()) > 65536)
    throw std::runtime_error("Invalid window family placement");
  return result;
}
QJsonObject windowFamilyJson(const WindowFamily &family)
{
  const auto role = static_cast<unsigned>(family.role);
  if (role >= std::size(roles))
    throw std::runtime_error("Unsupported window family role");
  QJsonObject object{
      {"parentSession", double(family.parent.session)},
      {"parentEpoch", double(family.parent.epoch)},
      {"relation", double(family.relation)},
      {"role", QLatin1String(roles[role])},
      {"x", family.offset.x()},
      {"y", family.offset.y()}
  };
  (void)parseWindowFamily({{"family", object}});
  return object;
}
QJsonArray windowFamilyCapabilities(QJsonArray capabilities, bool enabled)
{
  if (enabled)
    capabilities.append(QLatin1String(capability));
  return capabilities;
}
bool peerSupportsWindowFamily(const QJsonObject &hello)
{
  return hello.value("capabilities").toArray().contains(QLatin1String(capability));
}
void WindowFamilies::open(Owner owner, const std::optional<WindowFamily> &family)
{
  if (!owner.session || !owner.epoch)
    throw std::runtime_error("Invalid window family owner");
  if (family)
    (void)windowFamilyJson(*family);
  if (auto found = m_nodes.find(owner.session); found != m_nodes.end()) {
    const auto &old = found->second;
    if (old.owner != owner || bool(old.family) != bool(family) || (family && !family->sameAttachment(*old.family)))
      throw std::runtime_error("Window family attachment changed");
    found->second.family = family;
    return;
  }
  if (m_nodes.size() >= 16)
    throw std::runtime_error("Too many window family members");
  if (family) {
    const auto parent = m_nodes.find(family->parent.session);
    if (parent == m_nodes.end() || parent->second.owner != family->parent || family->parent == owner ||
        family->relation <= m_relation)
      throw std::runtime_error("Missing parent or replayed window family relation");
    m_relation = family->relation;
  }
  m_nodes.emplace(owner.session, Node{owner, family});
}
std::vector<Owner> WindowFamilies::close(Owner owner)
{
  const auto found = m_nodes.find(owner.session);
  if (found == m_nodes.end() || found->second.owner != owner)
    return {};
  std::vector<Owner> children, removed;
  for (const auto &[id, node] : m_nodes)
    if (node.family && node.family->parent == owner)
      children.push_back(node.owner);
  for (const auto child : children) {
    auto descendants = close(child);
    removed.insert(removed.end(), descendants.begin(), descendants.end());
  }
  m_nodes.erase(owner.session);
  removed.push_back(owner);
  return removed;
}
bool WindowFamilies::blocked(Owner owner) const
{
  for (const auto &[id, node] : m_nodes) {
    if (!node.family || !node.family->blocksParent())
      continue;
    auto parent = node.family->parent;
    for (std::size_t depth = 0; depth < 16; ++depth) {
      if (parent == owner)
        return true;
      const auto it = m_nodes.find(parent.session);
      if (it == m_nodes.end() || it->second.owner != parent || !it->second.family)
        break;
      parent = it->second.family->parent;
    }
  }
  return false;
}
Owner WindowFamilies::root(Owner owner) const
{
  for (std::size_t depth = 0; depth < 16; ++depth) {
    const auto found = m_nodes.find(owner.session);
    if (found == m_nodes.end() || found->second.owner != owner)
      return {};
    if (!found->second.family)
      return owner;
    owner = found->second.family->parent;
  }
  return {};
}
void WindowFamilies::clear()
{
  m_nodes.clear();
  m_relation = 0;
}
} // namespace deskflow::windowshare
