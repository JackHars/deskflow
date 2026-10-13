/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QApplication>
#include <QDeadlineTimer>
#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
class QWindow;
struct wl_display;
struct wl_callback;
namespace deskflow::windowshare {
// The experimental viewer owns its QApplication. Input notifications are
// counted before application/object filters, including nested processEvents.
// Native QPA tracking is separately opted in only for its private GNOME
// endpoint, and never replaces another component's event handler.
class ViewerApplication final : public QApplication
{
public:
  ViewerApplication(int &argc, char **argv);
  ~ViewerApplication() override;
  bool enableInputDrainDispatch();
  bool inputDispatchIdle() const noexcept;
  bool ownsInputDrainDispatch() const noexcept;
  bool notify(QObject *receiver, QEvent *event) override;

private:
  struct Dispatch;
  std::shared_ptr<Dispatch> m_dispatch;
};

struct WaylandViewportGeometry
{
  quintptr surface = 0;
  std::uint32_t resource = 0;
  QSizeF extent;
  QPointF contentOrigin;
  double logicalScale = 0;
  bool operator==(const WaylandViewportGeometry &) const = default;
};
// Borrows the actual platform window. Never opens/dispatches a second Wayland
// connection, commits a surface, changes event queues, or infers global origin.
std::optional<WaylandViewportGeometry> readWaylandViewport(QWindow *window, QString *reason = nullptr);
std::optional<QPointF>
mapWaylandViewport(QWindow *window, const WaylandViewportGeometry &expected, QPointF surfacePoint);

struct WaylandWindowAssociation
{
  QString token;
  QPointer<QObject> role;
  quintptr surface = 0, topLevel = 0;
  std::uint32_t resource = 0;
};
// Writes an opaque app_id on this exact existing xdg_toplevel. Its appearance
// on one owned MetaWindow is the compositor-side challenge response; window
// titles, PIDs, rectangles and other windows on this connection are irrelevant.
std::optional<WaylandWindowAssociation>
associateWaylandWindow(QWindow *window, const WaylandViewportGeometry &expected);
bool currentWaylandAssociation(
    QWindow *window, const WaylandViewportGeometry &expected, const WaylandWindowAssociation &association
);

// Read-only, bounded observation on the *existing* QtWayland default input
// event queue. Two ordered wl_display_sync callbacks are chained: the second
// request is issued only after the first callback. Qt QPA events are dispatched
// on the GUI thread before completion. No new Wayland connection, event queue,
// blocking roundtrip, input injection, or original-input permit is created.
// This C++ object is owned by the same GUI-thread native viewport endpoint.
class QtWaylandQueueWitness final : public QObject
{
public:
  using Completion = std::function<void(bool)>;
  explicit QtWaylandQueueWitness(QObject *parent = nullptr);
  ~QtWaylandQueueWitness() override;
  QtWaylandQueueWitness(const QtWaylandQueueWitness &) = delete;
  QtWaylandQueueWitness &operator=(const QtWaylandQueueWitness &) = delete;

  bool begin(QWindow *window, const WaylandViewportGeometry &expected, Completion completion);
  bool beginInputDrain(
      QWindow *window, const WaylandViewportGeometry &expected, QDeadlineTimer deadline,
      std::function<bool()> exactFence, Completion completion
  );
  void cancel() noexcept;

private:
  bool issueSync();
  bool current();
  bool beginQueue(QWindow *window, const WaylandViewportGeometry &expected, Completion completion);
  void finish(bool observed);
  static void syncDone(void *data, wl_callback *callback, std::uint32_t serial) noexcept;

  QPointer<QWindow> m_window;
  QPointer<QObject> m_inputContext;
  bool m_hadInputContext = false;
  WaylandViewportGeometry m_expected;
  wl_display *m_display = nullptr;   // Borrowed only during the native Qt lifetime.
  wl_callback *m_callback = nullptr; // Owned and destroyed on this GUI thread.
  QMetaObject::Connection m_windowGone;
  QTimer m_timeout;
  Completion m_completion;
  std::shared_ptr<const int> m_operation;
  std::function<bool()> m_exactFence;
  QDeadlineTimer m_deadline;
  bool m_inputDrain = false;
  unsigned m_phase = 0;
};
} // namespace deskflow::windowshare
