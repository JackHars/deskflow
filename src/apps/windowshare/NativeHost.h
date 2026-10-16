/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "ClipboardText.h"
#include "Core.h"
#include "KeyboardRoute.h"
#include "LocalInputMapping.h"
#include "LocalWindowPointer.h"
#include "OriginalInput.h"
#include "WindowDragTransaction.h"
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
namespace deskflow::windowshare {
struct Candidate
{
  std::uint32_t id = 0;
  QString title;
};
class NativeHost final : public QObject
{
public:
  explicit NativeHost(QString journalPath, QObject *parent = nullptr);
  ~NativeHost() override;
  // Probing never prompts. Only an explicit --request-permissions invocation
  // should call requestPermissions(), which presents the normal macOS dialogs.
  static QJsonObject permissions();
  static void requestPermissions();
  bool recover();
  void setHoldingDisplayEnabled(bool enabled);
  // Negotiated per connection before export. Legacy peers retain local return
  // for associated children instead of receiving unknown session metadata.
  void setWindowFamiliesEnabled(bool enabled);
  // Startup configuration only; rejects unsupported rates or live exports.
  bool setCaptureFrameRate(unsigned framesPerSecond);
  unsigned captureFrameRate() const;
  void refresh();
  // Workspace sleep/deactivation fences new work immediately and reopens only
  // after wake/activation plus exact durable restoration. Emergency return is
  // terminal for this host instance; explicit new host startup re-enables it.
  void exportWindow(std::uint32_t windowId);
  void observeWindowPointer(const LocalWindowPointer &pointer);
  // Private local coordinator only. The coordinator retains the joined real
  // press, titlebar eligibility and configured edge. These calls never post a
  // button event. Ready from the network can only acknowledge the preview;
  // the exact local release is additionally required before source mutation.
  // observeWindowPointer retains an exact real up without committing. Drop
  // additionally requires the coordinator's explicit release and final paint.
  std::optional<Owner> prepareWindowDrag(const WindowDragBinding &binding);
  bool releaseWindowDrag(const WindowDragBinding &binding, const WindowDragRelease &release);
  void cancelWindowDrag(const WindowDragBinding &binding);
  // Return preparation is read-only. The exact retained remote source and
  // unchanged physical layout determine the candidate frame. The preview
  // witness accepts only a live content view owned by this Cocoa process.
  std::optional<Rect> windowReturnFrame(Owner, std::uint64_t geometry, Point pointer, Point anchor) const;
  static bool windowReturnPreviewCurrent(std::uintptr_t contentView, Rect frame);
  bool returnWindowDrag(Owner, std::uint64_t geometry, Rect frame);
  void ready(std::uint64_t session, std::uint64_t epoch, std::uint64_t paintedGeometry);
  void input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event);
  void resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height, std::uint64_t request = 0);
  void requestKeyframe(std::uint64_t session, std::uint64_t epoch);
  void restore(std::uint64_t session);
  void restoreAll();
  void releaseAll();
  // Text clipboard only: retains and rechecks the exact exported native focus,
  // process birth, AX window, geometry and lifecycle; never grants input.
  std::optional<ClipboardFocusObservation> clipboardFocus() const;
  // Opt-in metadata observation only. Qt Cocoa otherwise notices other apps'
  // pasteboard changes only when this controller application becomes active.
  void setClipboardTextMonitoring(bool enabled);
  std::function<void(const ClipboardFocusObservation &)> onClipboardTextChanged;
  // An observed candidate and a quiesced network route are not authorization
  // to inject original input or a native-queue barrier. These calls neither
  // change focus nor release pointer ownership. Quiescence requires an empty
  // Qt input ledger; a full scope then also blocks new network pointer input.
  // Admission queries fail during outgoing native callbacks; inspect again
  // after the enclosing focus/geometry/lifecycle operation has completed.
  std::optional<KeyboardRouteBinding> keyboardRouteCandidate(
      std::uint64_t session, std::uint64_t epoch, std::uint64_t generation,
      ForwardingScope scope = ForwardingScope::Keyboard
  ) const;
  bool quiesceKeyboard(const KeyboardRouteBinding &binding);
  bool keyboardRouteQuiesced(const KeyboardRouteBinding &binding) const;
  // Exact local fence and empty legacy ledger only; no native authority query.
  bool keyboardRouteFenced(const KeyboardRouteBinding &binding) const;
  // Revoke original authority first. Only the exact outstanding binding can
  // reopen network keyboard admission, including after invalidation/return.
  bool resumeKeyboard(const KeyboardRouteBinding &binding);
  // Private authenticated coordinator only; never called from Viewer JSON.
  // These checks bind the native endpoint to an existing exact All fence; they
  // do not establish physical neutrality, code admission or compositor drain.
  bool establishOriginalInput(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch);
  bool originalInputCurrent(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const;
  bool deliverOriginalInput(
      const KeyboardRouteBinding &binding, const OriginalInputRecord &record, const LocalInputMappedPoint &point
  );
  // Cleanup retains the original window/process identity through focus loss.
  // Key cleanup fails closed if that app's focused window no longer matches;
  // a CGEvent window annotation cannot prove AppKit key dispatch after a switch.
  // A stale binding cannot release or replace a newer native owner.
  void revokeOriginalInput(const KeyboardRouteBinding &binding);
  std::function<void(QVector<Candidate>)> onCatalog;
  std::function<void(std::uint64_t, std::uint64_t, QJsonObject)> onOpened;
  std::function<void(std::uint64_t, std::uint64_t, std::uint64_t, QByteArray)> onFrame;
  std::function<void(std::uint64_t, std::uint64_t)> onCommitted;
  std::function<void(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t)> onFocused;
  std::function<void(std::uint64_t)> onClosed;
  std::function<void(QString)> onError;
  std::function<void(KeyboardRouteBinding, KeyboardRouteReason)> onKeyboardRouteInvalidated;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace deskflow::windowshare
