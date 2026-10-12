/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Transport.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <memory>
using namespace deskflow::windowshare;
namespace {
class Listener : public QTcpServer {
public:
  std::function<void(qintptr)> incoming;
private:
  void incomingConnection(qintptr descriptor) override { incoming(descriptor); }
};
void check(bool condition,const char *message) { if (!condition) throw std::runtime_error(message); }
void exercise(const Identity &host,const Identity &client,QByteArray hostPin,QByteArray clientPin,bool allowed) {
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client,hostPin);
  int packets = 0, closed = 0;
  bool accepted = false,timedOut = false;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host,clientPin);
    server->onClosed = [&](QString) { ++closed; loop.quit(); };
    server->onPacket = [&](Packet packet) {
      ++packets;
      check(packet.type == MessageType::Hello && parseObject(packet)["proof"].toInt() == 37,"authenticated payload mismatch");
      server->send(MessageType::Accepted,{{"proof",73}});
    };
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost,0),"test listener failed");
  peer.onAuthenticated = [&] { peer.send(MessageType::Hello,{{"proof",37}}); };
  peer.onPacket = [&](Packet packet) {
    accepted = packet.type == MessageType::Accepted && parseObject(packet)["proof"].toInt() == 73;
    loop.quit();
  };
  peer.onClosed = [&](QString) { ++closed; loop.quit(); };
  QTimer timeout; timeout.setSingleShot(true);
  QObject::connect(&timeout,&QTimer::timeout,&loop,[&] { timedOut=true; loop.quit(); }); timeout.start(10000);
  peer.connectTo("127.0.0.1",listener.serverPort());
  loop.exec();
  peer.onClosed = {}; peer.onPacket = {}; peer.onAuthenticated = {};
  if (server) { server->onClosed = {}; server->onPacket = {}; }
  check(!timedOut,"TLS test timed out");
  if (allowed) check(accepted && packets == 1,"approved pair did not communicate");
  else check(!accepted && packets == 0 && closed > 0,"unapproved peer reached application protocol");
}
}
int main(int argc,char **argv) {
  QCoreApplication application(argc,argv);
  try {
    QTemporaryDir directory; check(directory.isValid(),"temporary directory unavailable");
    const auto a = QDir(directory.path()).filePath("host"), b = QDir(directory.path()).filePath("client");
    initializeIdentity(a); initializeIdentity(b);
    const auto host = Identity::load(a),client = Identity::load(b);
    const auto hostPin = parseFingerprint(host.fingerprint()), clientPin = parseFingerprint(client.fingerprint());
    exercise(host,client,hostPin,clientPin,true);
    auto bad = hostPin; bad[0] = static_cast<char>(bad[0] ^ 1);
    exercise(host,client,bad,clientPin,false);
    bad = clientPin; bad[0] = static_cast<char>(bad[0] ^ 1);
    exercise(host,client,hostPin,bad,false);
    bool rejected=false;
    try { parseFingerprint("not-a-fingerprint"); } catch (const std::exception &) { rejected=true; }
    check(rejected,"malformed pin accepted");
    rejected=false;
    try { initializeIdentity(a); } catch (const std::exception &) { rejected=true; }
    check(rejected,"existing identity overwritten");
    QTextStream(stdout) << "PASS: approved mutual TLS, wrong server pin, wrong client pin, malformed pin, identity overwrite guard\n";
    return 0;
  } catch (const std::exception &error) { QTextStream(stderr) << "FAIL: " << error.what() << '\n'; return 1; }
}
