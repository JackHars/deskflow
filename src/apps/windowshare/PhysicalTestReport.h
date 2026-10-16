/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace deskflow::windowshare {
// Independent report-only process: no native event filter, recorder, route,
// synthetic input, timeout, sleep, or automatic dismissal. Cleanup completes
// before the physical coordinator opens it, and its log survives dismissal.
class PhysicalTestReport final : public QWidget
{
public:
  explicit PhysicalTestReport(QString path) : m_path(std::move(path))
  {
    setWindowTitle("Deskflow test results — stays open until dismissed");
    resize(780, 540);
    auto *layout = new QVBoxLayout(this);
    m_status = new QLabel("Reading the saved test timeline…", this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);
    m_timeline = new QPlainTextEdit(this);
    m_timeline->setReadOnly(true);
    layout->addWidget(m_timeline, 1);
    auto *location =
        new QLabel(QStringLiteral("Saved log: %1\nThis report does not share or capture input.").arg(m_path), this);
    location->setTextFormat(Qt::PlainText);
    location->setWordWrap(true);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(location);
    auto *dismiss = new QPushButton("Dismiss", this);
    connect(dismiss, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(dismiss);
    m_watcher.addPath(QFileInfo(m_path).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { refresh(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { refresh(); });
    refresh();
  }

  void refresh()
  {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
      if (m_sequence == 0)
        m_status->setText("The diagnostic log could not be opened. This window will remain open.");
      return; // Never erase a previously read failure during cleanup.
    }
    if (file.size() > 16 * 1024 * 1024) {
      m_status->setText("The diagnostic log is too large to display. The complete log is saved at the path below.");
      return;
    }
    if (!m_watcher.files().contains(m_path))
      m_watcher.addPath(m_path);
    file.seek(m_offset);
    m_pending += file.readAll();
    m_offset = file.pos();
    while (m_pending.contains('\n')) {
      const auto split = m_pending.indexOf('\n');
      const auto line = m_pending.left(split);
      m_pending.remove(0, split + 1);
      QJsonParseError error;
      const auto document = QJsonDocument::fromJson(line, &error);
      const auto row = document.object();
      const auto sequence = row.value("sequence").toInteger();
      if (error.error != QJsonParseError::NoError || !document.isObject() ||
          row.value("schema").toString() != "deskflow-physical-diagnostic-v1" || sequence != m_sequence + 1) {
        m_status->setText(
            "The saved timeline is incomplete or invalid. Review the log below; this test is not accepted."
        );
        return;
      }
      m_sequence = sequence;
      const auto step = row.value("step").toString();
      const auto state = row.value("state").toString();
      const auto message = row.value("message").toString();
      m_timeline->appendPlainText(
          QStringLiteral("%1  +%2 ms  [%3] %4\n%5\n%6\n")
              .arg(row.value("utc").toString())
              .arg(row.value("elapsedMs").toInteger())
              .arg(
                  state, step, message,
                  QString::fromUtf8(QJsonDocument(row.value("details").toObject()).toJson(QJsonDocument::Compact))
              )
      );
      if (state == "failed" && m_firstFailure.isEmpty())
        m_firstFailure = step + ": " + message;
      if (!m_firstFailure.isEmpty())
        m_status->setText(
            "Test stopped. First recorded failure:\n" + m_firstFailure +
            "\nThe timeline includes later errors and cleanup. Dismiss this window when finished reading."
        );
      else if (step == "finished")
        m_status->setText(
            "Test stopped. Results still require review; physical acceptance is not automatic.\n"
            "Dismiss this window when finished reading."
        );
    }
  }

  QString firstFailure() const
  {
    return m_firstFailure;
  }
  QString timeline() const
  {
    return m_timeline->toPlainText();
  }
  qint64 sequence() const
  {
    return m_sequence;
  }

private:
  QString m_path, m_firstFailure;
  QFileSystemWatcher m_watcher;
  QLabel *m_status = nullptr;
  QPlainTextEdit *m_timeline = nullptr;
  QByteArray m_pending;
  qint64 m_offset = 0, m_sequence = 0;
};
} // namespace deskflow::windowshare
