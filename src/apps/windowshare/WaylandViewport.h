/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QPointF>
#include <QTimer>
#include <QSizeF>
#include <QString>
#include <cstdint>
#include <optional>
#include <functional>
class QWindow;
struct wl_display;
struct wl_callback;
namespace deskflow::windowshare {
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
  void cancel() noexcept;

private:
  bool issueSync();
  bool current() const;
  void finish(bool observed);
  static void syncDone(void *data, wl_callback *callback, std::uint32_t serial) noexcept;

  QPointer<QWindow> m_window;
  WaylandViewportGeometry m_expected;
  wl_display *m_display = nullptr; // Borrowed only during the native Qt lifetime.
  wl_callback *m_callback = nullptr; // Owned and destroyed on this GUI thread.
  QMetaObject::Connection m_windowGone;
  QTimer m_timeout;
  Completion m_completion;
  unsigned m_phase = 0;
};
} // namespace deskflow::windowshare
