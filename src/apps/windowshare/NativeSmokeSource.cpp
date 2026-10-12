/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include <QApplication>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSaveFile>
#include <QSet>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

// This process is the only target of the opt-in native smoke test. It records
// event counts (never user text) to a temporary file owned by the test runner.
class Recorder final : public QObject
{
public:
  explicit Recorder(QString path) : m_path(std::move(path))
  {
    save();
  }
  bool eventFilter(QObject *receiver, QEvent *event) override
  {
    QJsonObject observed{{"type", int(event->type())}, {"receiver", receiver->metaObject()->className()}};
    switch (event->type()) {
    case QEvent::KeyPress:
      ++m_keyDown;
      m_keys.insert(static_cast<QKeyEvent *>(event)->key());
      break;
    case QEvent::KeyRelease:
      ++m_keyUp;
      m_keys.remove(static_cast<QKeyEvent *>(event)->key());
      break;
    case QEvent::MouseButtonPress:
      ++m_mouseDown;
      break;
    case QEvent::MouseButtonRelease:
      ++m_mouseUp;
      break;
    case QEvent::Wheel:
      ++m_scroll;
      break;
    default:
      return false;
    }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
      const auto *key = static_cast<QKeyEvent *>(event);
      observed["code"] = key->key();
      observed["scan"] = qint64(key->nativeScanCode());
      observed["virtual"] = qint64(key->nativeVirtualKey());
      observed["modifiers"] = int(key->modifiers());
      observed["repeat"] = key->isAutoRepeat();
    } else if (event->type() == QEvent::Wheel) {
      const auto *wheel = static_cast<QWheelEvent *>(event);
      observed["x"] = wheel->position().x();
      observed["y"] = wheel->position().y();
      observed["dx"] = wheel->pixelDelta().x();
      observed["dy"] = wheel->pixelDelta().y();
    } else {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      observed["x"] = mouse->position().x();
      observed["y"] = mouse->position().y();
      observed["button"] = int(mouse->button());
    }
    if (m_observed.size() < 64)
      m_observed.append(observed);
    save();
    return false;
  }

private:
  void save()
  {
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
      return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(
                   QJsonObject{
                       {"keyDown", m_keyDown},
                       {"keyUp", m_keyUp},
                       {"mouseDown", m_mouseDown},
                       {"mouseUp", m_mouseUp},
                       {"scroll", m_scroll},
                       {"heldKeys", m_keys.size()},
                       {"events", m_observed}
                   }
    ).toJson(QJsonDocument::Compact));
    file.commit();
  }
  QString m_path;
  QSet<int> m_keys;
  QJsonArray m_observed;
  int m_keyDown = 0, m_keyUp = 0, m_mouseDown = 0, m_mouseUp = 0, m_scroll = 0;
};
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (app.arguments().size() != 3)
    return 2;
  Recorder recorder(app.arguments().at(2));
  QWidget window;
  window.setWindowTitle(app.arguments().at(1));
  window.resize(640, 420);
  auto *layout = new QVBoxLayout(&window);
  layout->addWidget(
      new QLabel("Deskflow native capture/input test\nThis temporary window closes automatically.", &window)
  );
  auto *field = new QLineEdit(&window);
  layout->addWidget(field);
  app.installEventFilter(&recorder);
  window.show();
  field->setFocus();
  return app.exec();
}
