/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
namespace deskflow::windowshare {
struct Candidate {
  std::uint32_t id = 0;
  QString title;
};
class NativeHost final : public QObject {
public:
  explicit NativeHost(QString journalPath, QObject *parent = nullptr);
  ~NativeHost() override;
  void refresh();
  void exportWindow(std::uint32_t windowId);
  void ready(std::uint64_t session, std::uint64_t epoch);
  void input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event);
  void resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height);
  void restore(std::uint64_t session);
  void restoreAll();
  void releaseAll();
  std::function<void(QVector<Candidate>)> onCatalog;
  std::function<void(std::uint64_t, std::uint64_t, QJsonObject)> onOpened;
  std::function<void(std::uint64_t, std::uint64_t, QByteArray)> onFrame;
  std::function<void(std::uint64_t, std::uint64_t)> onCommitted;
  std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)> onFocused;
  std::function<void(std::uint64_t)> onClosed;
  std::function<void(QString)> onError;
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace deskflow::windowshare
