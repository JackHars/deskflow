/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"
#include "KeyboardRoute.h"
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
  void refresh();
  void exportWindow(std::uint32_t windowId);
  void ready(std::uint64_t session, std::uint64_t epoch);
  void input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event);
  void resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height, std::uint64_t request = 0);
  void requestKeyframe(std::uint64_t session, std::uint64_t epoch);
  void restore(std::uint64_t session);
  void restoreAll();
  void releaseAll();
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
  // Revoke original authority first. Only the exact outstanding binding can
  // reopen network keyboard admission, including after invalidation/return.
  bool resumeKeyboard(const KeyboardRouteBinding &binding);
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
