/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace deskflow::windowshare {

class NativeInspectionCompletionEvent final : public QEvent
{
public:
  static QEvent::Type eventType()
  {
    static const auto value = static_cast<QEvent::Type>(QEvent::registerEventType());
    return value;
  }
  explicit NativeInspectionCompletionEvent(std::function<void()> complete)
      : QEvent(eventType()),
        complete(std::move(complete))
  {
  }
  std::function<void()> complete;
};

// The worker never dereferences its owner. Retirement fences posting before
// QObject destruction removes any already queued completion. There is still
// only one read/completion per inspection, under the process-wide worker cap.
class NativeInspectionDelivery final
{
public:
  explicit NativeInspectionDelivery(QObject *receiver) : m_receiver(receiver)
  {
  }
  void post(std::function<void()> complete, Qt::EventPriority priority)
  {
    std::lock_guard lock(m_mutex);
    if (m_receiver)
      QCoreApplication::postEvent(m_receiver, new NativeInspectionCompletionEvent(std::move(complete)), priority);
  }
  void close()
  {
    std::lock_guard lock(m_mutex);
    m_receiver = nullptr;
  }

private:
  std::mutex m_mutex;
  QObject *m_receiver;
};

// Shared across result types and owner lifetimes in this process. Cancellation
// cannot cancel native IPC, and deleting/replacing an owner must not bypass
// admission. Include finished threads awaiting Qt deletion and results awaiting
// owner-thread consumption/discard in the bound.
class NativeInspectionCapacity final
{
public:
  static constexpr unsigned maximum = 64;
  class Permit final
  {
  public:
    ~Permit()
    {
      if (m_claimed)
        m_retained.fetch_sub(1, std::memory_order_relaxed);
    }
    Permit(const Permit &) = delete;
    Permit &operator=(const Permit &) = delete;

  private:
    friend class NativeInspectionCapacity;
    Permit() = default;
    bool m_claimed = false;
  };
  static std::shared_ptr<Permit> acquire()
  {
    auto retained = m_retained.load(std::memory_order_relaxed);
    if (retained >= maximum)
      return {};
    // Allocate before claiming a slot: failed control-block allocation must
    // neither leak the claim nor release it twice through a deleter.
    auto permit = std::shared_ptr<Permit>(new Permit);
    while (retained < maximum) {
      if (m_retained.compare_exchange_weak(retained, retained + 1, std::memory_order_relaxed)) {
        permit->m_claimed = true;
        return permit;
      }
    }
    return {};
  }
  static unsigned retained()
  {
    return m_retained.load(std::memory_order_relaxed);
  }

private:
  static inline std::atomic<unsigned> m_retained = 0;
};

// One read-only native inspection, never an input-authority cache. A stalled
// system query must not stall native input or create more worker requests.
// Completion runs on the owner thread and expires at the original deadline.
template <class Result, int MaximumAgeMs = 250, Qt::EventPriority CompletionPriority = Qt::NormalEventPriority>
class AsyncNativeInspection final : public QObject
{
public:
  using Clock = std::chrono::steady_clock;
  using Completion = std::function<void(std::optional<Result>)>;
  // The default remains the native input/environment limit. Placement uses
  // its existing two-second transaction deadline, independently enforced by
  // NativePlacementVerification; it never grants an input receipt.
  static_assert(MaximumAgeMs > 0 && MaximumAgeMs <= 2000);
  static constexpr auto maximumAge = std::chrono::milliseconds(MaximumAgeMs);
  explicit AsyncNativeInspection(std::function<Clock::time_point()> now = Clock::now) : m_now(std::move(now))
  {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] { expire(); });
  }
  ~AsyncNativeInspection() override
  {
    m_delivery->close();
  }
  bool request(std::function<Result()> inspect, Completion completion)
  {
    if (QThread::currentThread() != thread() || m_running || !inspect || !completion)
      return false;
    const auto permit = NativeInspectionCapacity::acquire();
    if (!permit)
      return false;
    auto result = std::make_shared<std::optional<Result>>();
    // The worker captures only its query and result, never this or its owner.
    auto *worker = QThread::create([inspect = std::move(inspect), result] {
      try {
        *result = inspect();
      } catch (...) {
        // Missing results fail closed on the owner thread.
      }
    });
    // The thread QObject survives its native query and any cancelled owner.
    // Keep its permit until Qt has also retired its deferred deletion.
    QObject::connect(worker, &QObject::destroyed, [permit] { (void)permit; });
    QObject::connect(worker, &QThread::finished, [this, result, permit, delivery = m_delivery] {
      // The environment watchdog opts into priority over ordinary work.
      // Placement retains FIFO ordering with already queued cancellation.
      // Both still reject results at their original absolute deadline.
      delivery->post(
          [this, result, permit] {
            // A retired QThread must not free capacity while its result still
            // occupies the owner's event queue. Discard on destruction also
            // releases this claim without invoking the former owner.
            (void)permit;
            m_running = false;
            finish(m_now() < m_due ? std::move(*result) : std::nullopt);
          },
          CompletionPriority
      );
    });
    // A destroyed owner cancels its callback, without blocking on system IPC.
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    m_running = true;
    m_due = m_now() + maximumAge;
    m_completion = std::move(completion);
    m_timer.start(maximumAge);
    worker->start();
    return true;
  }
  void cancel()
  {
    m_timer.stop();
    m_completion = {};
    // An uncancellable native read still occupies the single worker slot.
  }
  void expire()
  {
    if (!m_completion)
      return;
    const auto remaining = m_due - m_now();
    if (remaining > Clock::duration::zero())
      m_timer.start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
    else
      finish(std::nullopt);
  }
  bool busy() const
  {
    return m_running;
  }

private:
  bool event(QEvent *event) override
  {
    if (event->type() != NativeInspectionCompletionEvent::eventType())
      return QObject::event(event);
    auto complete = std::move(static_cast<NativeInspectionCompletionEvent *>(event)->complete);
    complete(); // May destroy this inspection; do not access it afterward.
    return true;
  }
  void finish(std::optional<Result> result)
  {
    m_timer.stop();
    auto completion = std::exchange(m_completion, {});
    if (completion)
      completion(std::move(result)); // May destroy this inspection or its owner.
  }
  std::function<Clock::time_point()> m_now;
  Completion m_completion;
  Clock::time_point m_due;
  std::shared_ptr<NativeInspectionDelivery> m_delivery = std::make_shared<NativeInspectionDelivery>(this);
  QTimer m_timer;
  bool m_running = false;
};
} // namespace deskflow::windowshare
