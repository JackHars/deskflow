/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "GnomeViewportProtocol.h"
#include "WaylandViewport.h"
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
  // Only the authenticated GNOME Bridge installs the native implementation.
  // Fixture callbacks may test delayed/stale responses, but cannot mint
  // production original-input authority.
  std::function<bool(const GnomeViewportState &, std::function<void(bool)>)> onQtQueue;
  // Distinct from observational Check. Both callbacks must retain the exact
  // live All forwarding fence; missing native support always denies.
  std::function<bool(const GnomeViewportState &, const InputCutoffBinding &)> onInputDrainCheck;
  std::function<bool(const GnomeViewportState &, const InputCutoffBinding &, QDeadlineTimer, std::function<void(bool)>)>
      onInputDrain;
  std::function<void()> onClosed;
  std::function<std::optional<GnomePreparationInventory>(const InputPreparationBinding &)> onPreparationInventory;
  std::function<bool(const InputPreparationBinding &, const GnomePreparationTarget &)> onPreparationCheck;
  std::function<std::optional<QPointF>(const InputPreparationBinding &, const GnomePreparationTarget &, QPointF)>
      onPreparationMap;
  std::function<
      bool(const InputPreparationBinding &, const GnomePreparationTarget &, QDeadlineTimer, std::function<void(bool)>)>
      onPreparationDrain;
  std::function<std::optional<GnomeViewportState>(const InputPreparationBinding &, const GnomePreparationTarget &)>
      onPreparationFocus;
  std::function<
      bool(const InputPreparationBinding &, const GnomePreparationTarget &, const std::optional<InputCutoffBinding> &)>
      onPreparationFinish;
  bool startWaylandQueue(QWindow *window, const WaylandViewportGeometry &expected, std::function<void(bool)> finished);
  bool startWaylandInputDrain(
      QWindow *window, const WaylandViewportGeometry &expected, QDeadlineTimer deadline,
      std::function<bool()> exactFence, std::function<void(bool)> finished
  );
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
  void handlePreparation(const GnomeViewportRequest &request);
  void finishPreparationQueue(std::uint64_t request, bool complete);
  void pollPreparationFocus();
  bool currentPreparation(const GnomePreparationTarget &target);
  void replyPreparation(const GnomeViewportRequest &request, bool ok, const QString &reason = {});
  bool send(const GnomeViewportReply &reply);
  bool current(std::uint64_t snapshot);
  bool currentInputDrain(const GnomeViewportRequest &request, QDeadlineTimer deadline);
  void finishQtQueue(const GnomeViewportRequest &request, bool observed);
  void invalidateSnapshot(const QString &reason);
  void retireSeal(const QString &reason);
  bool currentInputMap(const GnomeViewportRequest &request, QDeadlineTimer deadline);
  struct RetainedSeal
  {
    InputCutoffBinding origin, last;
    GnomeViewportState state;
  };
  std::optional<RetainedSeal> m_seal;
  struct PendingQueue
  {
    GnomeViewportRequest request;
    QDeadlineTimer deadline;
    bool starting = true;
    std::optional<bool> completion;
  };
  int m_descriptor = -1;
  std::unique_ptr<QSocketNotifier> m_read, m_write;
  GnomeViewportDecoder m_decoder;
  QByteArray m_output;
  std::optional<GnomeViewportState> m_state;
  std::uint64_t m_lastRequest = 0, m_snapshot = 0;
  QDeadlineTimer m_deadline;
  QTimer m_expiry;
  QTimer m_queueExpiry;
  QtWaylandQueueWitness m_queueWitness;
  std::optional<PendingQueue> m_pendingCheck;
  bool m_reading = false;
  struct Preparation
  {
    InputPreparationBinding binding;
    GnomePreparationInventory inventory;
    QDeadlineTimer deadline;
    std::optional<GnomePreparationTarget> target;
    std::optional<GnomeViewportState> focused;
    std::optional<InputCutoffBinding> promoted;
    bool drained = false;
  };
  std::optional<Preparation> m_preparation;
  std::optional<GnomeViewportRequest> m_preparationPending;
  QDeadlineTimer m_preparationOperationDeadline;
  QTimer m_preparationPoll, m_preparationExpiry;
};
} // namespace deskflow::windowshare
