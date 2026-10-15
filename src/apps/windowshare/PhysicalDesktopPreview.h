/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSaveFile>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QtEndian>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace deskflow::windowshare {
// Disposable observer process only. It never captures, translates or injects
// input, and never shares a process/event recorder with the source window.
class PhysicalDesktopPreview final : public QWidget
{
public:
  PhysicalDesktopPreview(QString framePath, QString receiptPath, bool showOnFrame = true)
      : m_framePath(std::move(framePath)),
        m_receiptPath(std::move(receiptPath)),
        m_showOnFrame(showOnFrame)
  {
    setWindowTitle("Ubuntu test desktop — live view");
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    auto *layout = new QVBoxLayout(this);
    auto *instructions = new QLabel(
        "This is the Ubuntu test desktop. Ctrl+Option+Shift+Right controls its pointer.\n"
        "Ctrl+Option+Shift+Left returns to the Mac. Closing this view stops the test.",
        this
    );
    layout->addWidget(instructions);
    m_image = new QLabel("Waiting for the Ubuntu desktop…", this);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumSize(640, 360);
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layout->addWidget(m_image, 1);
    resize(1100, 700);
    connect(&m_timer, &QTimer::timeout, this, [this] { refresh(); });
    m_timer.start(100);
  }
  quint32 sequence() const
  {
    return m_sequence;
  }
  bool fresh() const
  {
    return m_age.isValid() && m_age.elapsed() < 2000 && !m_failed;
  }
  void refresh()
  {
    if (!m_failed) {
      const int descriptor =
          open(QFile::encodeName(m_framePath).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
      if (descriptor >= 0) {
        QFile file;
        struct stat info{};
        if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
          ::close(descriptor);
          m_failed = true;
        } else if (
            fstat(descriptor, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_nlink != 1 ||
            (info.st_mode & 0077) || info.st_size < 41 || info.st_size > 4194312
        ) {
          m_failed = true;
        } else {
          const auto bytes = file.read(4194313);
          const auto current = bytes.size() >= 8 ? qFromBigEndian<quint32>(bytes.constData() + 4) : 0;
          if (bytes.size() != info.st_size || !bytes.startsWith("DFP1") || current < m_sequence || current > 1350 ||
              !current) {
            m_failed = true;
          } else if (current > m_sequence) {
            QBuffer png;
            png.setData(bytes.mid(8));
            png.open(QIODevice::ReadOnly);
            QImageReader reader(&png, "PNG");
            if (reader.size() != QSize(1280, 720)) {
              m_failed = true;
            } else {
              const auto image = reader.read();
              if (image.isNull()) {
                m_failed = true;
              } else {
                m_pixmap = QPixmap::fromImage(image);
                m_sequence = current;
                m_age.restart();
                if (m_showOnFrame && !isVisible())
                  show(); // Never activate or steal the source's keyboard focus.
              }
            }
          }
        }
      } else if (errno != ENOENT) {
        m_failed = true;
      }
    }
    if (fresh())
      m_image->setPixmap(m_pixmap.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
      m_image->setText("Ubuntu preview unavailable. Return left to the Mac; the test will stop.");
    QSaveFile receipt(m_receiptPath);
    if (receipt.open(QIODevice::WriteOnly)) {
      receipt.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      receipt.write(QJsonDocument(
                        QJsonObject{
                            {"version", 1},
                            {"sequence", qint64(m_sequence)},
                            {"fresh", fresh()},
                            {"visible", isVisible() && !isMinimized() && windowHandle() && windowHandle()->isExposed()},
                            {"viewOnly", true}
                        }
      ).toJson(QJsonDocument::Compact));
      receipt.commit();
    }
  }

private:
  QString m_framePath, m_receiptPath;
  bool m_showOnFrame = true, m_failed = false;
  quint32 m_sequence = 0;
  QLabel *m_image = nullptr;
  QPixmap m_pixmap;
  QElapsedTimer m_age;
  QTimer m_timer;
};
} // namespace deskflow::windowshare
