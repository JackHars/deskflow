/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace deskflow::windowshare {
// These local widget observations help a person exercise the fixture. They
// never grant routing authority or replace the independent native evidence.
class PhysicalDragTarget final : public QWidget
{
public:
  explicit PhysicalDragTarget(QWidget *parent = nullptr) : QWidget(parent)
  {
    setMinimumSize(260, 116);
    setAccessibleName("Drag the blue puck into the outlined target, then release");
    setMouseTracking(true);
  }
  std::function<void()> completed;
  std::function<bool()> inputAllowed;
  bool accepting = false;
  bool ready() const
  {
    return accepting && (!inputAllowed || inputAllowed());
  }
  QPointF origin() const
  {
    return {42, height() / 2.0};
  }
  QPointF destination() const
  {
    return {width() - 48.0, height() / 2.0};
  }
  void reset()
  {
    m_position = {};
    m_dragging = m_dropped = false;
    update();
  }
  void cancel()
  {
    m_dragging = false;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#f4f7fb"));
    p.setPen(QPen(QColor("#b6c6da"), 2, Qt::DashLine));
    p.drawLine(origin(), destination());
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(m_dropped ? "#127764" : "#526581"), 2));
    p.drawEllipse(destination(), 28, 28);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(m_dropped ? "#127764" : "#245eea"));
    const auto position = m_position.isNull() ? origin() : m_position;
    p.drawEllipse(position, 20, 20);
    p.setPen(Qt::white);
    p.drawText(QRectF(position.x() - 20, position.y() - 20, 40, 40), Qt::AlignCenter, m_dropped ? "✓" : "DRAG");
    p.setPen(QColor("#526581"));
    p.drawText(
        rect().adjusted(10, 4, -10, -4), Qt::AlignBottom | Qt::AlignHCenter,
        m_dragging  ? "Keep holding → release inside the ring"
        : m_dropped ? "Dropped in target"
                    : "Hold • move • release"
    );
  }
  void mousePressEvent(QMouseEvent *event) override
  {
    const auto position = m_position.isNull() ? origin() : m_position;
    if (ready() && event->button() == Qt::LeftButton && QLineF(position, event->position()).length() <= 23) {
      m_dragging = true;
      m_press = event->position();
      m_distance = 0;
      m_dropped = false;
      update();
      event->accept();
    } else
      event->ignore();
  }
  void mouseMoveEvent(QMouseEvent *event) override
  {
    if (ready() && m_dragging && (event->buttons() & Qt::LeftButton)) {
      m_distance = std::max(m_distance, QLineF(m_press, event->position()).length());
      m_position = {
          std::clamp(event->position().x(), 20.0, width() - 20.0),
          std::clamp(event->position().y(), 20.0, height() - 20.0)
      };
      update();
    }
  }
  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (event->button() != Qt::LeftButton)
      return;
    const bool valid =
        ready() && m_dragging && m_distance >= 40 && QLineF(event->position(), destination()).length() <= 28;
    m_dragging = false;
    if (valid) {
      m_position = destination();
      m_dropped = true;
      if (completed)
        completed();
    } else if (!m_dropped)
      m_position = {};
    update();
  }

private:
  QPointF m_position, m_press;
  qreal m_distance = 0;
  bool m_dragging = false, m_dropped = false;
};

class PhysicalScrollTarget final : public QAbstractScrollArea
{
public:
  explicit PhysicalScrollTarget(QWidget *parent = nullptr) : QAbstractScrollArea(parent)
  {
    setMinimumSize(260, 116);
    setAccessibleName("Scroll this grid up, down, left and right");
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    for (auto *bar : {horizontalScrollBar(), verticalScrollBar()}) {
      bar->setRange(0, 2400);
      bar->setPageStep(120);
      connect(bar, &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    }
    reset();
  }
  bool accepting = false;
  std::function<void(QPoint)> scrolled;
  std::function<bool()> inputAllowed;
  void reset()
  {
    horizontalScrollBar()->setValue(1200);
    verticalScrollBar()->setValue(1200);
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(viewport());
    const int x = horizontalScrollBar()->value(), y = verticalScrollBar()->value(), cell = 52;
    p.fillRect(viewport()->rect(), QColor("#edf3fb"));
    for (int row = y / cell; row <= (y + viewport()->height()) / cell; ++row)
      for (int column = x / cell; column <= (x + viewport()->width()) / cell; ++column) {
        const QRect tile(column * cell - x, row * cell - y, cell, cell);
        p.fillRect(tile.adjusted(1, 1, -1, -1), QColor((row + column) % 2 ? "#dce8f9" : "#f4f7fb"));
        p.setPen(QColor("#526581"));
        p.drawText(tile, Qt::AlignCenter, QStringLiteral("%1 · %2").arg(column).arg(row));
      }
    p.setPen(QPen(QColor("#245eea"), 2));
    const auto center = viewport()->rect().center();
    p.drawLine(center + QPoint(-9, 0), center + QPoint(9, 0));
    p.drawLine(center + QPoint(0, -9), center + QPoint(0, 9));
  }
  void wheelEvent(QWheelEvent *event) override
  {
    if (!accepting || (inputAllowed && !inputAllowed())) {
      event->accept();
      return;
    }
    const auto delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta() / 3;
    const QPoint before(horizontalScrollBar()->value(), verticalScrollBar()->value());
    horizontalScrollBar()->setValue(before.x() - delta.x());
    verticalScrollBar()->setValue(before.y() - delta.y());
    const QPoint moved(before.x() - horizontalScrollBar()->value(), before.y() - verticalScrollBar()->value());
    // Only actual wheel-driven movement counts. Moving a scrollbar or hitting
    // a content boundary cannot falsely complete an axis check.
    if (!moved.isNull() && scrolled)
      scrolled(moved);
    event->accept();
  }
};

class PhysicalInputDashboard final : public QWidget
{
public:
  enum class Phase
  {
    Waiting,
    Practice,
    Arming,
    Recording,
    Draining,
    Complete,
    Failed
  };
  explicit PhysicalInputDashboard(QWidget *parent = nullptr, std::function<qint64()> clock = {})
      : QWidget(parent),
        m_now(std::move(clock))
  {
    m_clock.start();
    if (!m_now)
      m_now = [this] { return m_clock.elapsed(); };
    setObjectName("inputDashboard");
    setStyleSheet(
        "QWidget#inputDashboard { background: #f4f7fb; color: #172b4d; }"
        "QLabel { color: #172b4d; background: transparent; }"
        "QFrame[card=true] { background: white; border: 1px solid #d6dfeb; border-radius: 9px; }"
        "QLineEdit { background: white; color: #172b4d; border: 2px solid #b6c6da; border-radius: 5px; padding: 8px; }"
        "QLineEdit:focus { border-color: #245eea; }"
        "QPushButton { background: #e4edff; color: #193d8d; border: 1px solid #b6caf7; border-radius: 6px; padding: "
        "9px; }"
        "QPushButton:focus { border: 2px solid #245eea; }"
        "QProgressBar { border: none; border-radius: 4px; background: #e3eaf5; text-align: center; color: #172b4d; }"
        "QProgressBar::chunk { background: #86b5ff; border-radius: 4px; }"
    );
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(10);
    auto *heading = new QLabel("Make every input visible", this);
    auto titleFont = font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    heading->setFont(titleFont);
    layout->addWidget(heading);
    m_phaseLabel = label("Waiting for connection", layout);
    m_phaseLabel->setMinimumHeight(46);
    m_phaseLabel->setMargin(10);
    m_instruction = label("Click Start in the Mac controls. This board will appear on Ubuntu.", layout);
    auto *typing = card(layout, "1  Type aBc, then hold x for one second and release");
    m_field = new QLineEdit(this);
    m_field->setObjectName("typingField");
    m_field->setAccessibleName("Type aBc, then hold x for one second and release");
    m_field->setPlaceholderText("Type aBc here, then hold x…");
    m_field->setMaxLength(128);
    auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(17);
    m_field->setFont(mono);
    typing->addWidget(m_field);
    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textEdited, this, [this](const QString &text) {
      if (accepting() && text.contains("aBc"))
        m_checks[0] = true;
    });
    auto *keyRow = new QHBoxLayout;
    m_repeatLabel = new QLabel;
    m_repeatLabel->setMinimumWidth(170);
    keyRow->addWidget(m_repeatLabel);
    m_hold = new QProgressBar;
    m_hold->setRange(0, 1000);
    m_hold->setMinimumHeight(22);
    keyRow->addWidget(m_hold);
    typing->addLayout(keyRow);
    auto *clickRow = new QHBoxLayout;
    m_click = new QPushButton("2  Click this button");
    m_click->setObjectName("clickTarget");
    m_click->setFocusPolicy(Qt::NoFocus);
    m_clickLabel = new QLabel("Waiting for a click");
    clickRow->addWidget(m_click);
    clickRow->addWidget(m_clickLabel, 1);
    layout->addLayout(clickRow);
    connect(m_click, &QPushButton::clicked, this, [this] {
      if (accepting()) {
        ++m_clicks;
        m_checks[2] = true;
      }
      refresh();
    });
    auto *movement = new QHBoxLayout;
    auto *dragBox = new QVBoxLayout;
    auto *scrollBox = new QVBoxLayout;
    movement->addLayout(dragBox, 1);
    movement->addLayout(scrollBox, 1);
    layout->addLayout(movement);
    label("3  Drag the puck into the ring", dragBox);
    m_drag = new PhysicalDragTarget;
    m_drag->inputAllowed = [this] { return accepting(); };
    dragBox->addWidget(m_drag);
    m_drag->completed = [this] {
      ++m_drops;
      m_checks[3] = true;
      refresh();
    };
    label("4  Scroll up, down, left and right", scrollBox);
    m_scroll = new PhysicalScrollTarget;
    m_scroll->inputAllowed = [this] { return accepting(); };
    scrollBox->addWidget(m_scroll);
    m_scrollLabel = label("↑ 0   ↓ 0   ← 0   → 0 px", scrollBox);
    m_scroll->scrolled = [this](QPoint delta) {
      ++m_wheels;
      m_scrollDistance[delta.x() >= 0 ? 0 : 1] += std::abs(delta.x());
      m_scrollDistance[delta.y() >= 0 ? 2 : 3] += std::abs(delta.y());
      m_checks[4] = m_scrollDistance[2] >= 16 && m_scrollDistance[3] >= 16;
      m_checks[5] = m_scrollDistance[0] >= 16 && m_scrollDistance[1] >= 16;
    };
    m_summary = label("0 / 6 checks observed", layout);
    auto summaryFont = font();
    summaryFont.setBold(true);
    summaryFont.setPointSize(13);
    m_summary->setFont(summaryFont);
    auto *checks = new QGridLayout;
    layout->addLayout(checks);
    for (int i = 0; i < 6; ++i) {
      m_checkLabels[i] = new QLabel;
      checks->addWidget(m_checkLabels[i], i / 3, i % 3);
    }
    m_footer = label("Practice first. Recording clears these checks so you can see exactly what was measured.", layout);
    m_footer->setStyleSheet("color: #526581; font-size: 11px;");
    connect(&m_timer, &QTimer::timeout, this, [this] { refresh(); });
    m_timer.start(100);
    setPhase(Phase::Waiting);
  }
  QLineEdit &field() const
  {
    return *m_field;
  }
  PhysicalDragTarget &dragTarget() const
  {
    return *m_drag;
  }
  PhysicalScrollTarget &scrollTarget() const
  {
    return *m_scroll;
  }
  QPushButton &clickTarget() const
  {
    return *m_click;
  }
  Phase phase() const
  {
    return m_phase;
  }
  bool accepting() const
  {
    return m_phase == Phase::Practice || (m_phase == Phase::Recording && (!m_recordingOpen || m_recordingOpen()));
  }
  void setRecordingGate(std::function<bool()> recordingOpen)
  {
    // Score against the actual recording boundary, not the label refresh.
    // This restricts display scoring only; it never grants input authority.
    m_recordingOpen = std::move(recordingOpen);
  }
  int checked() const
  {
    return std::count(m_checks.begin(), m_checks.end(), true);
  }
  QString phaseName() const
  {
    constexpr const char *names[]{"waiting", "practice", "arming", "recording", "draining", "complete", "failed"};
    return names[static_cast<unsigned>(m_phase)];
  }
  void setPhase(Phase phase, int secondsRemaining = 0)
  {
    if (m_phase != phase) {
      m_xDown = false;
      m_drag->cancel();
      if (phase == Phase::Practice || phase == Phase::Recording)
        reset();
      if (phase == Phase::Practice)
        m_measured = false;
      if (phase == Phase::Recording)
        m_measured = true;
      m_phase = phase;
    }
    m_remaining = secondsRemaining;
    m_drag->accepting = m_scroll->accepting = accepting();
    m_field->setReadOnly(!accepting());
    m_click->setEnabled(accepting());
    refresh();
  }
  QJsonObject snapshot() const
  {
    QJsonObject checks;
    for (int i = 0; i < 6; ++i)
      checks[checkKeys[i]] = m_checks[i];
    const auto rect = [this](QWidget *widget) {
      const auto point = widget->mapTo(window(), QPoint());
      return QJsonObject{{"x", point.x()}, {"y", point.y()}, {"width", widget->width()}, {"height", widget->height()}};
    };
    return {
        {"phase", phaseName()},
        {"checkScope", m_measured ? "recording" : "practice"},
        {"checks", checks},
        {"completedChecks", checked()},
        {"repeatEvents", m_repeats},
        {"longestHoldMs", m_longestHold},
        {"xHeld", m_xDown},
        {"clicks", m_clicks},
        {"drops", m_drops},
        {"wheelEvents", m_wheels},
        {"horizontalPositivePixels", m_scrollDistance[0]},
        {"horizontalNegativePixels", m_scrollDistance[1]},
        {"verticalPositivePixels", m_scrollDistance[2]},
        {"verticalNegativePixels", m_scrollDistance[3]},
        {"typingField", rect(m_field)},
        {"clickTarget", rect(m_click)},
        {"dragTarget", rect(m_drag)},
        {"scrollTarget", rect(m_scroll->viewport())},
        {"physicalAcceptance", "not-assessed"}
    };
  }
  void refresh()
  {
    const qint64 held = m_xDown ? std::max<qint64>(0, m_now() - m_holdStart) : m_lastHold;
    m_repeatLabel->setText(QStringLiteral("X repeats received: %1").arg(m_repeats));
    m_hold->setValue(std::min<qint64>(1000, held));
    m_hold->setFormat(
        m_xDown       ? QStringLiteral("Holding x · %1 s").arg(held / 1000.0, 0, 'f', 1)
        : m_checks[1] ? "Held, repeated and released ✓"
                      : "Hold x for 1 second, then release"
    );
    m_clickLabel->setText(m_clicks ? QStringLiteral("✓ %1 clicks received").arg(m_clicks) : "Waiting for a click");
    m_scrollLabel->setText(QStringLiteral("↑ %1   ↓ %2   ← %3   → %4 px")
                               .arg(m_scrollDistance[2])
                               .arg(m_scrollDistance[3])
                               .arg(m_scrollDistance[0])
                               .arg(m_scrollDistance[1]));
    for (int i = 0; i < 6; ++i) {
      m_checkLabels[i]->setText(QString(m_checks[i] ? "✓  " : "○  ") + checkNames[i]);
      const QString style = m_checks[i] ? "color: #127764; font-weight: 600;" : "color: #526581;";
      if (m_checkLabels[i]->styleSheet() != style)
        m_checkLabels[i]->setStyleSheet(style);
    }
    m_summary->setText(
        QStringLiteral("%1 / 6 checks observed%2").arg(checked()).arg(m_measured ? " in recording" : " in practice")
    );
    QString title, instruction, color = "#e3eaf5";
    switch (m_phase) {
    case Phase::Waiting:
      title = "WAITING FOR CONNECTION";
      instruction = "Click Start in the Mac controls. This board will appear on Ubuntu.";
      break;
    case Phase::Practice:
      title = "PRACTICE · inputs are not being recorded yet";
      instruction = "Try all four areas below. Then click Record and release all controls.";
      break;
    case Phase::Arming:
      title = "GET READY · release all keys and buttons";
      instruction = "Stay on Ubuntu. Wait for the blue RECORDING banner before repeating the checks.";
      break;
    case Phase::Recording:
      title = QStringLiteral("RECORDING NOW · %1 seconds left").arg(m_remaining);
      instruction = "Repeat all four areas now. Each check must light up again during this countdown.";
      color = "#dbe8ff";
      break;
    case Phase::Draining:
      title = "RECORDING ENDED · finishing the logs";
      instruction = "Release the controls. Wait here while the recording finishes.";
      break;
    case Phase::Complete:
      title =
          checked() == 6 ? "RECORDING ENDED · all 6 visual checks observed" : "RECORDING ENDED · checks are incomplete";
      instruction = "Ctrl+Option+Shift+Left returns to the Mac. Release the keys, then click Stop.";
      color = checked() == 6 ? "#dff2ec" : "#fff0d6";
      break;
    case Phase::Failed:
      title = "TEST STOPPED · recording could not finish";
      instruction = "Return to the Mac and click Stop. The failure is saved in the logs.";
      color = "#fff0d6";
      break;
    }
    m_phaseLabel->setText(title);
    const auto style = QStringLiteral("background: %1; border-radius: 6px; font-weight: 700;").arg(color);
    if (m_phaseLabel->styleSheet() != style)
      m_phaseLabel->setStyleSheet(style);
    m_instruction->setText(instruction);
    m_footer->setText(
        m_phase == Phase::Complete
            ? "These checks show what this window received. Saved input logs still need verification."
            : "Practice first. Recording clears these checks so you can see exactly what was measured."
    );
  }

protected:
  bool eventFilter(QObject *object, QEvent *event) override
  {
    if (object != m_field)
      return false;
    if (event->type() == QEvent::FocusOut)
      m_xDown = false;
    if (!accepting() || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
      return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() != Qt::Key_X)
      return false;
    if (event->type() == QEvent::KeyPress) {
      if (!key->isAutoRepeat() && !m_xDown) {
        m_xDown = true;
        m_holdStart = m_now();
        m_holdRepeats = 0;
      } else if (key->isAutoRepeat() && m_xDown) {
        ++m_repeats;
        ++m_holdRepeats;
      }
    } else if (!key->isAutoRepeat() && m_xDown) {
      m_lastHold = std::max<qint64>(0, m_now() - m_holdStart);
      m_longestHold = std::max(m_longestHold, m_lastHold);
      if (m_lastHold >= 1000 && m_holdRepeats > 0)
        m_checks[1] = true;
      m_xDown = false;
    }
    return false;
  }

private:
  static QLabel *label(const QString &text, QVBoxLayout *layout)
  {
    auto *result = new QLabel(text);
    result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);
    layout->addWidget(result);
    return result;
  }
  static QVBoxLayout *card(QVBoxLayout *layout, const QString &title)
  {
    auto *frame = new QFrame;
    frame->setProperty("card", true);
    auto *inside = new QVBoxLayout(frame);
    inside->setContentsMargins(12, 10, 12, 10);
    label(title, inside);
    layout->addWidget(frame);
    return inside;
  }
  void reset()
  {
    m_checks.fill(false);
    m_scrollDistance.fill(0);
    m_repeats = m_holdRepeats = m_clicks = m_drops = m_wheels = 0;
    m_lastHold = m_longestHold = 0;
    m_xDown = false;
    m_field->clear();
    m_drag->reset();
    m_scroll->reset();
  }
  static constexpr const char *checkKeys[]{"typing",   "holdRepeatRelease", "click",
                                           "dragDrop", "verticalScroll",    "horizontalScroll"};
  static constexpr const char *checkNames[]{"Typed aBc",   "Held x + repeat", "Click received",
                                            "Drag + drop", "Vertical scroll", "Horizontal scroll"};
  QElapsedTimer m_clock;
  std::function<qint64()> m_now;
  std::function<bool()> m_recordingOpen;
  QTimer m_timer;
  Phase m_phase = Phase::Waiting;
  QLineEdit *m_field;
  QLabel *m_phaseLabel, *m_instruction, *m_repeatLabel, *m_clickLabel, *m_scrollLabel, *m_summary, *m_footer;
  QProgressBar *m_hold;
  QPushButton *m_click;
  PhysicalDragTarget *m_drag;
  PhysicalScrollTarget *m_scroll;
  std::array<QLabel *, 6> m_checkLabels{};
  std::array<bool, 6> m_checks{};
  std::array<qint64, 4> m_scrollDistance{};
  int m_remaining = 0, m_repeats = 0, m_holdRepeats = 0, m_clicks = 0, m_drops = 0, m_wheels = 0;
  qint64 m_holdStart = 0, m_lastHold = 0, m_longestHold = 0;
  bool m_xDown = false, m_measured = false;
};
} // namespace deskflow::windowshare
