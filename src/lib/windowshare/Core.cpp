/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Core.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace deskflow::windowshare {
namespace {
bool finite(double v)
{
  return std::isfinite(v);
}
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void put(std::vector<std::uint8_t> &out, std::uint64_t n, unsigned bytes)
{
  for (unsigned i = bytes; i > 0; --i)
    out.push_back(static_cast<std::uint8_t>(n >> (8 * (i - 1))));
}
std::uint64_t get(std::span<const std::uint8_t> in, unsigned offset, unsigned bytes)
{
  std::uint64_t n = 0;
  for (unsigned i = 0; i < bytes; ++i)
    n = (n << 8) | in[offset + i];
  return n;
}
bool validType(std::uint64_t type)
{
  return type >= static_cast<unsigned>(MessageType::Hello) && type <= static_cast<unsigned>(MessageType::InputRoute);
}
std::uint32_t limit(MessageType type)
{
  return type == MessageType::Frame ? maxPayload : maxControlPayload;
}
} // namespace
bool Rect::valid() const
{
  return finite(x) && finite(y) && finite(width) && finite(height) && std::abs(x) <= 1000000 &&
         std::abs(y) <= 1000000 && width >= 1 && height >= 1 && width <= 16384 && height <= 16384;
}
bool Rect::contains(Point p) const
{
  return valid() && finite(p.x) && finite(p.y) && p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
}
std::optional<Point> mapPoint(Point p, Rect viewport, Rect source, bool clampDrag)
{
  if (!viewport.valid() || !source.valid() || !finite(p.x) || !finite(p.y))
    return std::nullopt;
  if (!clampDrag && !viewport.contains(p))
    return std::nullopt;
  const double nx = std::clamp((p.x - viewport.x) / viewport.width, 0.0, 1.0);
  const double ny = std::clamp((p.y - viewport.y) / viewport.height, 0.0, 1.0);
  return Point{
      std::min(source.x + nx * source.width, std::nextafter(source.x + source.width, source.x)),
      std::min(source.y + ny * source.height, std::nextafter(source.y + source.height, source.y))
  };
}
Rect fitInside(Rect original, Rect workArea)
{
  require(original.valid() && workArea.valid(), "invalid recovery geometry");
  original.width = std::min(original.width, workArea.width);
  original.height = std::min(original.height, workArea.height);
  original.x = std::clamp(original.x, workArea.x, workArea.x + workArea.width - original.width);
  original.y = std::clamp(original.y, workArea.y, workArea.y + workArea.height - original.height);
  return original;
}
std::optional<Rect> recoveryRect(Rect original, std::span<const Rect> physicalDisplays)
{
  if (!original.valid())
    return std::nullopt;
  std::optional<Rect> chosen;
  double bestOverlap = -1, bestGap = std::numeric_limits<double>::infinity();
  double bestCenterDistance = std::numeric_limits<double>::infinity();
  for (const auto &display : physicalDisplays) {
    if (!display.valid())
      continue;
    const double left = std::max(original.x, display.x);
    const double top = std::max(original.y, display.y);
    const double right = std::min(original.x + original.width, display.x + display.width);
    const double bottom = std::min(original.y + original.height, display.y + display.height);
    const double overlap = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double dx = std::max({0.0, original.x - (display.x + display.width),
                                display.x - (original.x + original.width)});
    const double dy = std::max({0.0, original.y - (display.y + display.height),
                                display.y - (original.y + original.height)});
    const double gap = dx * dx + dy * dy;
    const double centerX = (original.x + original.width / 2) - (display.x + display.width / 2);
    const double centerY = (original.y + original.height / 2) - (display.y + display.height / 2);
    const double centerDistance = centerX * centerX + centerY * centerY;
    // Full ties are independent of display enumeration order. This matters
    // when a hotplug reverses CGGetActiveDisplayList's ordering.
    if (!chosen || overlap > bestOverlap ||
        (overlap == bestOverlap &&
         (gap < bestGap ||
          (gap == bestGap &&
           (centerDistance < bestCenterDistance ||
            (centerDistance == bestCenterDistance &&
             std::tie(display.x, display.y, display.width, display.height) <
                 std::tie(chosen->x, chosen->y, chosen->width, chosen->height))))))) {
      chosen = display;
      bestOverlap = overlap;
      bestGap = gap;
      bestCenterDistance = centerDistance;
    }
  }
  return chosen ? std::optional<Rect>(fitInside(original, *chosen)) : std::nullopt;
}
std::vector<std::uint8_t> encode(const Packet &p)
{
  require(validType(static_cast<unsigned>(p.type)), "unknown packet type");
  require(p.payload.size() <= limit(p.type), "packet too large");
  require(p.sequence != 0, "zero packet sequence");
  std::vector<std::uint8_t> out;
  out.reserve(headerSize + p.payload.size());
  out.insert(out.end(), {'D', 'S', 'W', '1'});
  put(out, p.payload.size(), 4);
  put(out, static_cast<unsigned>(p.type), 2);
  put(out, 0, 2); // reserved flags
  put(out, p.session, 8);
  put(out, p.epoch, 8);
  put(out, p.sequence, 8);
  put(out, 0, 4); // reserved
  out.insert(out.end(), p.payload.begin(), p.payload.end());
  return out;
}
void Decoder::feed(std::span<const std::uint8_t> bytes, const std::function<void(Packet)> &receive)
{
  require(!m_failed, "decoder already failed");
  try {
    while (!bytes.empty()) {
      const auto count = std::min(bytes.size(), m_expected - m_buffer.size());
      m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(count));
      bytes = bytes.subspan(count);
      if (m_buffer.size() != m_expected)
        continue;
      if (m_expected == headerSize) {
        require(m_buffer[0] == 'D' && m_buffer[1] == 'S' && m_buffer[2] == 'W' && m_buffer[3] == '1', "bad magic");
        const auto type = get(m_buffer, 8, 2);
        require(validType(type), "unknown message type");
        require(get(m_buffer, 10, 2) == 0 && get(m_buffer, 36, 4) == 0, "unsupported flags");
        const auto length = get(m_buffer, 4, 4);
        require(length <= limit(static_cast<MessageType>(type)), "oversized packet");
        require(get(m_buffer, 28, 8) != 0, "zero sequence");
        m_expected = headerSize + static_cast<std::size_t>(length);
        if (m_buffer.size() != m_expected)
          continue;
      }
      Packet p;
      p.type = static_cast<MessageType>(get(m_buffer, 8, 2));
      p.session = get(m_buffer, 12, 8);
      p.epoch = get(m_buffer, 20, 8);
      p.sequence = get(m_buffer, 28, 8);
      p.payload.assign(m_buffer.begin() + headerSize, m_buffer.end());
      m_buffer.clear();
      m_expected = headerSize;
      receive(std::move(p));
    }
  } catch (...) {
    m_failed = true;
    m_buffer.clear();
    throw;
  }
}
void Session::prepare(std::uint64_t nextEpoch, Rect saved, Rect destination)
{
  require(phase == Phase::Local, "window is not local");
  require(id != 0 && nextEpoch > epoch && saved.valid() && destination.valid(), "invalid export");
  require(journaled, "persist recovery journal before export");
  epoch = nextEpoch;
  original = saved;
  source = destination;
  firstFrame = false;
  phase = Phase::Preparing;
}
void Session::frameReady(std::uint64_t receivedEpoch)
{
  require(receivedEpoch == epoch && phase == Phase::Preparing, "stale first frame");
  firstFrame = true;
}
void Session::commit(std::uint64_t receivedEpoch)
{
  require(receivedEpoch == epoch && phase == Phase::Preparing && firstFrame && journaled, "export not ready");
  phase = Phase::Remote;
}
void Session::beginReturn()
{
  require(phase == Phase::Remote || phase == Phase::Preparing || phase == Phase::Suspended, "cannot return window");
  phase = Phase::Returning;
}
void Session::restored()
{
  require(phase == Phase::Returning, "restore was not requested");
  phase = Phase::Local;
  journaled = false;
  firstFrame = false;
  ++geometry;
}
void Session::suspend()
{
  if (phase == Phase::Remote || phase == Phase::Preparing)
    phase = Phase::Suspended;
}
bool Session::acceptsInput(std::uint64_t receivedEpoch, std::uint64_t revision) const
{
  return phase == Phase::Remote && epoch == receivedEpoch && geometry == revision;
}
bool InputLedger::down(bool keyboard, std::uint32_t code, Owner owner)
{
  // Qt uses Unicode for printable keys and 0x01000000+ for special keys.
  // Keep buttons in their own small namespace; never truncate a key to 16 bits.
  if (owner.session == 0 || owner.epoch == 0 || (keyboard ? code > 0x01ffffffU : code > 31U) || m_pressed.size() >= 512)
    return false;
  return m_pressed.emplace(std::make_pair(keyboard, code), owner).second;
}
std::optional<Owner> InputLedger::up(bool keyboard, std::uint32_t code, std::optional<Owner> requester)
{
  const auto it = m_pressed.find({keyboard, code});
  if (it == m_pressed.end() || (requester && it->second != *requester))
    return std::nullopt;
  const auto owner = it->second;
  m_pressed.erase(it);
  return owner;
}
std::optional<Owner> InputLedger::ownerOf(bool keyboard, std::uint32_t code) const
{
  const auto it = m_pressed.find({keyboard, code});
  return it == m_pressed.end() ? std::nullopt : std::optional(it->second);
}
std::vector<Press> InputLedger::release(std::optional<Owner> owner)
{
  std::vector<Press> releases;
  for (auto it = m_pressed.begin(); it != m_pressed.end();) {
    if (!owner || it->second == *owner) {
      releases.push_back({it->first.first, it->first.second, it->second});
      it = m_pressed.erase(it);
    } else
      ++it;
  }
  return releases;
}
std::uint64_t FocusLease::request(Owner owner)
{
  require(owner.session != 0 && owner.epoch != 0, "invalid focus owner");
  require(m_revision != std::numeric_limits<std::uint64_t>::max(), "focus revision exhausted");
  m_owner = owner;
  m_ready = false;
  return ++m_revision;
}
bool FocusLease::acknowledge(Owner owner, std::uint64_t revision)
{
  if (owner != m_owner || revision != m_revision || revision == 0)
    return false;
  m_ready = true;
  return true;
}
bool FocusLease::permits(Owner owner, std::uint64_t revision) const
{
  return m_ready && owner == m_owner && revision == m_revision;
}
void FocusLease::revoke()
{
  m_ready = false;
  m_owner = {};
}
} // namespace deskflow::windowshare
