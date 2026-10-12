/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2025 - 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParser.h"

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "deskflow/ClientApp.h"
#include "deskflow/ServerApp.h"
#include "deskflow/ipc/CoreIpcServer.h"
#ifdef WINDOWSHARE_CORE_COMPANION
#include "windowshare/LocalInputBootstrap.h"
#include "windowshare/LocalInputCodeAdmission.h"
#include "windowshare/LocalInputSession.h"
#endif

#if defined(Q_OS_WIN)
#include "arch/win32/ArchMiscWindows.h"
#endif

#include <QApplication>
#include <QFileInfo>
#include <QSharedMemory>
#include <QTextStream>
#include <QThread>

void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto utf8 = message.toUtf8();
  switch (type) {
  case QtDebugMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_DEBUG "%s", utf8.constData());
    break;
  case QtInfoMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_INFO "%s", utf8.constData());
    break;
  case QtWarningMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_WARN "%s", utf8.constData());
    break;
  case QtCriticalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_ERR "%s", utf8.constData());
    break;
  case QtFatalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_CRIT "%s", utf8.constData());
    break;
  }
  if (type == QtFatalMsg) {
    abort();
  }
}

void showHelp(const CoreArgParser &parser)
{
  QTextStream(stdout) << parser.helpText();
}

App *createApp(const CoreArgParser &parser, EventQueue &events, const QString &processName)
{
  if (parser.serverMode()) {
    return new ServerApp(&events, processName);
  } else if (parser.clientMode()) {
    return new ClientApp(&events, processName);
  }
  return nullptr;
}

int main(int argc, char **argv)
{
#ifdef WINDOWSHARE_CORE_COMPANION
  using namespace deskflow::windowshare;
  std::optional<LocalInputCoreBootstrap> inputBootstrap;
  try {
    // macOS socketpair CLOEXEC setup must precede Qt/plugin worker startup.
    inputBootstrap.emplace(LocalInputCoreBootstrap::inspect(argc, argv));
  } catch (const std::exception &error) {
    QTextStream(stderr) << "Window sharing: " << error.what() << '\n';
    return s_exitArgs;
  }
#endif
#if defined(Q_OS_WIN)
  ArchMiscWindows::setInstanceWin32(GetModuleHandle(nullptr));
#endif

  QApplication::setApplicationName(QStringLiteral("%1 Core").arg(kAppName));
  QApplication app(argc, argv);

#ifdef WINDOWSHARE_CORE_COMPANION
  if (inputBootstrap->probe)
    return localInputCoreProbe(std::move(*inputBootstrap->pair));
#endif
  Arch arch;
  arch.init();

  Log log;
  qInstallMessageHandler(qtMessageHandler);

  CoreArgParser parser(QCoreApplication::arguments());

  // Print any parser errors
  if (!parser.errorText().isEmpty()) {
    QTextStream(stdout) << parser.errorText() << "\n";
#ifdef WINDOWSHARE_CORE_COMPANION
    if (inputBootstrap->pair)
      return s_exitArgs;
#endif
#ifdef WINDOWSHARE_X11_OBSERVER
    if (parser.windowShareObserveX11())
      return s_exitArgs;
#endif
  }

  if (parser.help()) {
    showHelp(parser);
    return s_exitSuccess;
  }

  if (parser.version()) {
    QTextStream(stdout) << parser.versionText();
    return s_exitSuccess;
  }

  // Before we check any more args we need to check for a duplicate process.
  // Create a shared memory segment with a unique key
  // This is to prevent a new instance from running if one is already running
  QSharedMemory sharedMemory(kCoreBinName);

  // Attempt to attach first and detach in order to clean up stale shm chunks
  // This can happen if the previous instance was killed or crashed
  if (sharedMemory.attach())
    sharedMemory.detach();

  if (!sharedMemory.create(1) && parser.singleInstanceOnly()) {
    LOG_WARN("an instance of deskflow core is already running");
    return s_exitDuplicate;
  }

  parser.parse();

#ifdef WINDOWSHARE_CORE_COMPANION
  // Retain the parsed local policy for the lifetime of this launch. Merely
  // configuring a pair never supplies any of the required input providers.
  std::optional<LocalInputHostConfiguration> inputHostConfiguration;
  std::unique_ptr<LocalInputCoreSession> inputSession;
  // The timer and admission guard are destroyed before the channel owner.
  // The worker holds only independent inspection state, never these objects.
  std::unique_ptr<LocalInputCodeAdmission> inputAdmission;
  QTimer inputAdmissionTimer;
  if (inputBootstrap->pair) {
    try {
      if (parser.windowShareHostConfig() != inputBootstrap->hostConfig || !parser.serverMode())
        throw std::runtime_error("Invalid WindowShare companion invocation");
      inputHostConfiguration.emplace(LocalInputHostConfiguration::load(parser.windowShareHostConfig()));
      QString failure;
      // Native restoration makes several bounded AX calls per window, followed
      // by holding-helper teardown. The short diagnostic grace is insufficient.
      LocalInputCompanionLimits limits;
      limits.gracefulMs = 5000;
      limits.terminateMs = 5000;
      inputSession = LocalInputCoreSession::start(
          std::move(*inputBootstrap->pair), inputHostConfiguration->hostArguments(), &failure, limits
      );
      if (!inputSession) {
        QTextStream(stderr) << "Window sharing: " << failure << '\n';
        return s_exitFailed;
      }
      inputSession->onStopped = [](const QString &reason) {
        LOG_WARN("experimental window host stopped: %s", qPrintable(reason));
      };
      if (!inputHostConfiguration->peerPolicy().empty()) {
        QString admissionError;
        inputAdmission = LocalInputCodeAdmission::request(*inputSession->channel(), &admissionError);
        if (!inputAdmission)
          LOG_WARN("window sharing code admission unavailable: %s", qPrintable(admissionError));
        else {
          QObject::connect(&inputAdmissionTimer, &QTimer::timeout, &inputAdmissionTimer, [&] {
            const auto state = inputAdmission->state();
            if (state == LocalInputCodeAdmission::State::Pending)
              return;
            inputAdmissionTimer.stop();
            if (state == LocalInputCodeAdmission::State::Ready)
              LOG_INFO("window sharing exact library admission verified; original input remains disabled");
            else
              LOG_WARN("window sharing code admission unavailable: %s", qPrintable(inputAdmission->error()));
          });
          inputAdmissionTimer.start(100);
        }
      }
      LOG_INFO("trusted window host connected; original input route remains disabled");
    } catch (const std::exception &error) {
      QTextStream(stderr) << "Window sharing: " << error.what() << '\n';
      return s_exitConfig;
    }
  }
#endif

  EventQueue events;
  const auto processName = QFileInfo(argv[0]).fileName();

  App *coreApp = createApp(parser, events, processName);
#ifdef WINDOWSHARE_X11_OBSERVER
  if (parser.windowShareObserveX11())
    static_cast<ClientApp *>(coreApp)->setWindowShareX11Observation(true);
#endif
#ifdef WINDOWSHARE_CORE_COMPANION
  if (inputSession) {
    // The factory and companion arguments have already required server mode.
    // Configure the value-only mailbox before moving App onto its core thread.
    static_cast<ServerApp *>(coreApp)->setSelectedPeerMailbox(inputSession->selectedPeerMailbox());
    static_cast<ServerApp *>(coreApp)->setOriginalInputCapture(inputSession->captureSource());
  }
#endif

  const auto ipcServer = new deskflow::core::ipc::CoreIpcServer(&app); // NOSONAR - Qt managed
  QObject::connect(
      ipcServer, &deskflow::core::ipc::IpcServer::stopProcessRequested, coreApp, &App::quit, Qt::DirectConnection
  );
  ipcServer->listen();

  QThread coreThread;
#ifdef WINDOWSHARE_CORE_COMPANION
  bool companionDrained = true;
  QObject::connect(&coreThread, &QThread::finished, &app, [&] {
    // Drain the owned host while the Qt event loop can still service it. The
    // companion's aboutToQuit handler is only an emergency last resort.
    if (inputSession)
      companionDrained = inputSession->stop();
    app.quit();
  });
#else
  QObject::connect(&coreThread, &QThread::finished, &app, &QApplication::quit);
#endif
  coreApp->run(coreThread);

  int exitCode = QApplication::exec();
  coreThread.wait();

  if (exitCode == s_exitSuccess) {
    exitCode = coreApp->getExitCode();
  }
  delete coreApp;

#ifdef WINDOWSHARE_CORE_COMPANION
  if (inputSession && (!inputSession->stop() || !companionDrained)) {
    LOG_WARN("experimental window host did not exit before the shutdown deadline");
    if (exitCode == s_exitSuccess)
      exitCode = s_exitFailed;
  }
#endif

  LOG_DEBUG("core exited, code: %d", exitCode);
  return exitCode;
}
