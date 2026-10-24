/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SessionControls.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace deskflow::windowshare {
namespace {
constexpr qsizetype maximumOutput = 65536;

class MonitorLayout final : public QWidget
{
public:
  explicit MonitorLayout(QWidget *parent) : QWidget(parent)
  {
    setFixedSize(180, 162);
    setAccessibleName("Ubuntu monitor above the Mac. Drag a Mac window upward.");
  }
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto foreground = palette().color(QPalette::Text);
    const auto accent = palette().color(QPalette::Highlight);
    painter.setBrush(palette().color(QPalette::Base));
    painter.setPen(QPen(foreground, 1.5));
    painter.drawRoundedRect(QRectF(15, 6, 150, 56), 5, 5);
    painter.drawRoundedRect(QRectF(35, 102, 110, 48), 5, 5);
    painter.drawLine(26, 155, 154, 155);
    painter.drawText(QRect(15, 6, 150, 56), Qt::AlignCenter, "Ubuntu");
    painter.drawText(QRect(35, 102, 110, 48), Qt::AlignCenter, "Mac");
    painter.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(90, 92, 90, 72);
    painter.drawLine(90, 72, 84, 79);
    painter.drawLine(90, 72, 96, 79);
    painter.drawLine(42, 102, 138, 102);
  }
};

QPushButton *button(const QString &text, const char *name, QWidget *parent)
{
  auto *result = new QPushButton(text, parent);
  result->setObjectName(QLatin1String(name));
  result->setAutoDefault(false);
  return result;
}
} // namespace

SessionControlTools SessionControls::packagedTools()
{
  const auto executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
  const auto directory = QFileInfo(executable).absolutePath();
#ifdef Q_OS_MACOS
  const QString role = "mac", defaultProfile = "DeskflowMac";
  const auto script = QDir(directory).absoluteFilePath("../Resources/seamless-profile.py");
#else
  const QString role = "ubuntu", defaultProfile = "DeskflowUbuntu";
  const auto script =
      QDir(directory).absoluteFilePath("../../share/deskflow-seamless-experimental/seamless-profile.py");
#endif
  return {
      QStandardPaths::findExecutable("python3"),
      QFileInfo(script).canonicalFilePath(),
      QDir(directory).absoluteFilePath("deskflow-core"),
      executable,
      QDir::home().filePath(defaultProfile),
      role
  };
}

SessionControls::SessionControls(SessionControlTools tools, QWidget *parent)
    : QWidget(parent),
      m_tools(std::move(tools)),
      m_deadline(new QTimer(this))
{
  setWindowTitle("Deskflow Window Sharing");
  setMinimumWidth(570);
  resize(700, 670);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 20);
  layout->setSpacing(16);
  auto *heading = new QLabel("Move windows to Ubuntu", this);
  auto font = heading->font();
  font.setPointSize(22);
  font.setWeight(QFont::DemiBold);
  heading->setFont(font);
  layout->addWidget(heading);
  auto *intro = new QHBoxLayout;
  intro->addWidget(new MonitorLayout(this));
  auto *status = new QVBoxLayout;
  m_title = new QLabel("Set up this computer", this);
  m_title->setObjectName("sessionStatus");
  auto titleFont = m_title->font();
  titleFont.setWeight(QFont::DemiBold);
  m_title->setFont(titleFont);
  m_detail = new QLabel(this);
  m_detail->setObjectName("sessionDetail");
  m_detail->setWordWrap(true);
  m_detail->setTextFormat(Qt::PlainText);
  m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  status->addWidget(m_title);
  status->addWidget(m_detail);
  status->addStretch();
  intro->addLayout(status, 1);
  layout->addLayout(intro);

  auto *setup = new QGroupBox("This computer", this);
  m_setupPanel = setup;
  auto *form = new QFormLayout(setup);
  m_directory = new QLineEdit(m_tools.profile, setup);
  m_directory->setObjectName("profileDirectory");
  m_browseProfile = button("Open saved setup…", "openProfile", setup);
  auto *directoryRow = new QHBoxLayout;
  directoryRow->addWidget(m_directory, 1);
  directoryRow->addWidget(m_browseProfile);
  form->addRow("Saved setup", directoryRow);
  m_name = new QLineEdit(m_tools.role == "mac" ? "MacBook" : "Ubuntu", setup);
  m_name->setObjectName("computerName");
  form->addRow("Computer name", m_name);
  auto *createRow = new QHBoxLayout;
  m_create = button("Create setup", "createProfile", setup);
  m_update = button("Use this version", "updateProfile", setup);
  m_openPairing = button("Save pairing file…", "savePairing", setup);
  createRow->addWidget(m_create);
  createRow->addWidget(m_update);
  createRow->addWidget(m_openPairing);
  createRow->addStretch();
  form->addRow(createRow);
  layout->addWidget(setup);

  auto *pairing = new QGroupBox("Connect the two computers", this);
  m_pairingPanel = pairing;
  auto *pairForm = new QFormLayout(pairing);
  auto *hint = new QLabel(
      "Create a setup on each computer. Exchange their pairing files, then choose the other computer’s file here.",
      pairing
  );
  hint->setWordWrap(true);
  pairForm->addRow(hint);
  m_peerFile = new QLineEdit(pairing);
  m_peerFile->setObjectName("peerFile");
  m_peerFile->setReadOnly(true);
  m_choosePeer = button("Choose file…", "choosePeer", pairing);
  auto *peerRow = new QHBoxLayout;
  peerRow->addWidget(m_peerFile, 1);
  peerRow->addWidget(m_choosePeer);
  pairForm->addRow("Other computer", peerRow);
  m_address = new QLineEdit(pairing);
  m_address->setObjectName("macAddress");
  m_address->setPlaceholderText("For example, 192.168.1.101");
  pairForm->addRow("Mac network address", m_address);
  m_screen = new QSpinBox(pairing);
  m_screen->setObjectName("ubuntuMonitor");
  m_screen->setRange(1, 64);
  pairForm->addRow("Ubuntu monitor", m_screen);
  if (m_tools.role == "mac") {
    pairForm->labelForField(m_screen)->hide();
    m_screen->hide();
  }
  m_clipboard = new QCheckBox("Share copied text (enable on both computers)", pairing);
  pairForm->addRow(m_clipboard);
  m_pair = button("Pair computers", "pairComputers", pairing);
  m_pairSummary = new QLabel(pairing);
  m_pairSummary->setWordWrap(true);
  m_pairSummary->setTextFormat(Qt::PlainText);
  auto *pairRow = new QHBoxLayout;
  pairRow->addWidget(m_pair);
  pairRow->addWidget(m_pairSummary, 1);
  pairForm->addRow(pairRow);
  layout->addWidget(pairing);
  const bool mac = m_tools.role == "mac";
  auto *support = new QGroupBox(mac ? "Mac permissions" : "Ubuntu desktop support", this);
  auto *supportLayout = new QVBoxLayout(support);
  m_supportDetail = new QLabel(
      mac ? "Create or open a setup to check Mac permissions." : "Create or open a setup to check Ubuntu support.",
      support
  );
  m_supportDetail->setObjectName("supportDetail");
  m_supportDetail->setWordWrap(true);
  m_supportDetail->setTextFormat(Qt::PlainText);
  supportLayout->addWidget(m_supportDetail);
  auto *supportActions = new QHBoxLayout;
  m_checkSupport = button(mac ? "Check Mac permissions" : "Check Ubuntu support", "checkSupport", support);
  m_setupSupport = button(mac ? "Request permissions" : "Install support", "setupSupport", support);
  supportActions->addWidget(m_checkSupport);
  supportActions->addWidget(m_setupSupport);
  supportActions->addStretch();
  supportLayout->addLayout(supportActions);
  layout->addWidget(support);
  connect(m_checkSupport, &QPushButton::clicked, this, [this] {
    invoke(m_tools.role == "mac" ? "permissions" : "provider", {"--operation", "status"});
  });
  connect(m_setupSupport, &QPushButton::clicked, this, [this] {
    const auto action = m_support.value("action").toString();
    if (m_tools.role == "mac" ? action == "request" : action == "install" || action == "enable")
      invoke(m_tools.role == "mac" ? "permissions" : "provider", {"--operation", action});
  });
  auto *actions = new QHBoxLayout;
  m_setupToggle = button("Show setup", "toggleSetup", this);
  m_logs = button("Open logs", "openLogs", this);
  m_start = button("Start sharing", "startStop", this);
  m_start->setMinimumHeight(36);
  actions->addWidget(m_logs);
  actions->addWidget(m_setupToggle);
  actions->addStretch();
  actions->addWidget(m_start);
  layout->addLayout(actions);
  connect(m_setupToggle, &QPushButton::clicked, this, [this] {
    m_showSetup = !m_showSetup;
    updateButtons();
  });

  connect(m_browseProfile, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(this, "Open saved window-sharing setup", m_directory->text());
    if (!path.isEmpty()) {
      m_directory->setText(path);
      inspect();
    }
  });
  connect(m_directory, &QLineEdit::editingFinished, this, &SessionControls::inspect);
  connect(m_create, &QPushButton::clicked, this, [this] {
    invoke(
        "init",
        {"--role", m_tools.role, "--name", m_name->text(), "--core", m_tools.core, "--windowshare", m_tools.windowShare}
    );
  });
  connect(m_update, &QPushButton::clicked, this, [this] {
    QStringList arguments{"--core", m_tools.core, "--windowshare", m_tools.windowShare};
    if (m_profile.value("paired").toBool()) {
      arguments << "--mac-address" << m_address->text() << "--screen" << QString::number(m_screen->value() - 1);
      arguments << (m_clipboard->isChecked() ? "--clipboard-text" : "--no-clipboard-text");
    }
    invoke("update", arguments);
  });
  connect(m_openPairing, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getSaveFileName(
        this, "Save public pairing file", m_name->text() + "-pairing.json", "Pairing files (*.json)"
    );
    if (path.isEmpty())
      return;
    QSaveFile file(path);
    const auto bytes = QJsonDocument(m_profile.value("pairing").toObject()).toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
      message("Pairing file saved", "Copy this file to the other computer. Your private keys stay here.");
    else
      message("Pairing file was not saved", "Choose a location you can write to.");
  });
  connect(m_choosePeer, &QPushButton::clicked, this, [this] {
    const auto path =
        QFileDialog::getOpenFileName(this, "Choose the other computer’s pairing file", {}, "Pairing files (*.json)");
    if (!path.isEmpty())
      m_peerFile->setText(path);
    updateButtons();
  });
  connect(m_address, &QLineEdit::textChanged, this, [this] { updateButtons(); });
  connect(m_screen, &QSpinBox::valueChanged, this, [this] { updateButtons(); });
  connect(m_clipboard, &QCheckBox::toggled, this, [this] { updateButtons(); });
  connect(m_pair, &QPushButton::clicked, this, [this] {
    QStringList arguments{"--peer",          m_peerFile->text(), "--mac-address",
                          m_address->text(), "--screen",         QString::number(m_screen->value() - 1)};
    if (m_clipboard->isChecked())
      arguments.append("--clipboard-text");
    invoke("pair", arguments);
  });
  connect(m_start, &QPushButton::clicked, this, [this] {
    if (m_busy)
      stop();
    else
      invoke("run", {"--control-stdin"});
  });
  connect(m_logs, &QPushButton::clicked, this, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_directory->text()).filePath("logs")));
  });
  m_process.setProcessChannelMode(QProcess::MergedChannels);
  connect(&m_process, &QProcess::readyReadStandardOutput, this, &SessionControls::readOutput);
  connect(&m_process, &QProcess::finished, this, &SessionControls::finish);
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      finish(-1, QProcess::CrashExit);
  });
  m_deadline->setSingleShot(true);
  connect(m_deadline, &QTimer::timeout, this, [this] {
    // Only setup commands have a deadline here. A live session owns its own
    // bounded shutdown; killing its supervisor could strand the native child.
    if (m_busy && m_operation != "run") {
      m_process.kill();
      message("Setup took too long", "The setup operation was stopped. No sharing session was started.");
    }
  });
  QCoreApplication::instance()->installEventFilter(this);
  inspect();
}

SessionControls::~SessionControls()
{
  QCoreApplication::instance()->removeEventFilter(this);
  m_process.disconnect(this);
  if (m_process.state() != QProcess::NotRunning && m_operation == "run") {
    // Normal Close/Quit is asynchronous below. Also cover application teardown
    // that bypasses a Quit event: do not let QProcess kill the input supervisor
    // before its closed lifetime pipe can initiate native restoration.
    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(45000))
      qCritical("Window sharing supervisor did not finish during application teardown; retain recovery evidence");
  }
}

bool SessionControls::eventFilter(QObject *object, QEvent *event)
{
  if (object == QCoreApplication::instance() && event->type() == QEvent::Quit && m_busy) {
    m_closing = true;
    stop();
    return true;
  }
  return QWidget::eventFilter(object, event);
}

QStringList SessionControls::profileArguments() const
{
  return {"--profile", m_directory->text()};
}

void SessionControls::inspect()
{
  if (m_busy)
    return;
  m_profile = {};
  m_support = {};
  m_supportDetail->setText(
      m_tools.role == "mac" ? "Create or open a setup to check Mac permissions."
                            : "Create or open a setup to check Ubuntu support."
  );
  m_showSetup = true;
  m_pairingFile.clear();
  m_pairSummary->clear();
  if (QFileInfo::exists(QDir(m_directory->text()).filePath("profile.json")))
    invoke("inspect", {"--core", m_tools.core, "--windowshare", m_tools.windowShare});
  else {
    message(
        "Set up this computer",
        "Create its setup, then exchange pairing files. Sharing starts only when you click Start sharing."
    );
    updateButtons();
  }
}

void SessionControls::invoke(const QString &operation, QStringList arguments)
{
  if (m_busy)
    return;
  if (m_tools.python.isEmpty() || !QFileInfo(m_tools.script).isFile() || !QFileInfo(m_tools.core).isExecutable()) {
    message(
        "The experimental package is needed",
        "Open the packaged Window Sharing app and make sure Python 3.9 or later is installed."
    );
    return;
  }
  m_operation = operation;
  m_busy = true;
  m_stopping = false;
  m_outputOverflow = false;
  m_output.clear();
  if (operation == "provider" || operation == "permissions") {
    m_support = {};
    m_supportDetail->setText(
        operation == "permissions" ? "Checking or requesting Mac permissions…" : "Checking or updating Ubuntu support…"
    );
  }
  arguments.prepend(operation);
  arguments.prepend(m_tools.script);
  arguments.prepend("-I");
  arguments.append(profileArguments());
  message(
      operation == "run" ? "Starting sharing…" : "Updating setup…",
      operation == "run" ? "Start sharing on both computers. Once connected, drag a Mac titlebar through the top edge."
                         : "This step does not start keyboard or pointer sharing."
  );
  updateButtons();
  if (operation != "run")
    m_deadline->start(75000);
  m_process.start(m_tools.python, arguments);
}

void SessionControls::readOutput()
{
  const auto bytes = m_process.readAllStandardOutput();
  const auto remaining = maximumOutput - m_output.size();
  m_output.append(bytes.left(remaining));
  if (bytes.size() > remaining) {
    // Preserve a fixed-size diagnostic buffer even if a broken backend floods it.
    m_outputOverflow = true;
    stop();
  }
  if (m_operation == "run" && !m_stopping && m_output.contains("Window-sharing session starting."))
    message(
        "Session started", "Start sharing on the other computer too. Once connected, drag a Mac titlebar upward onto "
                           "Ubuntu. Stop sharing returns shared windows to the Mac."
    );
}

void SessionControls::finish(int exitCode, QProcess::ExitStatus status)
{
  if (!m_busy)
    return;
  readOutput();
  saveResult(exitCode);
  m_deadline->stop();
  const auto operation = m_operation;
  const bool stopped = m_stopping;
  m_busy = false;
  m_stopping = false;
  updateButtons();
  if (m_closing) {
    close();
    return;
  }
  if (exitCode != 0 || status != QProcess::NormalExit || m_outputOverflow) {
    const auto marker = m_output.lastIndexOf("Window sharing:");
    message(
        "Window sharing could not continue", marker >= 0
                                                 ? QString::fromUtf8(m_output.mid(marker + 15, 700)).trimmed()
                                                 : "The session could not continue. Open logs for the failure details."
    );
    return;
  }
  if (operation == "run") {
    message(
        stopped ? "Sharing stopped" : "Sharing session ended",
        "Detailed progress and any restoration errors are saved in the logs."
    );
    return;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(m_output, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    message("Setup returned an invalid result", "Open the logs for details. Sharing has not started.");
    return;
  }
  if (operation != "inspect") {
    if (operation == "provider" || operation == "permissions") {
      const auto value = document.object();
      if (!value.value("ready").isBool() || !value.value("detail").isString() ||
          !value.value("inputStarted").isBool() || value.value("inputStarted").toBool()) {
        message("Setup check returned an invalid result", "Open logs for details. Sharing has not started.");
        return;
      }
      m_support = value;
      m_supportDetail->setText(value.value("detail").toString());
      message(
          value.value("ready").toBool()
              ? (m_profile.value("paired").toBool() ? "Ready to start" : "Ready to pair")
              : (operation == "permissions" ? "Mac permissions need attention" : "Ubuntu support needs attention"),
          value.value("detail").toString()
      );
      updateButtons();
      return;
    }
    inspect();
    return;
  }
  m_profile = document.object();
  if (m_profile.value("role").toString() != m_tools.role) {
    m_profile = {};
    message("Choose this computer’s setup", "This saved setup belongs to the other platform.");
    updateButtons();
    return;
  }
  m_name->setText(m_profile.value("name").toString());
  m_pairingFile = m_profile.value("pairingFile").toString();
  const bool paired = m_profile.value("paired").toBool();
  const bool packageReady = m_profile.value("packageReady").toBool();
  m_showSetup = !paired || !packageReady;
  m_pairSummary->setText(paired ? "Paired with " + m_profile.value("peer").toString() : "Waiting for a pairing file");
  if (paired) {
    const auto options = m_profile.value("options").toObject();
    m_address->setText(options.value("macAddress").toString());
    m_screen->setValue(options.value("screen").toInt() + 1);
    m_clipboard->setChecked(options.value("clipboardText").toBool());
  }
  message(
      !packageReady ? "Update saved setup" : (paired ? "Ready to start" : "Ready to pair"),
      !packageReady
          ? "Click Use this version to keep your pairing with this package. Sharing will stay stopped. " +
                m_profile.value("packageError").toString()
          : (paired ? "Paired with " + m_profile.value("peer").toString() +
                          ". Ubuntu is above the Mac. Stop regular Deskflow on both computers before starting window "
                          "sharing."
                    : "Save this computer’s pairing file and copy it to the other computer. Then choose its file here.")
  );
  updateButtons();
  if (packageReady)
    invoke(m_tools.role == "mac" ? "permissions" : "provider", {"--operation", "status"});
}

void SessionControls::saveResult(int exitCode)
{
  // Only a profile already validated by the helper receives this diagnostic.
  // Retain a bounded history, including failures before Core's log is opened.
  if (m_profile.isEmpty())
    return;
  const auto path = QDir(m_directory->text()).filePath("logs/controls.log");
  QByteArray history;
  QFile previous(path);
  if (previous.open(QIODevice::ReadOnly)) {
    previous.seek(std::max<qint64>(0, previous.size() - 131072));
    history = previous.read(131072);
  }
  history += "\n" + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + " " + m_operation.toUtf8() +
             " exit=" + QByteArray::number(exitCode) + "\n" + m_output + "\n";
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(history) != history.size() || !file.commit())
    qWarning("Window sharing controls could not save its diagnostic log");
}

void SessionControls::stop()
{
  if (!m_busy || m_stopping)
    return;
  m_stopping = true;
  if (m_operation == "run") {
    m_process.closeWriteChannel();
    message(
        "Stopping sharing…",
        "Waiting for the session to release input and restore its windows. Details are saved in the logs."
    );
  } else {
    m_process.kill();
  }
  updateButtons();
}

void SessionControls::closeEvent(QCloseEvent *event)
{
  if (!m_busy) {
    event->accept();
    return;
  }
  m_closing = true;
  stop();
  event->ignore();
}

void SessionControls::message(const QString &title, const QString &detail)
{
  m_title->setText(title);
  m_detail->setText(detail);
}

bool SessionControls::hasSetupChanges() const
{
  if (!m_profile.value("paired").toBool())
    return false;
  const auto options = m_profile.value("options").toObject();
  return m_address->text() != options.value("macAddress").toString() ||
         m_screen->value() != options.value("screen").toInt() + 1 ||
         m_clipboard->isChecked() != options.value("clipboardText").toBool();
}

void SessionControls::updateButtons()
{
  const bool created = !m_profile.isEmpty(), paired = m_profile.value("paired").toBool();
  const bool packageReady = m_profile.value("packageReady").toBool(), changed = hasSetupChanges();
  const bool changeSize = m_setupPanel->isHidden() == m_showSetup;
  m_setupPanel->setVisible(m_showSetup);
  m_pairingPanel->setVisible(m_showSetup);
  m_setupToggle->setVisible(paired);
  m_setupToggle->setEnabled(!m_busy);
  m_setupToggle->setText(m_showSetup ? "Hide setup" : "Show setup");
  if (changeSize)
    QTimer::singleShot(0, this, [this] { resize(width(), sizeHint().height()); });
  m_directory->setEnabled(!m_busy);
  m_browseProfile->setEnabled(!m_busy);
  m_create->setEnabled(!m_busy && !created);
  m_create->setVisible(!created);
  m_update->setVisible(created);
  m_update->setText(packageReady ? "Save changes" : "Use this version");
  m_update->setEnabled(!m_busy && created && (!packageReady || changed) && (!paired || !m_address->text().isEmpty()));
  m_name->setEnabled(!m_busy && !created);
  m_openPairing->setEnabled(!m_busy && created && !m_pairingFile.isEmpty());
  m_choosePeer->setEnabled(!m_busy && created && !paired);
  m_address->setEnabled(!m_busy && created);
  m_screen->setEnabled(!m_busy && created);
  m_clipboard->setEnabled(!m_busy && created);
  m_pair->setEnabled(!m_busy && created && !paired && !m_peerFile->text().isEmpty() && !m_address->text().isEmpty());
  m_start->setText(m_stopping ? "Stopping…" : (m_busy && m_operation == "run" ? "Stop sharing" : "Start sharing"));
  const bool supportReady = m_support.value("ready").toBool();
  m_start->setEnabled(
      !m_stopping && (m_busy ? m_operation == "run" : paired && packageReady && !changed && supportReady)
  );
  m_checkSupport->setEnabled(!m_busy && created && packageReady);
  const auto supportAction = m_support.value("action").toString();
  m_setupSupport->setVisible(
      m_tools.role == "mac" ? supportAction == "request" : supportAction == "install" || supportAction == "enable"
  );
  m_setupSupport->setEnabled(!m_busy && created && packageReady);
  m_setupSupport->setText(
      m_tools.role == "mac" ? "Request permissions" : (supportAction == "enable" ? "Enable support" : "Install support")
  );
  if (paired)
    m_pairSummary->setText(changed ? "Changes not saved" : "Paired with " + m_profile.value("peer").toString());
  m_logs->setEnabled(created);
}
} // namespace deskflow::windowshare
