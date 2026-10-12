/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParser.h"
#include "CoreArgs.h"

#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "deskflow/ProtocolTypes.h"

const QString CoreArgParser::s_headerText = QStringLiteral("%1: %2\n").arg(kCoreBinName, kDisplayVersion);

CoreArgParser::CoreArgParser(const QStringList &args)
{
  m_parser.setApplicationDescription(kAppDescription);
  m_parser.addPositionalArgument("coremode", "The mode to start in either: server or client", "coremode");

  m_parser.addOptions(CoreArgs::options);
  m_parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  m_parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
  m_parser.parse(args);

  m_helpText = m_parser.helpText().replace("<executable_name>", kCoreBinName);
  m_helpText.replace("[options] coremode", "coremode [options]");

  m_singleInstance = !m_parser.isSet(CoreArgs::multiInstanceOption);
#ifdef WINDOWSHARE_CORE_COMPANION
  if (m_parser.isSet(CoreArgs::windowShareProbeOption) && (args.size() != 2 || args.at(1) != "--windowshare-ipc-probe"))
    m_error = "Use --windowshare-ipc-probe alone";
  if (m_parser.isSet(CoreArgs::windowShareHostOption)) {
    const auto modes = m_parser.positionalArguments();
    if (modes.size() != 1 || modes.front().compare("server", Qt::CaseInsensitive) != 0 || help() || version() ||
        m_parser.value(CoreArgs::windowShareHostOption).isEmpty())
      m_error = "--windowshare-host-config requires server mode and a configuration file";
  }
#endif
#ifdef WINDOWSHARE_X11_OBSERVER
  if (windowShareObserveX11()) {
    const auto modes = m_parser.positionalArguments();
    if (modes.size() != 1 || modes.front().compare("client", Qt::CaseInsensitive) != 0 || help() || version())
      m_error = "--windowshare-observe-x11 requires client mode";
  }
#endif
#ifdef WINDOWSHARE_GNOME_PROVIDER
  if (windowShareGnomeEnabled()) {
    const auto modes = m_parser.positionalArguments();
    if (modes.size() != 1 || modes.front().compare("client", Qt::CaseInsensitive) != 0 || help() || version() ||
        m_parser.values(CoreArgs::windowShareGnomeOption).size() != 1 || windowShareGnomeConfig().isEmpty())
      m_error = "--windowshare-gnome-config requires client mode and one configuration file";
#ifdef WINDOWSHARE_X11_OBSERVER
    if (windowShareObserveX11())
      m_error = "Choose one WindowShare backend: GNOME or X11 observation";
#endif
  }
#endif
}

void CoreArgParser::parse()
{
  auto posArgs = m_parser.positionalArguments();
  if (posArgs.isEmpty()) {
    showHelpText();
    exit(s_exitSuccess);
  }

  const QString mode = posArgs.takeFirst();
  m_serverMode = (mode.compare("server", Qt::CaseInsensitive) == 0);
  m_clientMode = (mode.compare("client", Qt::CaseInsensitive) == 0);

  if ((!m_clientMode && !m_serverMode) || mode.isEmpty()) {
    showHelpText();
    exit(s_exitSuccess);
  }

  if (m_parser.isSet(CoreArgs::configOption)) {
    Settings::setSettingsFile(m_parser.value(CoreArgs::configOption));
  }
}

[[noreturn]] void CoreArgParser::showHelpText() const
{
  QTextStream(stdout) << helpText();
  exit(s_exitSuccess);
}

QString CoreArgParser::helpText() const
{
  return QStringLiteral("%1%2").arg(s_headerText, m_helpText);
}

QString CoreArgParser::versionText() const
{
  const static auto vString = QStringLiteral("%1 v%2, protocol v%3.%4\n%5\n");
  return vString.arg(
      kCoreBinName, kDisplayVersion, QString::number(kProtocolMajorVersion), QString::number(kProtocolMinorVersion),
      kCopyright
  );
}

QString CoreArgParser::errorText() const
{
  return m_error.isEmpty() ? m_parser.errorText() : m_error;
}

bool CoreArgParser::help() const
{
  return m_parser.isSet(CoreArgs::helpOption);
}

bool CoreArgParser::version() const
{
  return m_parser.isSet(CoreArgs::versionOption);
}

bool CoreArgParser::serverMode() const
{
  return m_serverMode;
}

bool CoreArgParser::clientMode() const
{
  return m_clientMode;
}

bool CoreArgParser::singleInstanceOnly() const
{
  return m_singleInstance;
}

#ifdef WINDOWSHARE_GNOME_PROVIDER
bool CoreArgParser::windowShareGnomeEnabled() const
{
  return m_parser.isSet(CoreArgs::windowShareGnomeOption);
}
QString CoreArgParser::windowShareGnomeConfig() const
{
  return m_parser.value(CoreArgs::windowShareGnomeOption);
}
#endif

#ifdef WINDOWSHARE_CORE_COMPANION
QString CoreArgParser::windowShareHostConfig() const
{
  return m_parser.value(CoreArgs::windowShareHostOption);
}
#endif

#ifdef WINDOWSHARE_X11_OBSERVER
bool CoreArgParser::windowShareObserveX11() const
{
  return m_parser.isSet(CoreArgs::windowShareX11Option);
}
#endif
