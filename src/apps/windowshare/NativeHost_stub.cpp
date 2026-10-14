/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include <stdexcept>
namespace deskflow::windowshare {
struct NativeHost::Impl
{
};
NativeHost::NativeHost(QString, QObject *parent) : QObject(parent)
{
  throw std::runtime_error("Window hosting requires macOS 13 or later; use --connect on Ubuntu");
}
NativeHost::~NativeHost() = default;
QJsonObject NativeHost::permissions()
{
  return {{"supported", false}, {"screenCapture", false}, {"accessibility", false}};
}
void NativeHost::requestPermissions()
{
}
bool NativeHost::recover()
{
  return false;
}
void NativeHost::setHoldingDisplayEnabled(bool)
{
}
bool NativeHost::setCaptureFrameRate(unsigned)
{
  return false;
}
unsigned NativeHost::captureFrameRate() const
{
  return 0;
}
void NativeHost::refresh()
{
}
void NativeHost::exportWindow(std::uint32_t)
{
}
void NativeHost::observeWindowPointer(const LocalWindowPointer &)
{
}
std::optional<Owner> NativeHost::prepareWindowDrag(const WindowDragBinding &)
{
  return std::nullopt;
}
bool NativeHost::releaseWindowDrag(const WindowDragBinding &, const WindowDragRelease &)
{
  return false;
}
std::optional<Rect> NativeHost::windowReturnFrame(Owner, std::uint64_t, Point, Point) const
{
  return {};
}
bool NativeHost::windowReturnPreviewCurrent(std::uintptr_t, Rect)
{
  return false;
}
bool NativeHost::returnWindowDrag(Owner, std::uint64_t, Rect)
{
  return false;
}
void NativeHost::cancelWindowDrag(const WindowDragBinding &)
{
}
void NativeHost::ready(std::uint64_t, std::uint64_t, std::uint64_t)
{
}
void NativeHost::input(std::uint64_t, std::uint64_t, const QJsonObject &)
{
}
void NativeHost::resizeWindow(std::uint64_t, std::uint64_t, int, int, std::uint64_t)
{
}
void NativeHost::requestKeyframe(std::uint64_t, std::uint64_t)
{
}
void NativeHost::restore(std::uint64_t)
{
}
void NativeHost::restoreAll()
{
}
void NativeHost::releaseAll()
{
}
std::optional<ClipboardFocusObservation> NativeHost::clipboardFocus() const
{
  return {};
}
void NativeHost::setClipboardTextMonitoring(bool)
{
}
std::optional<KeyboardRouteBinding>
NativeHost::keyboardRouteCandidate(std::uint64_t, std::uint64_t, std::uint64_t, ForwardingScope) const
{
  return std::nullopt;
}
bool NativeHost::quiesceKeyboard(const KeyboardRouteBinding &)
{
  return false;
}
bool NativeHost::keyboardRouteQuiesced(const KeyboardRouteBinding &) const
{
  return false;
}
bool NativeHost::resumeKeyboard(const KeyboardRouteBinding &)
{
  return false;
}
bool NativeHost::establishOriginalInput(const KeyboardRouteBinding &, std::uint64_t)
{
  return false;
}
bool NativeHost::originalInputCurrent(const KeyboardRouteBinding &, std::uint64_t) const
{
  return false;
}
bool NativeHost::deliverOriginalInput(
    const KeyboardRouteBinding &, const OriginalInputRecord &, const LocalInputMappedPoint &
)
{
  return false;
}
void NativeHost::revokeOriginalInput(const KeyboardRouteBinding &)
{
}
} // namespace deskflow::windowshare
