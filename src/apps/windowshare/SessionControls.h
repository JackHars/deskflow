/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QJsonObject>
#include <QProcess>
#include <QWidget>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace deskflow::windowshare {

struct SessionControlTools
{
  QString python, script, core, windowShare, profile, role;
};

// This window creates no native input authority. Only an explicit Start invokes
// the profile launcher; its inherited stdin pipe binds session lifetime to us.
class SessionControls final : public QWidget
{
public:
  explicit SessionControls(SessionControlTools tools, QWidget *parent = nullptr);
  ~SessionControls() override;
  static SessionControlTools packagedTools();

protected:
  void closeEvent(QCloseEvent *event) override;
  bool eventFilter(QObject *object, QEvent *event) override;

private:
  void inspect();
  void invoke(const QString &operation, QStringList arguments);
  void finish(int exitCode, QProcess::ExitStatus status);
  void readOutput();
  void saveResult(int exitCode);
  void updateButtons();
  void stop();
  void message(const QString &title, const QString &detail);
  QStringList profileArguments() const;
  bool hasSetupChanges() const;

  SessionControlTools m_tools;
  QProcess m_process;
  QTimer *m_deadline;
  QByteArray m_output;
  QString m_operation, m_pairingFile;
  QJsonObject m_profile, m_support;
  QLineEdit *m_directory, *m_name, *m_address, *m_peerFile;
  QSpinBox *m_screen;
  QCheckBox *m_clipboard;
  QLabel *m_title, *m_detail, *m_pairSummary;
  QGroupBox *m_setupPanel, *m_pairingPanel;
  QPushButton *m_setupToggle;
  QPushButton *m_browseProfile, *m_create, *m_update, *m_openPairing, *m_choosePeer, *m_pair, *m_start, *m_logs;
  QPushButton *m_checkSupport, *m_setupSupport;
  QLabel *m_supportDetail;
  bool m_busy = false, m_stopping = false, m_closing = false, m_outputOverflow = false;
  bool m_showSetup = true;
};

} // namespace deskflow::windowshare
