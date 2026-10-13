/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "PlatformMarkers.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace deskflow::windowshare {
inline constexpr std::uint32_t protocolVersion = 1;
inline constexpr std::size_t headerSize = 40;
inline constexpr std::uint32_t maxPayload = 16 * 1024 * 1024;
inline constexpr std::uint32_t maxControlPayload = 64 * 1024;
struct Point
{
  double x = 0, y = 0;
};
struct Rect
{
  double x = 0, y = 0, width = 0, height = 0;
  bool valid() const;
  bool contains(Point p) const;
};
// Viewer-local coordinates -> source-window logical coordinates. Black bars do
// not accept input. Geometry revisions are checked by the caller before mapping.
std::optional<Point> mapPoint(Point p, Rect viewport, Rect source, bool clampDrag = false);
Rect fitInside(Rect original, Rect workArea);
// Choose the physical output to restore to, even if it was unplugged while
// the source was hidden. Prefer overlap with the *saved* frame, then the
// nearest output. Never assume the first enumerated output is the primary.
// Callers must exclude Deskflow's private holding display from this list.
// A missing/invalid output is a refusal to move a source window.
std::optional<Rect> recoveryRect(Rect original, std::span<const Rect> physicalDisplays);
// Local macOS WindowServer display identity plus logical and pixel geometry.
// No remote peer may supply or authorize this state. Distinguish a new output
// using the same frame, reordered enumeration, rotation, and Retina mode change.
struct PhysicalOutputSignature
{
  std::uint32_t displayId = 0;
  Rect bounds;
  std::uint32_t pixelWidth = 0, pixelHeight = 0;
  int rotationDegrees = 0;
};
// Empty, oversized, malformed or duplicate snapshots are never equality
// evidence. No allocations, throws or implicit acceptance when both fail.
bool samePhysicalOutputLayout(
    std::span<const PhysicalOutputSignature> expected, std::span<const PhysicalOutputSignature> current
) noexcept;
// Returns true only for one self-contained H.264 Annex B access unit carrying
// SPS, PPS and an IDR in that order. This is a media decoder reset predicate,
// NOT proof of stream/peer trust (provided by the authenticated envelope).
// A partial/malformed NAL stream can never restart a decoder after a drop.
bool decoderSyncAccessUnit(std::span<const std::uint8_t> bytes) noexcept;

enum class MessageType : std::uint16_t
{
  Hello = 1,
  Accepted,
  Catalog,
  Export,
  Opened,
  Frame,
  Input,
  Resize,
  Return,
  Closed,
  Ping,
  Pong,
  Error,
  Focus,
  ReleaseAll,
  Ready,
  KeyboardRoute,
  InputRoute,
  ClipboardText
};
struct Packet
{
  MessageType type = MessageType::Error;
  std::uint64_t session = 0, epoch = 0, sequence = 0;
  std::vector<std::uint8_t> payload;
};
std::vector<std::uint8_t> encode(const Packet &packet);
// Fail closed on malformed/oversized headers. Buffer growth is bounded before
// allocation; no native structs or platform endian assumptions cross the wire.
class Decoder
{
public:
  void feed(std::span<const std::uint8_t> bytes, const std::function<void(Packet)> &receive);
  bool failed() const
  {
    return m_failed;
  }
  std::size_t buffered() const
  {
    return m_buffer.size();
  }

private:
  std::vector<std::uint8_t> m_buffer;
  std::size_t m_expected = headerSize;
  bool m_failed = false;
};

class SequenceGate
{
public:
  bool accept(std::uint64_t sequence)
  {
    if (sequence == 0 || sequence <= m_last)
      return false;
    m_last = sequence;
    return true;
  }

private:
  std::uint64_t m_last = 0;
};

enum class Phase
{
  Local,
  Preparing,
  Remote,
  Returning,
  Suspended,
  Closed
};
struct Session
{
  std::uint64_t id = 0, epoch = 0, geometry = 1;
  Phase phase = Phase::Local;
  Rect original, source;
  bool journaled = false, firstFrame = false;
  void prepare(std::uint64_t nextEpoch, Rect saved, Rect destination);
  void frameReady(std::uint64_t receivedEpoch);
  void commit(std::uint64_t receivedEpoch);
  void beginReturn();
  void restored();
  void suspend();
  bool acceptsInput(std::uint64_t receivedEpoch, std::uint64_t revision) const;
};

struct Owner
{
  std::uint64_t session = 0, epoch = 0;
  bool operator==(const Owner &) const = default;
};
struct Press
{
  bool keyboard = false;
  std::uint32_t code = 0;
  Owner owner;
};
// The owner of a down event keeps its up event even across focus changes.
// OS injection is performed only after this ledger accepts the event.
class InputLedger
{
public:
  bool down(bool keyboard, std::uint32_t code, Owner owner);
  std::optional<Owner> up(bool keyboard, std::uint32_t code, std::optional<Owner> requester = std::nullopt);
  std::optional<Owner> ownerOf(bool keyboard, std::uint32_t code) const;
  std::vector<Press> release(std::optional<Owner> owner = std::nullopt);
  std::size_t size() const
  {
    return m_pressed.size();
  }

private:
  std::map<std::pair<bool, std::uint32_t>, Owner> m_pressed;
};

class FocusLease
{
public:
  std::uint64_t request(Owner owner);
  bool acknowledge(Owner owner, std::uint64_t revision);
  bool permits(Owner owner, std::uint64_t revision) const;
  void revoke();

private:
  Owner m_owner;
  std::uint64_t m_revision = 0;
  bool m_ready = false;
};
} // namespace deskflow::windowshare
