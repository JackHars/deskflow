/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

// This is a private-bus protocol/lifetime fixture. Its socketpair is deliberately
// not an EIS server, and its replies are not compositor or input-authority proof.
// The separate isolated GNOME harness tests the real Shell/Mutter provider.
using deskflow::windowshare::GnomeProviderClient;
using deskflow::windowshare::GnomeProviderConfig;
using namespace deskflow::windowshare;
using namespace std::chrono_literals;

namespace {
constexpr auto serviceName = "org.deskflow.Seamless.Gnome50";
constexpr auto objectPath = "/org/deskflow/Seamless/Gnome50";
constexpr auto sessionId = "fixture-session";
constexpr auto viewerId = "fixture-viewer";
constexpr guint generation = 7;
constexpr auto introspection = R"xml(
<node>
  <interface name="org.deskflow.Seamless.Gnome50">
    <method name="GetCapabilities">
      <arg type="u" direction="out"/><arg type="s" direction="out"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="CreateSession">
      <arg type="s" direction="out"/><arg type="u" direction="out"/>
      <arg type="h" direction="out"/>
    </method>
    <method name="LaunchViewerWithViewport">
      <arg type="s" direction="in"/><arg type="u" direction="in"/>
      <arg type="s" direction="in"/><arg type="as" direction="in"/>
      <arg type="h" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="GetViewer">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="ValidateRoutedPoint">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="ValidateCurrentHit">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="CloseSession">
      <arg type="s" direction="in"/><arg type="u" direction="in"/>
    </method>
    <signal name="Closed"><arg type="s"/><arg type="u"/><arg type="s"/></signal>
    <signal name="Invalidated"><arg type="s"/><arg type="u"/><arg type="u"/><arg type="s"/></signal>
  </interface>
</node>)xml";

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct Descriptor
{
  int value = -1;
  explicit Descriptor(int value = -1) : value(value)
  {
  }
  ~Descriptor()
  {
    reset();
  }
  void reset(int replacement = -1)
  {
    if (value >= 0)
      ::close(value);
    value = replacement;
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
};

struct Error
{
  GError *value = nullptr;
  ~Error()
  {
    g_clear_error(&value);
  }
};

template <typename Predicate>
void waitUntil(Predicate predicate, const char *message, std::chrono::milliseconds timeout = 3000ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    // Only the fake service uses the default context. The actual client owns
    // and dispatches its separate context on its production worker thread.
    for (unsigned i = 0; i < 32 && g_main_context_pending(nullptr); ++i)
      g_main_context_iteration(nullptr, false);
    if (predicate())
      return;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(message);
    std::this_thread::sleep_for(1ms);
  }
}

struct PrivateBus
{
  QTemporaryDir directory;
  QProcess daemon;
  QByteArray busAddress;
  QByteArray previousAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");
  bool hadAddress = qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS"), changedEnvironment = false;
  PrivateBus()
  {
    try {
      // Explicitly own/reap the daemon rather than GTestDBus's extra forked
      // watcher (whose EOF path crashes in macOS GLib 2.88 under ASan).
      // No installed configuration or service activation directory is read.
      check(directory.isValid(), "Cannot create private bus directory");
      const auto path = directory.filePath("bus.conf");
      QFile file(path);
      constexpr auto configuration = R"xml(<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>)xml";
      check(
          file.open(QIODevice::WriteOnly) &&
              file.write(configuration) == static_cast<qint64>(std::strlen(configuration)),
          "Cannot write private bus configuration"
      );
      file.close();
      daemon.start("dbus-daemon", {"--nofork", "--print-address=1", "--config-file=" + path});
      check(daemon.waitForStarted(3000), "Cannot start the owned private dbus-daemon");
      const auto deadline = std::chrono::steady_clock::now() + 3000ms;
      while (!daemon.canReadLine()) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        check(
            left.count() > 0 && daemon.waitForReadyRead(static_cast<int>(left.count())),
            "Private bus did not publish its address"
        );
      }
      busAddress = daemon.readLine(1025).trimmed();
      check(busAddress.startsWith("unix:") && busAddress.size() <= 1024, "Private bus returned an invalid address");
      check(qputenv("DBUS_SESSION_BUS_ADDRESS", busAddress), "Cannot select the owned private bus");
      changedEnvironment = true;
    } catch (...) {
      stopDaemon();
      throw;
    }
  }
  ~PrivateBus()
  {
    stopDaemon();
    if (changedEnvironment) {
      if (hadAddress)
        qputenv("DBUS_SESSION_BUS_ADDRESS", previousAddress);
      else
        qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    }
  }
  const char *address() const
  {
    return busAddress.constData();
  }
  void stopDaemon() noexcept
  {
    if (daemon.state() == QProcess::NotRunning)
      return;
    daemon.terminate();
    if (!daemon.waitForFinished(1000)) {
      daemon.kill();
      daemon.waitForFinished(1000);
    }
  }
};

struct Wake
{
  std::atomic<unsigned> count = 0;
  unsigned consumed = 0;
  void changed() noexcept
  {
    count.fetch_add(1, std::memory_order_release);
  }
  GnomeProviderClient::Update next(GnomeProviderClient &client)
  {
    waitUntil([&] { return count.load(std::memory_order_acquire) != consumed; }, "Client notification timed out");
    consumed = count.load(std::memory_order_acquire);
    return client.takeUpdate();
  }
};

struct Configuration
{
  QTemporaryDir directory;
  GnomeProviderConfig load()
  {
    check(directory.isValid(), "Cannot create private fixture configuration directory");
    const auto executable = directory.filePath("never-executed-viewer");
    QFile viewer(executable);
    check(
        viewer.open(QIODevice::WriteOnly) && viewer.write("fixture, never executed\n") > 0, "Cannot write fixture file"
    );
    viewer.close();
    check(
        viewer.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
        "Cannot set fixture executable mode"
    );
    const QJsonObject object{
        {"version", 1},
        {"viewerExecutable", executable},
        {"connect", "192.0.2.1"},
        {"identity", directory.filePath("identity")},
        {"peerPin", QString(64, 'a')}
    };
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto path = directory.filePath("provider.json");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write fixture configuration");
    file.close();
    return GnomeProviderConfig::load(path);
  }
};

struct Service
{
  enum class Mode
  {
    Normal,
    OwnerMismatch,
    CloseDuringSetup,
    DelayedCreate
  };

  GDBusConnection *connection = nullptr, *otherOwner = nullptr;
  GDBusNodeInfo *description = nullptr;
  GDBusMethodInvocation *pendingCreate = nullptr;
  guint registration = 0, nameWatch = 0, viewportTimer = 0, revision = 1;
  Descriptor peer, viewportPeer;
  GnomeViewportDecoder viewportDecoder;
  std::uint64_t paintSnapshot = 0;
  bool delaySnapshot = false, wrongPeer = false, invalidateOnCheck = false, wrongRoutedEcho = false;
  // Simulate compositor changes without a delivered Invalidated signal while
  // Qt's asynchronous Check is pending. These are real D-Bus fixture replies,
  // not native authorization or a fabricated event-drain receipt.
  bool changeNativeRevisionAfterCheck = false, changeNativeTransformAfterCheck = false;
  bool afterQtCheck = false;
  unsigned nativeHitQueries = 0;
  bool advertiseRoutedPoint = true;
  QByteArray delayedReply;
  std::chrono::steady_clock::time_point delayedUntil;
  Mode mode;
  std::string caller, failure;
  unsigned capabilities = 0, creates = 0, launches = 0, closes = 0;
  bool callerGone = false;

  Service(const PrivateBus &bus, Mode mode = Mode::Normal) : mode(mode)
  {
    try {
      connection = connect(bus.address());
      own(connection, serviceName);
      if (mode == Mode::OwnerMismatch) {
        otherOwner = connect(bus.address());
        own(otherOwner, "org.gnome.Shell");
      } else {
        own(connection, "org.gnome.Shell");
      }
      Error error;
      description = g_dbus_node_info_new_for_xml(introspection, &error.value);
      check(description, "Invalid fixture D-Bus introspection");
      static const GDBusInterfaceVTable vtable{method, nullptr, nullptr, {nullptr}};
      registration = g_dbus_connection_register_object(
          connection, objectPath, description->interfaces[0], &vtable, this, nullptr, &error.value
      );
      check(registration, "Cannot register private fixture service");
      nameWatch = g_dbus_connection_signal_subscribe(
          connection, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
          nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
          [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
             gpointer data) {
            auto &self = *static_cast<Service *>(data);
            const char *name = nullptr, *before = nullptr, *after = nullptr;
            g_variant_get(parameters, "(&s&s&s)", &name, &before, &after);
            if (!self.caller.empty() && self.caller == name && !*after) {
              self.callerGone = true;
              self.peer.reset();
            }
          },
          this, nullptr
      );
    } catch (...) {
      cleanup();
      throw;
    }
  }
  ~Service()
  {
    cleanup();
  }
  Service(const Service &) = delete;
  Service &operator=(const Service &) = delete;

  static GDBusConnection *connect(const char *address)
  {
    Error error;
    auto *result = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION
        ),
        nullptr, nullptr, &error.value
    );
    check(result, "Cannot connect to private fixture bus");
    g_dbus_connection_set_exit_on_close(result, false);
    check(
        g_dbus_connection_get_capabilities(result) & G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING,
        "Private bus does not support Unix descriptor passing"
    );
    return result;
  }
  static void own(GDBusConnection *connection, const char *name)
  {
    Error error;
    auto *reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", name, 4u), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error.value
    );
    check(reply, "Cannot own fixture bus name");
    guint result = 0;
    g_variant_get(reply, "(u)", &result);
    g_variant_unref(reply);
    check(result == 1, "Fixture bus name was already owned");
  }
  void loseProviderName()
  {
    Error error;
    auto *reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName",
        g_variant_new("(s)", serviceName), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error.value
    );
    check(reply, "Cannot release fixture provider name");
    guint result = 0;
    g_variant_get(reply, "(u)", &result);
    g_variant_unref(reply);
    check(result == 1, "Fixture did not own provider name before release");
  }
  void created(GDBusMethodInvocation *invocation)
  {
    int descriptors[2]{-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0, "Cannot create fixture socketpair");
    Descriptor delivered(descriptors[0]);
    peer.reset(descriptors[1]);
    check(
        ::fcntl(delivered.value, F_SETFD, FD_CLOEXEC) == 0 && ::fcntl(peer.value, F_SETFD, FD_CLOEXEC) == 0,
        "Cannot make fixture descriptors close-on-exec"
    );
    Error error;
    auto *list = g_unix_fd_list_new();
    const auto handle = g_unix_fd_list_append(list, delivered.value, &error.value);
    if (handle != 0) {
      g_object_unref(list);
      throw std::runtime_error("Cannot append fixture descriptor");
    }
    g_dbus_method_invocation_return_value_with_unix_fd_list(
        invocation, g_variant_new("(suh)", sessionId, generation, handle), list
    );
    g_object_unref(list);
  }
  void replyLate()
  {
    check(pendingCreate, "Fixture has no delayed request");
    auto *invocation = std::exchange(pendingCreate, nullptr);
    try {
      created(invocation);
    } catch (...) {
      g_object_unref(invocation);
      throw;
    }
    g_object_unref(invocation);
  }
  void sendViewport(const GnomeViewportReply &reply)
  {
    const auto bytes = gnomeViewportFrame(gnomeViewportJson(reply));
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    check(
        ::send(viewportPeer.value, bytes.constData(), static_cast<std::size_t>(bytes.size()), flags) == bytes.size(),
        "Cannot send fixture viewport reply"
    );
  }
  void invalidatePaint()
  {
    check(paintSnapshot != 0, "No fixture painted snapshot to invalidate");
    GnomeViewportReply reply;
    reply.operation = GnomeViewportOperation::Invalidated;
    reply.snapshot = paintSnapshot;
    reply.reason = "fixture-paint-changed";
    sendViewport(reply);
    paintSnapshot = 0;
  }
  void invalidateNative()
  {
    Error error;
    check(
        g_dbus_connection_emit_signal(
            connection, caller.c_str(), objectPath, serviceName, "Invalidated",
            g_variant_new("(suus)", sessionId, generation, ++revision, "fixture-focus-changed"), &error.value
        ),
        "Cannot invalidate fixture native observation"
    );
  }
  void viewportTick()
  {
    if (!delayedReply.isEmpty() && std::chrono::steady_clock::now() >= delayedUntil) {
      int flags = 0;
#ifdef MSG_NOSIGNAL
      flags = MSG_NOSIGNAL;
#endif
      check(
          ::send(viewportPeer.value, delayedReply.constData(), static_cast<std::size_t>(delayedReply.size()), flags) ==
              delayedReply.size(),
          "Cannot send deliberately late viewport reply"
      );
      delayedReply.clear();
    }
    char buffer[4096];
    const auto count = ::recv(viewportPeer.value, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count > 0)
      viewportDecoder.append(QByteArray(buffer, static_cast<qsizetype>(count)));
    for (unsigned i = 0; i < 8; ++i) {
      const auto object = viewportDecoder.take();
      if (!object)
        return;
      const auto request = parseGnomeViewportRequest(*object);
      GnomeViewportReply reply;
      reply.operation = request.operation;
      reply.request = request.request;
      reply.ok = true;
      if (request.operation == GnomeViewportOperation::Snapshot) {
        paintSnapshot = request.request;
        auto &state = reply.state;
        state.connection = 1;
        state.session = 11;
        state.epoch = 12;
        state.geometry = 3;
        state.focusRequest = 4;
        state.focusRevision = 5;
        state.surfaceIncarnation = 1;
        state.viewportRevision = 2;
        state.peerFingerprint = QString(64, wrongPeer ? 'b' : 'a');
        state.nativeSurface = 9;
        state.clientWidth = state.surfaceWidth = state.videoWidth = 800;
        state.clientHeight = state.surfaceHeight = state.videoHeight = 600;
        state.sourceWidth = state.imageWidth = 640;
        state.sourceHeight = state.imageHeight = 480;
        state.dpr = state.logicalScale = 1;
        if (std::exchange(delaySnapshot, false)) {
          delayedReply = gnomeViewportFrame(gnomeViewportJson(reply));
          delayedUntil = std::chrono::steady_clock::now() + 350ms;
          continue;
        }
      } else {
        reply.snapshot = request.snapshot;
        if (request.snapshot != paintSnapshot) {
          reply.ok = false;
          reply.reason = "fixture-stale-snapshot";
        } else if (request.operation == GnomeViewportOperation::Map) {
          reply.x = request.x * 0.8;
          reply.y = request.y * 0.8;
        } else if (request.operation == GnomeViewportOperation::Check) {
          // A synchronous Qt Check acknowledgement is not evidence that
          // GNOME remained stable while the Qt default queue was processed.
          afterQtCheck = true;
          if (std::exchange(changeNativeRevisionAfterCheck, false))
            ++revision; // Deliberately emit NO native invalidation signal.
          if (std::exchange(invalidateOnCheck, false))
            invalidatePaint();
        }
      }
      sendViewport(reply);
    }
    throw std::runtime_error("Fixture viewport requests exceeded bound");
  }
  void handle(const char *name, GVariant *parameters, GDBusMethodInvocation *invocation)
  {
    if (!std::strcmp(name, "GetCapabilities")) {
      ++capabilities;
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      for (const auto *key : {"exactWaylandClientBinding", "eisSessionOwned", "nativeHitTesting", "viewportChannel"})
        g_variant_builder_add(&values, "{sv}", key, g_variant_new_boolean(true));
      if (advertiseRoutedPoint)
        g_variant_builder_add(&values, "{sv}", "routedPointObservation", g_variant_new_boolean(true));
      g_variant_builder_add(&values, "{sv}", "backend", g_variant_new_string("MetaBackendNative"));
      g_variant_builder_add(&values, "{sv}", "deliveryReady", g_variant_new_boolean(false));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(usa{sv})", 1u, "50.1", &values));
    } else if (!std::strcmp(name, "CreateSession")) {
      check(creates++ == 0, "Client repeated CreateSession");
      caller = g_dbus_method_invocation_get_sender(invocation);
      if (mode == Mode::DelayedCreate)
        pendingCreate = G_DBUS_METHOD_INVOCATION(g_object_ref(invocation));
      else
        created(invocation);
    } else if (!std::strcmp(name, "LaunchViewerWithViewport")) {
      ++launches;
      const char *id = nullptr, *executable = nullptr;
      guint value = 0;
      GVariant *arguments = nullptr;
      gint handle = -1;
      g_variant_get(parameters, "(&su&s@ash)", &id, &value, &executable, &arguments, &handle);
      const bool valid = std::strcmp(id, sessionId) == 0 && value == generation && *executable == '/' &&
                         g_variant_n_children(arguments) > 0 &&
                         caller == g_dbus_method_invocation_get_sender(invocation);
      g_variant_unref(arguments);
      check(valid, "Client changed session or sent invalid viewer arguments");
      auto *message = g_dbus_method_invocation_get_message(invocation);
      auto *descriptors = g_dbus_message_get_unix_fd_list(message);
      check(
          descriptors && g_unix_fd_list_get_length(descriptors) == 1 && handle == 0, "Invalid viewport descriptor list"
      );
      Error descriptorError;
      viewportPeer.reset(g_unix_fd_list_get(descriptors, handle, &descriptorError.value));
      check(viewportPeer.value >= 0, "Cannot receive private viewport descriptor");
#ifdef SO_NOSIGPIPE
      const int noSigpipe = 1;
      check(
          ::setsockopt(viewportPeer.value, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe)) == 0,
          "Cannot protect fixture writes"
      );
#endif
      viewportTimer = g_timeout_add(
          1,
          [](gpointer data) -> gboolean {
            auto &self = *static_cast<Service *>(data);
            try {
              self.viewportTick();
              return G_SOURCE_CONTINUE;
            } catch (const std::exception &error) {
              self.failure = error.what();
              self.viewportTimer = 0;
              return G_SOURCE_REMOVE;
            }
          },
          this
      );
      if (mode == Mode::CloseDuringSetup) {
        Error error;
        check(
            g_dbus_connection_emit_signal(
                connection, caller.c_str(), objectPath, serviceName, "Closed",
                g_variant_new("(sus)", sessionId, generation, "closed-before-launch-reply"), &error.value
            ),
            "Cannot emit fixture close during setup"
        );
      }
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", viewerId));
    } else if (!std::strcmp(name, "GetViewer") || !std::strcmp(name, "ValidateCurrentHit") ||
               !std::strcmp(name, "ValidateRoutedPoint")) {
      check(caller == g_dbus_method_invocation_get_sender(invocation), "Viewport query changed private caller");
      const char *id = nullptr, *token = nullptr, *request = nullptr;
      guint value = 0, expectedRevision = revision;
      double x = 400, y = 300;
      const bool routed = !std::strcmp(name, "ValidateRoutedPoint");
      const bool hit = routed || !std::strcmp(name, "ValidateCurrentHit");
      if (hit) {
        ++nativeHitQueries;
        g_variant_get(parameters, "(&su&s&sddu)", &id, &value, &token, &request, &x, &y, &expectedRevision);
      }
      else
        g_variant_get(parameters, "(&su&s)", &id, &value, &token);
      check(
          id && token && std::strcmp(id, sessionId) == 0 && value == generation && std::strcmp(token, viewerId) == 0,
          "Native viewport query changed binding"
      );
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      const auto add = [&](const char *key, GVariant *value) { g_variant_builder_add(&values, "{sv}", key, value); };
      add("session", g_variant_new_string(sessionId));
      add("viewer", g_variant_new_string(viewerId));
      add("generation", g_variant_new_uint32(generation));
      add("revision", g_variant_new_uint32(revision));
      add("window", g_variant_new_string("fixture-window"));
      add("surface", g_variant_new_string("fixture-surface"));
      add("focused", g_variant_new_boolean(true));
      add("available", g_variant_new_boolean(
                           expectedRevision == revision &&
                           (routed ? x >= 400 && x <= 450 && y >= 300 && y <= 350 : x == 400 && y == 300)
                       ));
      add("reason", g_variant_new_string("fixture-native-denied"));
      add("observationOnly", g_variant_new_boolean(true));
      add("deliveryReady", g_variant_new_boolean(false));
      add("pointerX", g_variant_new_double(400));
      add("pointerY", g_variant_new_double(300));
      if (hit) {
        add("request", g_variant_new_string(request));
        // Change the local transform *after* the Qt Check returned while
        // retaining surface tokens/revision. A mere token comparison cannot
        // detect this otherwise silent geometry drift.
        const double drift = afterQtCheck && changeNativeTransformAfterCheck ? 1.0 : 0.0;
        add("localX", g_variant_new_double((routed ? x - 100 : 300) + drift));
        add("localY", g_variant_new_double(routed ? y - 100 : 200));
        if (routed) {
          add("routedX", g_variant_new_double(wrongRoutedEcho ? x + 1 : x));
          add("routedY", g_variant_new_double(y));
        }
      }
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &values));
    } else if (!std::strcmp(name, "CloseSession")) {
      ++closes;
      const char *id = nullptr;
      guint value = 0;
      g_variant_get(parameters, "(&su)", &id, &value);
      check(
          std::strcmp(id, sessionId) == 0 && value == generation &&
              caller == g_dbus_method_invocation_get_sender(invocation),
          "Client closed a different session"
      );
      peer.reset();
      if (viewportTimer)
        g_source_remove(std::exchange(viewportTimer, 0));
      viewportPeer.reset();
      delayedReply.clear();
      g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    } else {
      throw std::runtime_error("Unexpected client method");
    }
  }
  static void method(
      GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *name, GVariant *parameters,
      GDBusMethodInvocation *invocation, gpointer data
  ) noexcept
  {
    auto &self = *static_cast<Service *>(data);
    try {
      self.handle(name, parameters, invocation);
    } catch (const std::exception &error) {
      self.failure = error.what();
      g_dbus_method_invocation_return_dbus_error(invocation, "org.deskflow.Test.FixtureFailure", error.what());
    }
  }
  void cleanup() noexcept
  {
    if (viewportTimer)
      g_source_remove(std::exchange(viewportTimer, 0));
    viewportPeer.reset();
    if (pendingCreate) {
      g_dbus_method_invocation_return_dbus_error(pendingCreate, "org.deskflow.Test.Ended", "Fixture ended");
      g_object_unref(std::exchange(pendingCreate, nullptr));
    }
    peer.reset();
    if (connection) {
      if (nameWatch)
        g_dbus_connection_signal_unsubscribe(connection, nameWatch);
      if (registration)
        g_dbus_connection_unregister_object(connection, registration);
      g_dbus_connection_close_sync(connection, nullptr, nullptr);
      g_object_unref(std::exchange(connection, nullptr));
    }
    if (otherOwner) {
      g_dbus_connection_close_sync(otherOwner, nullptr, nullptr);
      g_object_unref(std::exchange(otherOwner, nullptr));
    }
    if (description)
      g_dbus_node_info_unref(std::exchange(description, nullptr));
  }
};

void ownerMismatch(const PrivateBus &bus)
{
  Service service(bus, Service::Mode::OwnerMismatch);
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto update = wake.next(client);
  Descriptor descriptor(update.descriptor);
  check(update.closed && !update.ready && !client.live(), "Mismatched Shell owner was accepted");
  check(descriptor.value == -1 && service.creates == 0 && service.capabilities == 0, "Owner mismatch reached setup");
  check(update.error.find("not owned") != std::string::npos, "Owner mismatch lost its diagnostic");
  check(service.failure.empty(), "Private service failed");
}

void closeDuringSetup(const PrivateBus &bus)
{
  Service service(bus, Service::Mode::CloseDuringSetup);
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto update = wake.next(client);
  Descriptor descriptor(update.descriptor);
  check(update.closed && !update.ready && !client.live(), "Queued setup close was overwritten by Ready");
  check(descriptor.value == -1, "Setup close leaked the unadopted descriptor to the consumer");
  waitUntil([&] { return service.callerGone; }, "Setup close did not retire the private client connection");
  const auto final = client.takeUpdate();
  Descriptor finalDescriptor(final.descriptor);
  check(!final.ready && final.closed && !client.live() && finalDescriptor.value == -1, "Setup close later revived");
  check(
      service.creates == 1 && service.launches == 1 && service.closes == 1, "Setup close did not clean up exactly once"
  );
  check(service.failure.empty(), "Private service failed");
}

void transferredDescriptor(const PrivateBus &bus)
{
  Service service(bus);
  Configuration configuration;
  Wake wake;
  auto client = std::make_unique<GnomeProviderClient>(configuration.load(), [&] { wake.changed(); });
  const auto update = wake.next(*client);
  Descriptor descriptor(update.descriptor);
  check(update.ready && !update.closed && client->live() && descriptor.value >= 0, "Client did not become Ready");
  check(
      update.session == sessionId && update.viewer == viewerId && update.generation == generation,
      "Ready changed the provider's session identity"
  );
  check((::fcntl(descriptor.value, F_GETFD) & FD_CLOEXEC) != 0, "Transferred descriptor is inheritable");
  check(::send(service.peer.value, "x", 1, 0) == 1, "Cannot write fixture socket peer");
  char byte = 0;
  waitUntil(
      [&] { return ::recv(descriptor.value, &byte, 1, MSG_DONTWAIT) == 1; },
      "Transferred descriptor is not the peer socket"
  );
  check(byte == 'x', "Transferred descriptor corrupted data");
  const auto second = client->takeUpdate();
  Descriptor duplicate(second.descriptor);
  check(duplicate.value == -1, "Descriptor ownership was transferred twice");
  client->stop();
  client->stop();
  check(!client->live(), "stop() did not revoke Ready immediately");
  waitUntil([&] { return service.callerGone; }, "stop() did not close the private caller connection");
  client.reset();
  check(::fcntl(descriptor.value, F_GETFD) >= 0, "Client destruction closed the consumer-owned descriptor");
  waitUntil(
      [&] { return ::recv(descriptor.value, &byte, 1, MSG_DONTWAIT) == 0; },
      "Session stop did not close its socket peer"
  );
  check(service.closes == 1 && service.creates == 1 && service.launches == 1, "stop() repeated setup or cleanup");
  check(service.failure.empty(), "Private service failed");
}

void ownerLossAfterReady(const PrivateBus &bus)
{
  Service service(bus);
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  check(ready.ready && client.live() && descriptor.value >= 0, "Owner-loss fixture did not become Ready");
  service.loseProviderName();
  const auto closed = wake.next(client);
  Descriptor duplicate(closed.descriptor);
  check(
      closed.closed && !closed.ready && !client.live() && duplicate.value == -1,
      "Provider owner loss did not revoke Ready"
  );
  waitUntil([&] { return service.callerGone; }, "Owner loss did not close the original caller connection");
  check(::fcntl(descriptor.value, F_GETFD) >= 0, "Owner loss closed the consumer-owned descriptor");
  char byte = 0;
  check(::recv(descriptor.value, &byte, 1, MSG_DONTWAIT) == 0, "Owner loss left the session socket peer open");
  Service::own(service.connection, serviceName);
  const auto final = client.takeUpdate();
  Descriptor finalDescriptor(final.descriptor);
  check(
      !final.ready && final.closed && !client.live() && finalDescriptor.value == -1,
      "Provider reappearance revived a terminal client"
  );
  check(
      service.creates == 1 && service.launches == 1 && service.closes == 1, "Owner loss replayed setup or lost cleanup"
  );
  check(service.failure.empty(), "Private service failed");
}

void stopDuringDelayedSetup(const PrivateBus &bus)
{
  Service service(bus, Service::Mode::DelayedCreate);
  Configuration configuration;
  Wake wake;
  auto client = std::make_unique<GnomeProviderClient>(configuration.load(), [&] { wake.changed(); });
  waitUntil([&] { return service.pendingCreate != nullptr; }, "Client did not reach delayed CreateSession");
  const auto start = std::chrono::steady_clock::now();
  client->stop();
  check(!client->live(), "Stopped setup client became live");
  waitUntil([&] { return service.callerGone; }, "Canceled setup retained its private connection", 2000ms);
  const auto update = wake.next(*client);
  Descriptor descriptor(update.descriptor);
  check(update.closed && !update.ready && descriptor.value == -1, "Canceled setup published a descriptor");
  service.replyLate();
  check(g_dbus_connection_flush_sync(service.connection, nullptr, nullptr), "Cannot flush deliberately late reply");
  client.reset();
  check(std::chrono::steady_clock::now() - start < 2000ms, "Canceled setup waited for the normal method timeout");
  check(
      service.creates == 1 && service.launches == 0 && service.closes == 0,
      "Late creation reply restarted or closed an unknown session"
  );
  check(service.failure.empty(), "Private service failed");
}
GnomeProviderClient::ViewportUpdate viewportResult(GnomeProviderClient &client, std::uint64_t request)
{
  std::optional<GnomeProviderClient::ViewportUpdate> result;
  waitUntil(
      [&] {
        auto value = client.takeViewportUpdate();
        if (value) {
          check(value->request == request, "Viewport result changed request identity");
          result = std::move(value);
        }
        return result.has_value();
      },
      "Viewport result timed out"
  );
  return std::move(*result);
}
GnomeProviderClient::ViewportObservation observed(GnomeProviderClient &client)
{
  const auto request = client.requestViewportObservation();
  check(request != 0, "Client refused an idle viewport request");
  check(client.requestViewportObservation() == 0, "Client queued a second outstanding viewport request");
  const auto result = viewportResult(client, request);
  if (!result.observation || !result.observation->live())
    throw std::runtime_error("No live viewport observation: " + result.error);
  return *result.observation;
}
void viewportJoinAndRevocation(const PrivateBus &bus)
{
  Service service(bus);
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  check(ready.ready && descriptor.value >= 0, "Viewport fixture did not become Ready");
  const auto first = observed(client);
  const auto &snapshot = first.snapshot();
  check(
      snapshot.providerOwner == g_dbus_connection_get_unique_name(service.connection) &&
          snapshot.session == sessionId && snapshot.viewer == viewerId && snapshot.generation == generation &&
          snapshot.window == "fixture-window" && snapshot.surface == "fixture-surface" &&
          snapshot.nativeRevision == service.revision && snapshot.desktopX == 400 && snapshot.desktopY == 300 &&
          snapshot.surfaceX == 300 && snapshot.surfaceY == 200 && snapshot.sourceX == 240 && snapshot.sourceY == 160 &&
          snapshot.paint.session == 11 && snapshot.paint.epoch == 12 && snapshot.paint.geometry == 3,
      "Viewport join changed native, channel or paint identities"
  );
  service.invalidateNative();
  waitUntil([&] { return !first.live(); }, "Native invalidation did not revoke retained viewport", 100ms);
  const auto nativeClosed = client.takeViewportUpdate();
  check(
      nativeClosed && !nativeClosed->observation && nativeClosed->request == snapshot.request,
      "Native revocation was not published"
  );
  const auto second = observed(client);
  service.invalidatePaint();
  waitUntil([&] { return !second.live(); }, "Child invalidation did not revoke retained viewport", 100ms);
  const auto paintClosed = client.takeViewportUpdate();
  check(paintClosed && !paintClosed->observation, "Child revocation was not published");
  const auto third = observed(client);
  client.stop();
  check(!third.live(), "stop() did not immediately revoke retained viewport");
  waitUntil([&] { return service.callerGone; }, "Viewport stop did not retire its provider caller");
  check(service.failure.empty(), "Viewport service failed");
}
void routedPointUsesFrozenCoordinate(const PrivateBus &bus)
{
  Service service(bus);
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  check(ready.ready && descriptor.value >= 0, "Routed point fixture did not become Ready");
  check(
      client.requestRoutedPointObservation({0, 4, 425, 325}) == 0 &&
          client.requestRoutedPointObservation({3, 0, 425, 325}) == 0 &&
          client.requestRoutedPointObservation({3, 4, std::numeric_limits<double>::infinity(), 325}) == 0 &&
          client.requestRoutedPointObservation({3, 4, 10000001, 325}) == 0 &&
          client.requestRoutedPointObservation({3, 4, 0.1, 325}) == 0,
      "Invalid routed point acquired a provider request"
  );
  const GnomeProviderClient::RoutedPoint route{3, 4, 425, 325};
  const auto request = client.requestRoutedPointObservation(route);
  check(request != 0, "Exact routed point was refused");
  check(
      client.requestViewportObservation() == 0 && client.requestRoutedPointObservation(route) == 0,
      "Routed-point preparation failed to reserve the single outstanding slot"
  );
  const auto result = viewportResult(client, request);
  check(result.observation && result.observation->live(), "Exact routed-point join failed");
  const auto &snapshot = result.observation->snapshot();
  check(
      snapshot.routedPoint == route && snapshot.desktopX == route.desktopX && snapshot.desktopY == route.desktopY &&
          snapshot.surfaceX == 325 && snapshot.surfaceY == 225 && snapshot.sourceX == 260 &&
          snapshot.sourceY == 180 && snapshot.nativeRevision == service.revision,
      "Provider substituted its current cursor for the frozen Server point"
  );
  check(
      snapshot.desktopX != 400 && snapshot.desktopY != 300,
      "Provider reported the diagnostic cursor as routed coordinates"
  );
  const auto deniedId = client.requestRoutedPointObservation({3, 5, 900, 900});
  check(deniedId != 0 && !result.observation->live(), "New request did not revoke old routed observation");
  const auto denied = viewportResult(client, deniedId);
  check(!denied.observation && denied.error == "fixture-native-denied" && client.live(),
        "Point outside the native viewer hit was accepted or unexpectedly ended its session");
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Routed-point fixture did not retire provider caller");
  check(service.failure.empty(), "Routed-point fixture service failed");
}
void routedPointCapabilityIsRequired(const PrivateBus &bus)
{
  Service service(bus);
  service.advertiseRoutedPoint = false;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  check(ready.ready && descriptor.value >= 0, "Old provider fixture did not become Ready");
  check(
      client.requestRoutedPointObservation({3, 4, 425, 325}) == 0,
      "Old provider silently negotiated exact routed-point observations"
  );
  check(observed(client).live(), "Optional routed capability denied ordinary current-pointer observation");
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Old provider fixture did not retire caller");
  check(service.failure.empty(), "Old provider fixture service failed");
}
void routedPointWrongEchoIsTerminal(const PrivateBus &bus)
{
  Service service(bus);
  service.wrongRoutedEcho = true;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  check(ready.ready && descriptor.value >= 0, "Wrong-echo fixture did not become Ready");
  const auto request = client.requestRoutedPointObservation({7, 8, 425, 325});
  check(request != 0, "Wrong-echo fixture did not send a request");
  const auto denied = viewportResult(client, request);
  check(!denied.observation && denied.error.find("substituted") != std::string::npos,
        "Mismatched native point echo became a viewport observation");
  waitUntil([&] { return !client.live(); }, "Malformed native reply did not close the provider session");
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Wrong-echo fixture did not retire caller");
  check(service.failure.empty(), "Wrong-echo fixture service failed");
}
void viewportTimeoutAndLateReply(const PrivateBus &bus)
{
  Service service(bus);
  service.delaySnapshot = true;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  const auto request = client.requestViewportObservation();
  check(request != 0, "Client refused initial timeout request");
  const auto denied = viewportResult(client, request);
  check(
      !denied.observation && denied.error == "viewport-timeout" && client.live(),
      "Timed-out viewport became authority or ended an otherwise valid session"
  );
  const auto fresh = observed(client);
  waitUntil([&] { return service.delayedReply.isEmpty(); }, "Fixture did not send its deliberately late reply");
  check(fresh.live() && client.live(), "Late retired reply revoked the new viewport or revived the old request");
  check(!client.takeViewportUpdate(), "Late retired reply published another result");
  waitUntil([&] { return !fresh.live(); }, "Retained viewport renewed its absolute deadline", 400ms);
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Timeout fixture did not clean up");
  check(service.failure.empty(), "Timeout fixture service failed");
}
void viewportInvalidatedBeforePublication(const PrivateBus &bus)
{
  Service service(bus);
  service.invalidateOnCheck = true;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  const auto request = client.requestViewportObservation();
  check(request != 0, "Client refused viewport invalidation fixture");
  const auto denied = viewportResult(client, request);
  check(!denied.observation && !denied.error.empty() && client.live(), "Invalidated final check published a viewport");
  const auto fresh = observed(client);
  check(fresh.live(), "Fresh observation did not recover from a bounded denial");
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Invalidation fixture did not clean up");
  check(service.failure.empty(), "Invalidation fixture service failed");
}
void nativeHitRecheckedAfterQtQueue(const PrivateBus &bus)
{
  // Case 1: native focus/geometry revision silently changes after the
  // asynchronous Qt Check. An earlier hit must not escape as a live snapshot.
  {
    Service service(bus);
    service.changeNativeRevisionAfterCheck = true;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready && descriptor.value >= 0, "Post-Qt native revision fixture unavailable");
    const auto request = client.requestRoutedPointObservation({13, 24, 425, 325});
    check(request != 0, "Post-Qt revision check could not be requested");
    const auto result = viewportResult(client, request);
    check(
        !result.observation && result.error.find("native viewport changed") != std::string::npos &&
            service.afterQtCheck && service.nativeHitQueries >= 3 && client.live(),
        "A silently replaced native revision escaped the post-Qt queue boundary"
    );
    // Rejection is bounded and nonterminal: the new native revision may
    // establish its own fresh snapshot, not revive the first one.
    const auto next = observed(client);
    check(next.live() && next.snapshot().nativeRevision == service.revision,
          "Rejected post-Qt native change poisoned all future observations");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Post-Qt native revision fixture did not close");
    check(service.failure.empty(), "Post-Qt native revision fixture failed");
  }
  // Case 2: focus/identity tokens and native revision remain equal but the
  // compositor's exact local transform shifts while Qt processed its queue.
  // Rechecking only the native token is insufficient.
  {
    Service service(bus);
    service.changeNativeTransformAfterCheck = true;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready && descriptor.value >= 0, "Post-Qt transform fixture unavailable");
    const auto request = client.requestRoutedPointObservation({13, 25, 425, 325});
    check(request != 0, "Post-Qt transform check could not be requested");
    const auto result = viewportResult(client, request);
    check(
        !result.observation && result.error.find("native hit changed after Qt") != std::string::npos &&
            service.afterQtCheck && service.nativeHitQueries >= 3 && client.live(),
        "Silently changed native surface transform was accepted through the Qt queue observation"
    );
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Post-Qt transform fixture did not close");
    check(service.failure.empty(), "Post-Qt transform fixture failed");
  }
}

void viewportWrongAuthenticatedPeer(const PrivateBus &bus)
{
  Service service(bus);
  service.wrongPeer = true;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  const auto request = client.requestViewportObservation();
  check(request != 0, "Client refused peer mismatch fixture");
  const auto denied = viewportResult(client, request);
  check(
      !denied.observation && denied.error.find("authenticated peer") != std::string::npos,
      "A viewport from the wrong configured peer was accepted"
  );
  waitUntil([&] { return !client.live() && service.callerGone; }, "Protocol failure did not retire its session");
  check(client.requestViewportObservation() == 0, "Terminal viewport channel revived");
  check(service.failure.empty(), "Peer mismatch fixture service failed");
}
} // namespace

int main(int argc, char **argv)
{
  auto *daemon = g_find_program_in_path("dbus-daemon");
  if (!daemon) {
    std::puts("SKIP: private D-Bus lifecycle tests require dbus-daemon (no desktop bus is used)");
    return 77;
  }
  g_free(daemon);
  QCoreApplication application(argc, argv);
  try {
    PrivateBus bus;
    ownerMismatch(bus);
    std::puts("PASS: provider and Shell must have the same unique owner");
    closeDuringSetup(bus);
    std::puts("PASS: queued close before launch reply cannot publish Ready");
    transferredDescriptor(bus);
    std::puts("PASS: descriptor transfers once and remains consumer-owned through stop/destruction");
    ownerLossAfterReady(bus);
    std::puts("PASS: provider owner loss retires the exact session without reconnect");
    stopDuringDelayedSetup(bus);
    std::puts("PASS: delayed setup cancellation closes the caller and ignores a late descriptor reply");
    viewportJoinAndRevocation(bus);
    std::puts("PASS: native/painted viewport join retains exact identities and revokes on native/child/stop changes");
    routedPointUsesFrozenCoordinate(bus);
    std::puts("PASS: exact routed GNOME stage point, not the latest pointer, retains sequence and native identity");
    routedPointCapabilityIsRequired(bus);
    std::puts("PASS: old providers retain cursor diagnostics without exact-point admission");
    routedPointWrongEchoIsTerminal(bus);
    std::puts("PASS: substituted point echo retires the native observation channel");
    viewportTimeoutAndLateReply(bus);
    std::puts("PASS: request deadline and late-reply retirement cannot renew or revive a viewport");
    viewportInvalidatedBeforePublication(bus);
    std::puts("PASS: final child invalidation prevents viewport publication and permits a fresh request");
    nativeHitRecheckedAfterQtQueue(bus);
    std::puts("PASS: final frozen GNOME native hit and local transform are rechecked after Qt queue completion");
    viewportWrongAuthenticatedPeer(bus);
    std::puts("PASS: wrong authenticated viewer peer ends the viewport session");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
