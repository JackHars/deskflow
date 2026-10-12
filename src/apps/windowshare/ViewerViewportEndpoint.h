/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "GnomeViewportProtocol.h"
#include <QDeadlineTimer>
#include <QObject>
#include <QPointF>
#include <QSocketNotifier>
#include <QTimer>
#include <functional>
#include <memory>

namespace deskflow::windowshare {
class ViewerViewportBootstrap final
{
public:
  static ViewerViewportBootstrap inspect(int argc, char *const argv[]);
  ~ViewerViewportBootstrap();
  ViewerViewportBootstrap(ViewerViewportBootstrap &&other) noexcept;
  ViewerViewportBootstrap(const ViewerViewportBootstrap &) = delete;
  bool inherited() const noexcept
  {
    return m_descriptor >= 0;
  }
  void validateOptions(const QStringList &options) const;
  int takeDescriptor() noexcept;

private:
  ViewerViewportBootstrap() = default;
  int m_descriptor = -1;
};

// Owner-thread, bounded nonblocking IO. No listening socket, bus calls, input
// injection, or synchronous waits. One exact snapshot is retained for <=250ms.
class ViewerViewportEndpoint final : public QObject
{
public:
  explicit ViewerViewportEndpoint(int ownedDescriptor, QObject *parent = nullptr);
  ~ViewerViewportEndpoint() override;
  std::function<std::optional<GnomeViewportState>()> onSnapshot;
  std::function<bool(const GnomeViewportState &)> onCheck;
  std::function<std::optional<QPointF>(const GnomeViewportState &, QPointF)> onMap;
  std::function<void()> onClosed;
  void invalidate(const QString &reason = QStringLiteral("viewport-changed"));
  void close();
  bool live() const noexcept
  {
    return m_descriptor >= 0;
  }

private:
  void read();
  void write();
  void handle(const GnomeViewportRequest &request);
  bool send(const GnomeViewportReply &reply);
  bool current(std::uint64_t snapshot);
  int m_descriptor = -1;
  std::unique_ptr<QSocketNotifier> m_read, m_write;
  GnomeViewportDecoder m_decoder;
  QByteArray m_output;
  std::optional<GnomeViewportState> m_state;
  std::uint64_t m_lastRequest = 0, m_snapshot = 0;
  QDeadlineTimer m_deadline;
  QTimer m_expiry;
  bool m_reading = false;
};
} // namespace deskflow::windowshare
