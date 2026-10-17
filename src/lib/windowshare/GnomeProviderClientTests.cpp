/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderClient.h"
#include "GnomeWindowDragProtocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <future>
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
using Clock = InputCutoffClock;

namespace deskflow::windowshare {
struct GnomeProviderClientTestAccess
{
  static std::mutex &mutex(GnomeProviderClient &client)
  {
    return client.stateMutexForTesting();
  }
  static bool pending(const GnomeProviderClient &client)
  {
    return client.pendingMapForTesting();
  }
  static bool completed(const GnomeProviderClient &client)
  {
    return client.pendingMapForTesting(true);
  }
  static std::atomic<std::uint64_t> &counter(GnomeProviderClient &client)
  {
    return client.requestCounterForTesting();
  }
};
} // namespace deskflow::windowshare

namespace {
struct AdmissionHold
{
  std::mutex state;
  std::condition_variable changed;
  bool entered = false, released = false;
  std::thread holder;
  explicit AdmissionHold(GnomeProviderClient &client)
      : holder([&] {
          const std::lock_guard admission(GnomeProviderClientTestAccess::mutex(client));
          std::unique_lock lock(state);
          entered = true;
          changed.notify_all();
          // A regression that accidentally blocks the caller must fail the
          // elapsed-time assertion, never strand the test process.
          changed.wait_for(lock, std::chrono::seconds(2), [&] { return released; });
        })
  {
    std::unique_lock lock(state);
    if (!changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; })) {
      released = true;
      changed.notify_all();
      lock.unlock();
      holder.join();
      throw std::runtime_error("Could not hold the real provider state mutex");
    }
  }
  void release()
  {
    {
      const std::lock_guard lock(state);
      released = true;
    }
    changed.notify_all();
    if (holder.joinable())
      holder.join();
  }
  ~AdmissionHold()
  {
    release();
  }
};

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
    <method name="WindowDragPreview">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="t" direction="in"/><arg type="t" direction="in"/>
      <arg type="s" direction="in"/><arg type="i" direction="in"/><arg type="i" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="WatchWindowDrag">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="b" direction="in"/>
    </method>
    <signal name="WindowDragChanged"><arg type="s"/><arg type="u"/><arg type="s"/><arg type="a{sv}"/></signal>
    <method name="GetViewer">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="GetViewerWithAssociation">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="a{sv}" direction="out"/>
    </method>
    <method name="ValidateRoutedPoint">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="ValidateRoutedPoints">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="a(ssudd)" direction="in"/>
      <arg type="u" direction="in"/><arg type="a{sv}" direction="out"/>
    </method>
    <method name="ObserveStageFrames">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="ValidateCurrentHit">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
    <method name="DrainInputPrefix">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="d" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="in"/><arg type="u" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>
<method name="SelectInputPreparation">
      <arg name="session" type="s" direction="in" />
      <arg name="generation" type="u" direction="in" />
      <arg name="viewer" type="s" direction="in" />
      <arg name="request" type="s" direction="in" />
      <arg name="x" type="d" direction="in" />
      <arg name="y" type="d" direction="in" />
      <arg name="associations" type="as" direction="in" />
      <arg name="binding" type="a{sv}" direction="in" />
      <arg name="budgetMs" type="u" direction="in" />
      <arg name="result" type="a{sv}" direction="out" />
    </method>

<method name="QueryInputPreparation">
      <arg name="session" type="s" direction="in" />
      <arg name="generation" type="u" direction="in" />
      <arg name="viewer" type="s" direction="in" />
      <arg name="request" type="s" direction="in" />
      <arg name="x" type="d" direction="in" />
      <arg name="y" type="d" direction="in" />
      <arg name="revision" type="u" direction="in" />
      <arg name="binding" type="a{sv}" direction="in" />
      <arg name="cleanup" type="b" direction="in" />
      <arg name="result" type="a{sv}" direction="out" />
    </method>

<method name="ActivateInputPreparation">
      <arg name="session" type="s" direction="in" />
      <arg name="generation" type="u" direction="in" />
      <arg name="viewer" type="s" direction="in" />
      <arg name="request" type="s" direction="in" />
      <arg name="x" type="d" direction="in" />
      <arg name="y" type="d" direction="in" />
      <arg name="revision" type="u" direction="in" />
      <arg name="binding" type="a{sv}" direction="in" />
      <arg name="budgetMs" type="u" direction="in" />
      <arg name="result" type="a{sv}" direction="out" />
    </method>

<method name="DrainInputPreparation">
      <arg name="session" type="s" direction="in" />
      <arg name="generation" type="u" direction="in" />
      <arg name="viewer" type="s" direction="in" />
      <arg name="request" type="s" direction="in" />
      <arg name="x" type="d" direction="in" />
      <arg name="y" type="d" direction="in" />
      <arg name="revision" type="u" direction="in" />
      <arg name="binding" type="a{sv}" direction="in" />
      <arg name="budgetMs" type="u" direction="in" />
      <arg name="cleanup" type="b" direction="in" />
      <arg name="result" type="a{sv}" direction="out" />
    </method>

<method name="FinishInputPreparation">
      <arg name="session" type="s" direction="in" />
      <arg name="generation" type="u" direction="in" />
      <arg name="viewer" type="s" direction="in" />
      <arg name="request" type="s" direction="in" />
      <arg name="x" type="d" direction="in" />
      <arg name="y" type="d" direction="in" />
      <arg name="revision" type="u" direction="in" />
      <arg name="binding" type="a{sv}" direction="in" />
      <arg name="budgetMs" type="u" direction="in" />
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
  bool advertiseRoutedPoint = true, advertiseStage = false;
  bool wrongStagePoint = false, shortStageReply = false, unavailableStage = false, unknownStageMethod = false;
  bool duplicateStageField = false, substituteStageField = false, wrongStageFieldType = false;
  bool changeNativeRevisionDuringStage = false, signalStageInvalidation = false;
  bool wrongStageRevision = false, wrongStageRequest = false, emptyStageReason = false;
  unsigned stageRequests = 0;
  bool advertiseDrain = false, wrongDrainBinding = false, duplicateDrainField = false, unknownDrainMethod = false;
  bool advertisePreparation = false, wrongPreparationBinding = false, noPreparationTarget = false;
  bool nativePrepared = false, nativeActivated = false, qtPreparationGate = false, qtPreparationConsumed = false;
  bool denyPreparationCleanup = false, delayPreparationFocus = false, invalidateFinalPreparation = false;
  bool activationRestackBeforeReply = false, invalidateAfterActivation = false;
  unsigned preparationPostActivationQueries = 0;
  unsigned preparationFocusDelayMs = 150;
  unsigned preparationDrains = 0, preparationQtDrains = 0, preparationActivations = 0, preparationCleanups = 0;
  std::vector<std::string> preparationSteps;
  std::optional<InputPreparationBinding> preparationBinding;
  bool delayDrain = false, invalidateDuringDrain = false, wrongQtDrainBinding = false;
  std::string orderingContract;
  bool wrongOrderingContract = false, duplicateOrderingCapability = false;
  unsigned drainRequests = 0, qtDrainRequests = 0;
  unsigned qtMapRequests = 0;
  unsigned nativeBatchQueries = 0, qtBatchRequests = 0;
  bool advertiseBatch = true, wrongBatchEcho = false, denyQtBatch = false, driftAfterBatch = false;
  std::optional<InputCutoffBinding> qtSeal;
  std::uint64_t qtGateGeneration = 6;
  guint lastDrainBudget = 0;
  GDBusMethodInvocation *pendingDrain = nullptr;
  GVariant *pendingDrainReply = nullptr;
  QByteArray delayedReply;
  std::chrono::steady_clock::time_point delayedUntil;
  Mode mode;
  std::string caller, failure;
  unsigned capabilities = 0, creates = 0, launches = 0, closes = 0;
  bool callerGone = false;
  bool missingAssociationCapability = false, wrongWindowAssociation = false, duplicateWindowAssociation = false;
  bool advertiseWindowDrag = false, wrongDragBinding = false, duplicateDragField = false;
  unsigned nativeDragRequests = 0;
  std::vector<GnomeWindowDragReply> dragReplies;
  std::vector<GnomeWindowDragEvent> dragEvents;

  GnomeViewportState preparationPaint() const
  {
    GnomeViewportState state;
    state.connection = 1;
    state.session = 11;
    state.epoch = 12;
    state.geometry = 3;
    state.focusRequest = 4;
    state.focusRevision = 5;
    state.surfaceIncarnation = 1;
    state.viewportRevision = 2;
    state.peerFingerprint = QString(64, wrongPeer ? 'b' : 'a');
    state.windowAssociation = QStringLiteral("org.deskflow.window.") + QString(64, 'b');
    state.nativeSurface = 9;
    state.clientWidth = state.surfaceWidth = state.videoWidth = 800;
    state.clientHeight = state.surfaceHeight = state.videoHeight = 600;
    state.sourceWidth = state.imageWidth = 640;
    state.sourceHeight = state.imageHeight = 480;
    state.dpr = state.logicalScale = 1;
    return state;
  }

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
  void invalidateSeal()
  {
    check(bool(qtSeal), "No fixture prefix seal to invalidate");
    GnomeViewportReply reply;
    reply.operation = GnomeViewportOperation::SealInvalidated;
    reply.cutoff = *qtSeal;
    reply.reason = "fixture-All-fence-resumed";
    qtSeal.reset();
    sendViewport(reply);
  }
  void invalidateNative(const char *reason = "fixture-focus-changed")
  {
    Error error;
    check(
        g_dbus_connection_emit_signal(
            connection, caller.c_str(), objectPath, serviceName, "Invalidated",
            g_variant_new("(suus)", sessionId, generation, ++revision, reason), &error.value
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
      if (isGnomeWindowDrag(*object)) {
        if (object->value("kind") == QJsonValue("event"))
          dragEvents.push_back(parseGnomeWindowDragEvent(*object));
        else
          dragReplies.push_back(parseGnomeWindowDragReply(*object));
        continue;
      }
      const auto request = parseGnomeViewportRequest(*object);
      GnomeViewportReply reply;
      reply.operation = request.operation;
      reply.request = request.request;
      reply.ok = true;
      reply.cutoff = request.cutoff;
      reply.seal = request.seal;
      if (isGnomePreparationOperation(request.operation)) {
        reply.preparation = request.preparation;
        reply.target = request.target;
        const auto state = preparationPaint();
        const GnomePreparationTarget target{
            state.session,          state.epoch, state.geometry, state.surfaceIncarnation, 1, state.nativeSurface,
            state.windowAssociation
        };
        if (request.operation == GnomeViewportOperation::PreparationInventory) {
          check(!qtPreparationGate && request.budgetMs <= 500, "Preparation inventory replaced an active gate");
          qtPreparationGate = true;
          preparationBinding = request.preparation;
          preparationSteps.push_back("qt-gate");
          reply.inventory = {state.connection, state.peerFingerprint, {target}};
        } else {
          check(
              qtPreparationGate && preparationBinding == request.preparation && request.target == target,
              "Preparation private call lost its exact gate/paint"
          );
          if (request.operation == GnomeViewportOperation::PreparationMap) {
            reply.x = request.x * .8;
            reply.y = request.y * .8;
          } else if (request.operation == GnomeViewportOperation::PreparationDrain) {
            check(preparationDrains == 1, "Qt preparation drain preceded native prefix drain");
            ++preparationQtDrains;
            preparationSteps.push_back("qt-drain");
          } else if (request.operation == GnomeViewportOperation::PreparationFocus) {
            check(nativeActivated, "Qt focus preceded exact native activation");
            preparationSteps.push_back("qt-focus");
            reply.state = state;
            if (invalidateAfterActivation)
              invalidateNative("restacked");
            if (std::exchange(delayPreparationFocus, false)) {
              delayedReply = gnomeViewportFrame(gnomeViewportJson(reply));
              delayedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(preparationFocusDelayMs);
              continue;
            }
          } else if (request.operation == GnomeViewportOperation::PreparationPromote) {
            check(request.cutoff.forwarding.generation == qtGateGeneration, "Preparation promoted without genuine All");
            qtPreparationConsumed = true;
            preparationSteps.push_back("qt-consumed");
          } else if (request.operation == GnomeViewportOperation::PreparationCleanup) {
            check(preparationDrains >= 1, "Preparation cleanup preceded native drain");
            ++preparationCleanups;
            preparationSteps.push_back("qt-cleanup");
            if (denyPreparationCleanup) {
              reply.ok = false;
              reply.reason = "fixture-cleanup-unavailable";
            } else
              qtPreparationGate = false;
          }
        }
      } else if (request.operation == GnomeViewportOperation::Snapshot) {
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
        state.windowAssociation = QStringLiteral("org.deskflow.window.") + QString(64, 'b');
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
        } else if (
            request.operation == GnomeViewportOperation::Map || request.operation == GnomeViewportOperation::InputMap
        ) {
          reply.x = request.x * 0.8;
          reply.y = request.y * 0.8;
          if (request.operation == GnomeViewportOperation::InputMap) {
            ++qtMapRequests;
            if (!qtSeal || request.seal != qtSeal->request || !continuesInputCutoffSeal(*qtSeal, request.cutoff) ||
                request.cutoff.forwarding.generation != qtGateGeneration) {
              reply.ok = false;
              reply.reason = "fixture-prefix-seal-not-held";
            }
          }
        } else if (request.operation == GnomeViewportOperation::InputMapBatch) {
          ++qtBatchRequests;
          if (!qtSeal || *qtSeal != request.cutoff || request.cutoff.forwarding.generation != qtGateGeneration ||
              denyQtBatch) {
            reply.ok = false;
            reply.reason = "fixture-prefix-batch-denied";
          } else {
            for (const auto &point : request.points)
              reply.points.push_back({point.cutoff, point.x * 0.8, point.y * 0.8});
          }
        } else if (request.operation == GnomeViewportOperation::Check) {
          // A synchronous Qt Check acknowledgement is not evidence that
          // GNOME remained stable while the Qt default queue was processed.
          afterQtCheck = true;
          if (std::exchange(changeNativeRevisionAfterCheck, false))
            ++revision; // Deliberately emit NO native invalidation signal.
          if (std::exchange(invalidateOnCheck, false))
            invalidatePaint();
        } else if (
            request.operation == GnomeViewportOperation::InputDrain ||
            request.operation == GnomeViewportOperation::InputSeal
        ) {
          ++qtDrainRequests;
          reply.cutoff = request.cutoff;
          if (wrongQtDrainBinding)
            ++reply.cutoff.request;
          if (request.cutoff.forwarding.generation != qtGateGeneration) {
            reply.ok = false;
            reply.reason = "fixture-exact-All-fence-not-held";
          }
          if (std::exchange(invalidateOnCheck, false))
            invalidatePaint();
          if (reply.ok && request.operation == GnomeViewportOperation::InputSeal)
            qtSeal = request.cutoff;
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
      if (!missingAssociationCapability)
        g_variant_builder_add(&values, "{sv}", "perWindowAssociation", g_variant_new_boolean(true));
      if (advertiseRoutedPoint)
        g_variant_builder_add(&values, "{sv}", "routedPointObservation", g_variant_new_boolean(true));
      if (advertiseBatch)
        g_variant_builder_add(&values, "{sv}", "routedPointsObservation", g_variant_new_boolean(true));
      if (advertiseStage)
        g_variant_builder_add(&values, "{sv}", "stageUpdateSequence", g_variant_new_boolean(true));
      if (advertiseWindowDrag) {
        g_variant_builder_add(&values, "{sv}", "windowDragPlacement", g_variant_new_boolean(true));
        g_variant_builder_add(&values, "{sv}", "ownedWindowMoveObservation", g_variant_new_boolean(true));
      }
      if (advertiseDrain) {
        g_variant_builder_add(&values, "{sv}", "inputDrainSegment", g_variant_new_boolean(true));
        g_variant_builder_add(
            &values, "{sv}", "inputDrainOrderingLimit",
            g_variant_new_string(orderingContract.empty() ? "mutter-eis-sync-build-unverified" : "")
        );
        g_variant_builder_add(
            &values, "{sv}", "inputDrainOrderingContract", g_variant_new_string(orderingContract.c_str())
        );
        if (duplicateOrderingCapability)
          g_variant_builder_add(&values, "{sv}", "inputDrainOrderingContract", g_variant_new_string(""));
      }
      if (advertisePreparation)
        g_variant_builder_add(&values, "{sv}", "inputPreparation", g_variant_new_boolean(true));
      g_variant_builder_add(&values, "{sv}", "backend", g_variant_new_string("MetaBackendNative"));
      g_variant_builder_add(&values, "{sv}", "deliveryReady", g_variant_new_boolean(false));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(usa{sv})", 1u, "50.1", &values));
    } else if (!std::strcmp(name, "WindowDragPreview")) {
      ++nativeDragRequests;
      const char *id = nullptr, *viewer = nullptr, *association = nullptr, *action = nullptr;
      guint value = 0;
      guint64 drag = 0, sequence = 0;
      gint x = 0, y = 0;
      g_variant_get(
          parameters, "(&su&s&stt&sii)", &id, &value, &viewer, &association, &drag, &sequence, &action, &x, &y
      );
      check(
          std::strcmp(id, sessionId) == 0 && value == generation && std::strcmp(viewer, viewerId) == 0 &&
              caller == g_dbus_method_invocation_get_sender(invocation) && !std::strcmp(action, "place"),
          "Native drag client changed session/caller/action"
      );
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&values, "{sv}", "available", g_variant_new_boolean(true));
      g_variant_builder_add(&values, "{sv}", "session", g_variant_new_string(sessionId));
      g_variant_builder_add(&values, "{sv}", "generation", g_variant_new_uint32(generation));
      g_variant_builder_add(&values, "{sv}", "viewer", g_variant_new_string(viewerId));
      g_variant_builder_add(&values, "{sv}", "association", g_variant_new_string(association));
      g_variant_builder_add(&values, "{sv}", "drag", g_variant_new_uint64(drag + (wrongDragBinding ? 1 : 0)));
      g_variant_builder_add(&values, "{sv}", "sequence", g_variant_new_uint64(sequence));
      g_variant_builder_add(&values, "{sv}", "x", g_variant_new_int32(x));
      g_variant_builder_add(&values, "{sv}", "y", g_variant_new_int32(y));
      g_variant_builder_add(&values, "{sv}", "width", g_variant_new_int32(640));
      g_variant_builder_add(&values, "{sv}", "height", g_variant_new_int32(480));
      g_variant_builder_add(&values, "{sv}", "revision", g_variant_new_uint32(1));
      if (duplicateDragField)
        g_variant_builder_add(&values, "{sv}", "revision", g_variant_new_uint32(1));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &values));
    } else if (!std::strcmp(name, "WatchWindowDrag")) {
      const char *id = nullptr, *viewer = nullptr, *association = nullptr;
      guint value = 0;
      gboolean enabled = false;
      g_variant_get(parameters, "(&su&s&sb)", &id, &value, &viewer, &association, &enabled);
      check(
          std::strcmp(id, sessionId) == 0 && value == generation && std::strcmp(viewer, viewerId) == 0 &&
              caller == g_dbus_method_invocation_get_sender(invocation),
          "Native drag watch changed exact owner"
      );
      g_dbus_method_invocation_return_value(invocation, nullptr);
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
    } else if (
        !std::strcmp(name, "SelectInputPreparation") || !std::strcmp(name, "QueryInputPreparation") ||
        !std::strcmp(name, "ActivateInputPreparation") || !std::strcmp(name, "FinishInputPreparation")
    ) {
      check(
          advertisePreparation && caller == g_dbus_method_invocation_get_sender(invocation),
          "Preparation native call changed private owner or capability"
      );
      auto *binding = g_variant_get_child_value(parameters, 7);
      const auto getString = [&](unsigned index) {
        auto *value = g_variant_get_child_value(parameters, index);
        const std::string result = g_variant_get_string(value, nullptr);
        g_variant_unref(value);
        return result;
      };
      const auto getDouble = [&](unsigned index) {
        auto *value = g_variant_get_child_value(parameters, index);
        const auto result = g_variant_get_double(value);
        g_variant_unref(value);
        return result;
      };
      const auto request = getString(3);
      const double x = getDouble(4), y = getDouble(5);
      check(
          getString(0) == sessionId && getString(2) == viewerId &&
              (qtPreparationGate || !std::strcmp(name, "FinishInputPreparation")) && g_variant_n_children(binding) == 5,
          "Preparation fabricated an All or lost its closed gate"
      );
      if (!std::strcmp(name, "SelectInputPreparation")) {
        check(!nativePrepared, "Preparation selected two native lifetimes");
        nativePrepared = true;
        preparationSteps.push_back("native-select");
      } else if (!std::strcmp(name, "ActivateInputPreparation")) {
        check(
            nativePrepared && preparationDrains == 1 && preparationQtDrains == 1,
            "Native activation bypassed the full ordered preparation drain"
        );
        nativeActivated = true;
        ++preparationActivations;
        preparationSteps.push_back("native-activate");
        // The native adapter must complete its bounded activation frames
        // before replying. A queued restack belongs before that boundary,
        // not after it; the client may retain only the completed revision.
        if (activationRestackBeforeReply)
          invalidateNative("restacked");
      } else if (!std::strcmp(name, "FinishInputPreparation")) {
        check(nativePrepared && !qtPreparationGate, "Native preparation retired before Qt gate cleanup");
        nativePrepared = false;
        g_variant_unref(binding);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
        return;
      }
      if (wrongPreparationBinding) {
        GVariantDict changed;
        g_variant_dict_init(&changed, binding);
        g_variant_dict_insert(&changed, "preparationRequest", "s", "999");
        g_variant_unref(binding);
        binding = g_variant_ref_sink(g_variant_dict_end(&changed));
      }
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      const auto add = [&](const char *key, GVariant *value) { g_variant_builder_add(&values, "{sv}", key, value); };
      const bool available = !noPreparationTarget || std::strcmp(name, "SelectInputPreparation");
      add("available", g_variant_new_boolean(available));
      add("reason", g_variant_new_string(available ? "" : "preparation-no-target"));
      add("session", g_variant_new_string(sessionId));
      add("generation", g_variant_new_uint32(generation));
      add("viewer", g_variant_new_string(viewerId));
      add("request", g_variant_new_string(request.c_str()));
      add("binding", binding);
      g_variant_unref(binding);
      add("association", g_variant_new_string(("org.deskflow.window." + std::string(64, 'b')).c_str()));
      add("window", g_variant_new_string("fixture-window"));
      add("surface", g_variant_new_string("fixture-surface"));
      add("revision", g_variant_new_uint32(revision));
      add("stageX", g_variant_new_double(x));
      add("stageY", g_variant_new_double(y));
      add("localX", g_variant_new_double(x - 100));
      add("localY", g_variant_new_double(y - 100));
      add("focused", g_variant_new_boolean(nativeActivated));
      add("orderingContract", g_variant_new_string(orderingContract.c_str()));
      // The final exact reply may already be on its way when focus/layout
      // invalidates. The client must consume this signal before publication,
      // not discard it just because the public state remains Preparing.
      if (!std::strcmp(name, "QueryInputPreparation") && nativeActivated && ++preparationPostActivationQueries == 2 &&
          invalidateFinalPreparation)
        invalidateNative();
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &values));
    } else if (!std::strcmp(name, "DrainInputPrefix") || !std::strcmp(name, "DrainInputPreparation")) {
      check(caller == g_dbus_method_invocation_get_sender(invocation), "Drain request changed private caller");
      const char *sessionKey = nullptr, *token = nullptr, *request = nullptr;
      guint g = 0, requestedRevision = 0, budget = 0;
      double x = 0, y = 0;
      GVariant *binding = nullptr;
      const bool preparing = !std::strcmp(name, "DrainInputPreparation");
      gboolean cleanup = false;
      if (preparing)
        g_variant_get(
            parameters, "(&su&s&sddu@a{sv}ub)", &sessionKey, &g, &token, &request, &x, &y, &requestedRevision, &binding,
            &budget, &cleanup
        );
      else
        g_variant_get(
            parameters, "(&su&s&sddu@a{sv}u)", &sessionKey, &g, &token, &request, &x, &y, &requestedRevision, &binding,
            &budget
        );
      check(
          !std::strcmp(sessionKey, sessionId) && g == generation && !std::strcmp(token, viewerId) && request &&
              *request && advertiseDrain && budget > 0 && budget <= 120,
          "Drain request changed native binding or inherited budget"
      );
      ++drainRequests;
      if (preparing) {
        ++preparationDrains;
        preparationSteps.push_back(cleanup ? "native-cleanup" : "native-drain");
      }
      lastDrainBudget = budget;
      if (unknownDrainMethod) {
        g_variant_unref(binding);
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Input drain is unavailable"
        );
        return;
      }
      if (wrongDrainBinding) {
        GVariantDict changed;
        g_variant_dict_init(&changed, binding);
        g_variant_dict_insert(&changed, "cutoffRequest", "s", "18446744073709551615");
        g_variant_unref(binding);
        binding = g_variant_ref_sink(g_variant_dict_end(&changed));
      }
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      const auto add = [&](const char *key, GVariant *value) { g_variant_builder_add(&values, "{sv}", key, value); };
      add("available", g_variant_new_boolean(true));
      add(duplicateDrainField ? "available" : "reason",
          duplicateDrainField ? g_variant_new_boolean(true) : g_variant_new_string(""));
      add("session", g_variant_new_string(sessionId));
      add("generation", g_variant_new_uint32(generation));
      add("viewer", g_variant_new_string(viewerId));
      add("request", g_variant_new_string(request));
      add("revision", g_variant_new_uint32(requestedRevision));
      add("stageX", g_variant_new_double(x));
      add("stageY", g_variant_new_double(y));
      add("binding", binding);
      g_variant_unref(binding);
      add("segment", g_variant_new_string("clutter-queue-and-stage"));
      add("orderingLimit", g_variant_new_string(orderingContract.empty() ? "mutter-eis-sync-build-unverified" : ""));
      add("orderingContract",
          g_variant_new_string(wrongOrderingContract ? "sha256:substituted" : orderingContract.c_str()));
      auto *reply = g_variant_new("(a{sv})", &values);
      if (invalidateDuringDrain)
        invalidateNative();
      if (std::exchange(delayDrain, false)) {
        check(!pendingDrain, "Fixture retained two delayed drains");
        pendingDrain = G_DBUS_METHOD_INVOCATION(g_object_ref(invocation));
        pendingDrainReply = g_variant_ref_sink(reply);
      } else
        g_dbus_method_invocation_return_value(invocation, reply);
    } else if (!std::strcmp(name, "ObserveStageFrames")) {
      check(caller == g_dbus_method_invocation_get_sender(invocation), "Stage request changed authenticated caller");
      const char *sessionKey = nullptr, *token = nullptr, *request = nullptr;
      guint g = 0, requestedRevision = 0;
      double stageX = 0, stageY = 0;
      g_variant_get(
          parameters, "(&su&s&sddu)", &sessionKey, &g, &token, &request, &stageX, &stageY, &requestedRevision
      );
      check(
          sessionKey && !std::strcmp(sessionKey, sessionId) && g == generation && token &&
              !std::strcmp(token, viewerId) && request && *request && advertiseStage,
          "Stage observation changed exact session or lacked capability"
      );
      ++stageRequests;
      if (unknownStageMethod) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Stage observation method is unavailable"
        );
        return;
      }
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      const auto add = [&](const char *key, GVariant *value) { g_variant_builder_add(&values, "{sv}", key, value); };
      const bool invalidated = std::exchange(changeNativeRevisionDuringStage, false);
      if (invalidated) {
        if (signalStageInvalidation)
          invalidateNative("native-state-invalidated");
        else
          ++revision;
      }
      const bool permitted = !unavailableStage && requestedRevision == revision;
      add("available", g_variant_new_boolean(permitted));
      if (duplicateStageField)
        add("available", g_variant_new_boolean(permitted));
      else if (substituteStageField)
        add("unexpected-field", g_variant_new_string(""));
      else
        add("reason", g_variant_new_string(
                          permitted || emptyStageReason ? ""
                          : invalidated                 ? "native-state-invalidated"
                                                        : "fixture-stage-unavailable"
                      ));
      add("observationOnly", g_variant_new_boolean(true));
      add("deliveryReady", g_variant_new_boolean(false));
      add("session", g_variant_new_string(sessionId));
      add("viewer", g_variant_new_string(viewerId));
      add("request", g_variant_new_string(wrongStageRequest ? "other-request" : request));
      add("generation", g_variant_new_uint32(generation));
      add("revision", g_variant_new_uint32(wrongStageRevision ? revision + 1 : revision));
      add("stageUpdates",
          wrongStageFieldType ? g_variant_new_string("two") : g_variant_new_uint32(shortStageReply ? 1 : 2));
      add("sameStageView", g_variant_new_boolean(permitted));
      add("stageX", g_variant_new_double(wrongStagePoint ? stageX + 1 : stageX));
      add("stageY", g_variant_new_double(stageY));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &values));
    } else if (!std::strcmp(name, "ValidateRoutedPoints")) {
      ++nativeBatchQueries;
      const char *id = nullptr, *token = nullptr, *request = nullptr, *epoch = nullptr;
      guint value = 0, expectedRevision = 0;
      GVariant *points = nullptr;
      g_variant_get(
          parameters, "(&su&s&s&s@a(ssudd)u)", &id, &value, &token, &request, &epoch, &points, &expectedRevision
      );
      check(
          caller == g_dbus_method_invocation_get_sender(invocation) && std::strcmp(id, sessionId) == 0 &&
              value == generation && std::strcmp(token, viewerId) == 0,
          "Batch query changed provider binding"
      );
      GVariantBuilder values, mapped;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_init(&mapped, G_VARIANT_TYPE("a(ssudddd)"));
      GVariantIter iterator;
      g_variant_iter_init(&iterator, points);
      const char *cutoff = nullptr, *sequence = nullptr;
      guint part = 0;
      double x = 0, y = 0;
      while (g_variant_iter_next(&iterator, "(&s&sudd)", &cutoff, &sequence, &part, &x, &y))
        g_variant_builder_add(
            &mapped, "(ssudddd)", cutoff, sequence, part, x + (wrongBatchEcho ? 1 : 0), y,
            x - 100 + (driftAfterBatch && qtBatchRequests ? 1 : 0), y - 100
        );
      g_variant_unref(points);
      const auto add = [&](const char *key, GVariant *item) { g_variant_builder_add(&values, "{sv}", key, item); };
      add("session", g_variant_new_string(sessionId));
      add("viewer", g_variant_new_string(viewerId));
      add("generation", g_variant_new_uint32(generation));
      add("revision", g_variant_new_uint32(revision));
      add("request", g_variant_new_string(request));
      add("captureEpoch", g_variant_new_string(epoch));
      add("window", g_variant_new_string("fixture-window"));
      add("surface", g_variant_new_string("fixture-surface"));
      add("available", g_variant_new_boolean(expectedRevision == revision));
      add("reason", g_variant_new_string(""));
      add("points", g_variant_builder_end(&mapped));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &values));
    } else if (
        !std::strcmp(name, "GetViewerWithAssociation") || !std::strcmp(name, "ValidateCurrentHit") ||
        !std::strcmp(name, "ValidateRoutedPoint")
    ) {
      check(caller == g_dbus_method_invocation_get_sender(invocation), "Viewport query changed private caller");
      const char *id = nullptr, *token = nullptr, *request = nullptr, *association = nullptr;
      guint value = 0, expectedRevision = revision;
      double x = 400, y = 300;
      const bool routed = !std::strcmp(name, "ValidateRoutedPoint");
      const bool hit = routed || !std::strcmp(name, "ValidateCurrentHit");
      if (hit) {
        ++nativeHitQueries;
        g_variant_get(parameters, "(&su&s&sddu)", &id, &value, &token, &request, &x, &y, &expectedRevision);
      } else {
        g_variant_get(parameters, "(&su&s&s)", &id, &value, &token, &association);
        check(
            std::string(association) == "org.deskflow.window." + std::string(64, 'b'),
            "Provider queried another native role association"
        );
      }
      check(
          id && token && std::strcmp(id, sessionId) == 0 && value == generation && std::strcmp(token, viewerId) == 0,
          "Native viewport query changed binding"
      );
      GVariantBuilder values;
      g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
      const auto add = [&](const char *key, GVariant *value) { g_variant_builder_add(&values, "{sv}", key, value); };
      add("session", g_variant_new_string(sessionId));
      add("viewer", g_variant_new_string(viewerId));
      add("windowAssociation",
          g_variant_new_string(("org.deskflow.window." + std::string(64, wrongWindowAssociation ? 'c' : 'b')).c_str()));
      if (duplicateWindowAssociation)
        add("windowAssociation", g_variant_new_string(("org.deskflow.window." + std::string(64, 'b')).c_str()));
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
    if (pendingDrain) {
      g_dbus_method_invocation_return_dbus_error(pendingDrain, "org.deskflow.Test.Ended", "Fixture ended");
      g_object_unref(std::exchange(pendingDrain, nullptr));
      g_variant_unref(std::exchange(pendingDrainReply, nullptr));
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
GnomeProviderClient::InputDrainRequest inputDrainRequest(std::uint64_t request = 1)
{
  InputCutoffBinding cutoff;
  cutoff.request = request;
  cutoff.coreConnectionGeneration = 7;
  cutoff.capture = {9, 37, 1};
  cutoff.forwarding = {{11, 12}, 6, 4, 5, 3, ForwardingScope::All};
  cutoff.viewerConnection = 1;
  return {cutoff, {9, 37, 425, 325}, InputCutoffClock::now() + 250ms};
}
GnomeProviderClient::InputDrainUpdate inputDrainResult(GnomeProviderClient &client, std::uint64_t request)
{
  std::optional<GnomeProviderClient::InputDrainUpdate> result;
  waitUntil(
      [&] {
        auto value = client.takeInputDrainUpdate();
        if (value) {
          check(value->request == request, "Input drain changed request identity");
          result = std::move(value);
        }
        return result.has_value();
      },
      "Input drain result timed out"
  );
  return std::move(*result);
}
void inputDrainBindingAndRevocation(const PrivateBus &bus)
{
  Service service(bus);
  service.advertiseDrain = true;
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  auto wanted = inputDrainRequest();
  const auto request = client.requestInputDrain(wanted);
  check(request && !client.requestViewportObservation(), "Input drain did not reserve the bounded shared slot");
  const auto result = inputDrainResult(client, request);
  if (!result.segment || !result.segment->live())
    throw std::runtime_error("No live compositor/Qt segment: " + result.error);
  check(
      result.segment->binding() == wanted.cutoff && result.segment->snapshot().routedPoint == wanted.point &&
          result.segment->snapshot().desktopX == 425 && service.drainRequests == 1 && service.qtDrainRequests == 1 &&
          service.stageRequests == 0 && !service.afterQtCheck && !client.takeViewportUpdate() &&
          std::string(result.segment->orderingLimitation()) == "mutter-eis-sync-build-unverified" &&
          !result.segment->senderOrdering(),
      "Input drain lost correlation or used generic observation as a drain"
  );
  check(!client.requestInputDrain(wanted), "A retired cutoff request was reused");
  service.invalidatePaint();
  waitUntil([&] { return !result.segment->live(); }, "Gate/paint invalidation did not revoke input-drain segment");
  const auto invalidated = client.takeInputDrainUpdate();
  check(
      invalidated && invalidated->request == request && !invalidated->segment,
      "Input-drain invalidation was not published on its typed update"
  );
  wanted = inputDrainRequest(2);
  wanted.cutoff.forwarding.generation = 7; // Snapshot matches, exact gate does not.
  const auto deniedId = client.requestInputDrain(wanted);
  const auto denied = inputDrainResult(client, deniedId);
  check(
      !denied.segment && denied.error == "fixture-exact-All-fence-not-held" && client.live(),
      "An unrelated All generation granted a segment"
  );
  auto bad = inputDrainRequest(3);
  bad.point.sequence++;
  check(!client.requestInputDrain(bad), "Input drain admitted a substituted capture point");
  bad = inputDrainRequest(3);
  bad.cutoff.forwarding.scope = ForwardingScope::Keyboard;
  check(!client.requestInputDrain(bad), "Keyboard-only gate admitted a full input drain");
  bad = inputDrainRequest(3);
  bad.cutoff.viewerConnection++;
  const auto oldDrainCount = service.drainRequests;
  const auto badId = client.requestInputDrain(bad);
  const auto changed = inputDrainResult(client, badId);
  check(
      !changed.segment && service.drainRequests == oldDrainCount, "Changed Viewer connection reached the native drain"
  );
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Input drain fixture did not close");
  check(service.failure.empty(), "Input drain binding fixture failed");
}
void inputDrainMalformedAndInvalidated(const PrivateBus &bus)
{
  for (int mode = 0; mode < 5; ++mode) {
    Service service(bus);
    service.advertiseDrain = true;
    service.wrongOrderingContract = mode == 4;
    service.duplicateDrainField = mode == 0;
    service.wrongDrainBinding = mode == 1;
    service.wrongQtDrainBinding = mode == 2;
    service.invalidateDuringDrain = mode == 3;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    const auto id = client.requestInputDrain(inputDrainRequest());
    check(id, "Input drain fixture was not admitted");
    const auto result = inputDrainResult(client, id);
    check(!result.segment && !result.error.empty(), "Malformed or invalidated drain granted a segment");
    if (mode != 2)
      check(service.qtDrainRequests == 0, "Invalid provider segment reached Qt drain");
    if (mode < 3 || mode == 4)
      waitUntil([&] { return !client.live(); }, "Substituted drain reply did not retire the private channel");
    else
      check(client.live(), "Ordinary native invalidation became a protocol failure");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Malformed drain fixture did not close");
    check(service.failure.empty(), "Malformed drain fixture failed");
  }
}
void inputDrainDeadlineAndCapabilities(const PrivateBus &bus)
{
  for (int mode = 0; mode < 3; ++mode) {
    Service service(bus);
    service.advertiseDrain = mode != 0;
    service.delayDrain = mode == 1;
    service.unknownDrainMethod = mode == 2;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    auto wanted = inputDrainRequest();
    if (mode == 0) {
      check(
          !client.requestInputDrain(wanted) && service.drainRequests == 0,
          "Provider without native drain capability admitted a request"
      );
    } else {
      if (mode == 1)
        wanted.deadline = InputCutoffClock::now() + 80ms;
      const auto id = client.requestInputDrain(wanted);
      check(id, "Delayed/unknown drain fixture was not admitted");
      const auto result = inputDrainResult(client, id);
      check(
          !result.segment && client.live() && service.qtDrainRequests == 0,
          "Expired or unavailable native drain reached Qt or granted a segment"
      );
      if (mode == 1) {
        check(
            service.lastDrainBudget <= 80 && service.pendingDrain,
            "Input drain renewed the inherited deadline or did not exercise a late native reply"
        );
        g_dbus_method_invocation_return_value(service.pendingDrain, service.pendingDrainReply);
        g_object_unref(std::exchange(service.pendingDrain, nullptr));
        g_variant_unref(std::exchange(service.pendingDrainReply, nullptr));
      } else
        service.unknownDrainMethod = false;
      const auto freshId = client.requestInputDrain(inputDrainRequest(2));
      check(freshId && freshId != id, "A fresh drain after a late/unsupported reply was not admitted");
      const auto fresh = inputDrainResult(client, freshId);
      check(
          fresh.segment && fresh.segment->live() && fresh.segment->binding().request == 2 &&
              service.drainRequests == 2 && service.qtDrainRequests == 1,
          "A late or unsupported reply contaminated the next exact drain"
      );
    }
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Deadline drain fixture did not close");
    check(service.failure.empty(), "Deadline drain fixture failed");
  }
}
void inputDrainOrderingAndExactCancellation(const PrivateBus &bus)
{
  Service service(bus);
  service.advertiseDrain = true;
  service.orderingContract = "sha256:" + std::string(64, 'a');
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  const auto request = client.requestInputDrain(inputDrainRequest());
  const auto result = inputDrainResult(client, request);
  check(
      result.segment && result.segment->senderOrdering() && result.segment->senderOrdering()->live(),
      "Verified exact provider contract did not retain independently live evidence"
  );
  const auto proof = *result.segment->senderOrdering();
  check(
      proof.contract() == service.orderingContract && std::string(result.segment->orderingLimitation()).empty(),
      "Sender-ordering proof changed the negotiated contract"
  );
  check(!client.cancelInputDrain(request + 1) && proof.live(), "Wrong cancellation revoked an exact live receipt");
  check(
      client.cancelInputDrain(request) && !result.segment->live() && !proof.live() && !client.takeInputDrainUpdate() &&
          !client.cancelInputDrain(request),
      "Cancellation did not revoke copied evidence and retire its result exactly once"
  );
  service.delayDrain = true;
  const auto delayed = client.requestInputDrain(inputDrainRequest(2));
  check(delayed != 0, "Fresh drain was blocked after completed cancellation");
  waitUntil([&] { return service.pendingDrain != nullptr; }, "Native cancellation did not reach its pending call");
  check(client.cancelInputDrain(delayed) && !client.takeInputDrainUpdate(), "Pending drain was not canceled");
  g_dbus_method_invocation_return_value(service.pendingDrain, service.pendingDrainReply);
  g_object_unref(std::exchange(service.pendingDrain, nullptr));
  g_variant_unref(std::exchange(service.pendingDrainReply, nullptr));
  std::uint64_t fresh = 0;
  waitUntil(
      [&] {
        fresh = client.requestInputDrain(inputDrainRequest(3));
        return fresh != 0;
      },
      "Canceled bounded native call never released its slot"
  );
  const auto next = inputDrainResult(client, fresh);
  check(
      next.segment && next.segment->binding().request == 3 && next.segment->senderOrdering() &&
          next.segment->senderOrdering()->live() && service.qtDrainRequests == 2 && !client.cancelInputDrain(delayed),
      "Late canceled native reply contaminated fresh sender evidence"
  );
  const auto nextProof = *next.segment->senderOrdering();
  service.invalidatePaint();
  waitUntil(
      [&] { return !nextProof.live(); }, "Gate invalidation did not independently revoke copied ordering evidence"
  );
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Ordering/cancellation fixture did not close");
  check(service.failure.empty(), "Ordering/cancellation fixture failed");
}

void retainedPrefixMaps(const PrivateBus &bus)
{
  Service service(bus);
  service.advertiseDrain = true;
  service.orderingContract = "sha256:" + std::string(64, 'a');
  Configuration configuration;
  Wake wake;
  GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
  const auto ready = wake.next(client);
  Descriptor descriptor(ready.descriptor);
  auto initial = inputDrainRequest(10);
  initial.retainPrefix = true;
  const auto firstRequest = client.requestInputDrain(initial);
  check(firstRequest != 0, "Supported retained prefix request was refused");
  const auto first = inputDrainResult(client, firstRequest);
  check(
      first.segment && first.segment->live() && first.segment->seal() && first.segment->seal()->live(),
      "Initial full drain did not return a distinct live prefix seal"
  );
  const auto seal = *first.segment->seal();
  check(
      seal.binding() == initial.cutoff && seal.contract() == service.orderingContract,
      "Prefix seal lost its exact origin or build contract"
  );
  waitUntil([&] { return !first.segment->live(); }, "Initial stamped point did not expire");
  check(
      seal.live() && !first.segment->senderOrdering()->live() && first.segment->state() == InputCutoffState::Sealed,
      "Historical prefix seal renewed an old point or expired with it"
  );
  auto mapping = inputDrainRequest(11);
  ++mapping.cutoff.capture.sequence;
  ++mapping.point.sequence;
  const auto mappedRequest = client.requestInputMap(seal, mapping);
  check(mappedRequest != 0, "Fresh point under retained prefix was refused");
  auto awaitMap = [&](std::uint64_t id) {
    std::optional<GnomeProviderClient::InputDrainUpdate> result;
    waitUntil(
        [&] {
          if (auto update = client.takeInputMapUpdate(); update && update->request == id)
            result = std::move(update);
          return result.has_value();
        },
        "Fresh sealed map did not complete"
    );
    return std::move(*result);
  };
  const auto mapped = awaitMap(mappedRequest);
  check(
      mapped.segment && mapped.segment->live() && mapped.segment->seal() && mapped.segment->seal()->live() &&
          mapped.segment->state() == InputCutoffState::DrainObserved && mapped.segment->binding() == mapping.cutoff &&
          mapped.segment->snapshot().routedPoint == mapping.point && service.drainRequests == 1 &&
          service.qtDrainRequests == 1 && service.qtMapRequests == 1,
      "Fresh exact point repeated the old-prefix drain or lost capture association"
  );
  // The Qt snapshot retires independently of the caller's request deadline.
  // Reproduce its asynchronous invalidation while the prefix remains sealed.
  service.invalidatePaint();
  waitUntil([&] { return !mapped.segment->live(); }, "Qt snapshot retirement left a mapped point live");
  check(
      mapped.segment->state() == InputCutoffState::Sealed && seal.live() && !mapped.segment->senderOrdering()->live(),
      "Retiring a Qt point revoked held-input ownership or retained delivery authority"
  );
  const auto repeated = client.requestInputMap(seal, mapping);
  check(repeated, "Repeated capture was not correlated through pending admission");
  const auto repeatedResult = awaitMap(repeated);
  check(
      !repeatedResult.segment && repeatedResult.error == "cutoff-not-increasing" && service.qtMapRequests == 1,
      "Repeated capture acquired another sealed map"
  );
  auto changed = inputDrainRequest(12);
  changed.cutoff.capture.sequence += 2;
  changed.point.sequence += 2;
  ++changed.cutoff.forwarding.generation;
  check(client.requestInputMap(seal, changed) == 0, "A new All generation reused an old prefix seal");
  --changed.cutoff.forwarding.generation;
  check(
      !client.requestInputMap(GnomeProviderClient::InputDrainSeal{}, changed), "Default seal acquired native mapping"
  );
  check(
      client.cancelInputDrain(firstRequest) && !seal.live() && !mapped.segment->live() &&
          mapped.segment->state() == InputCutoffState::Denied && !mapped.segment->senderOrdering()->live() &&
          client.live(),
      "Canceling the original seal did not revoke copied maps while preserving the provider"
  );
  check(!client.requestInputMap(seal, changed), "Canceled prefix seal admitted another map");
  auto next = inputDrainRequest(13);
  next.retainPrefix = true;
  const auto nextRequest = client.requestInputDrain(next);
  check(nextRequest, "Fresh retained prefix was blocked after cancellation");
  const auto nextResult = inputDrainResult(client, nextRequest);
  check(nextResult.segment && nextResult.segment->seal(), "Fresh prefix failed after cancellation");
  const auto nextSeal = *nextResult.segment->seal();
  service.invalidateSeal();
  waitUntil([&] { return !nextSeal.live(); }, "Exact Qt seal invalidation did not revoke copied historical evidence");
  check(
      !nextResult.segment->live() && nextResult.segment->state() == InputCutoffState::Denied,
      "Qt prefix revocation left its associated point or backend state live"
  );
  client.stop();
  waitUntil([&] { return service.callerGone; }, "Retained prefix fixture did not close");
  check(service.failure.empty(), "Retained prefix fixture service failed");
}
GnomeProviderClient::InputDrainRequest mappedRequest(std::uint64_t id)
{
  auto result = inputDrainRequest(id);
  result.cutoff.capture.sequence += id;
  result.point.sequence = result.cutoff.capture.sequence;
  return result;
}
GnomeProviderClient::InputDrainUpdate mappedResult(GnomeProviderClient &client, std::uint64_t id)
{
  std::optional<GnomeProviderClient::InputDrainUpdate> result;
  waitUntil(
      [&] {
        result = client.takeInputMapUpdate();
        return result.has_value();
      },
      "Deferred mapped admission did not complete"
  );
  check(result->request == id, "Mapped admission changed its local request ID");
  return std::move(*result);
}
GnomeProviderClient::InputMapBatchUpdate mappedBatchResult(GnomeProviderClient &client, std::uint64_t id)
{
  std::optional<GnomeProviderClient::InputMapBatchUpdate> result;
  waitUntil(
      [&] {
        result = client.takeInputMapBatchUpdate();
        return result.has_value();
      },
      "Deferred batch admission did not complete"
  );
  check(result->request == id, "Batch admission changed its local request ID");
  return std::move(*result);
}
void mappedAdmissionContention(const PrivateBus &bus)
{
  // Exercise scalar/batch handoff, original earliest deadlines, a real Qt seal
  // invalidation, pending cancellation, terminal stop and allocator exhaustion.
  for (unsigned mode = 0; mode < 8; ++mode) {
    Service service(bus);
    service.advertiseDrain = true;
    service.orderingContract = "sha256:" + std::string(64, 'a');
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    auto initial = inputDrainRequest(10);
    initial.retainPrefix = true;
    const auto initialId = client.requestInputDrain(initial);
    const auto first = inputDrainResult(client, initialId);
    check(first.segment && first.segment->seal(), "Admission fixture did not retain a prefix");
    const auto seal = *first.segment->seal();
    auto mapping = mappedRequest(11);
    std::vector<GnomeProviderClient::InputDrainRequest> points;
    for (unsigned i = 0; i < 16; ++i)
      points.push_back(mappedRequest(11 + i));
    const bool batch = mode == 1 || mode == 3;
    if (mode == 2)
      mapping.deadline = Clock::now() + 35ms;
    if (mode == 3)
      points[7].deadline = Clock::now() + 35ms;
    if (mode == 7)
      GnomeProviderClientTestAccess::counter(client).store(UINT64_MAX - 2);
    AdmissionHold hold(client);
    const auto started = Clock::now();
    const auto request = batch ? client.requestInputMapBatch(seal, points) : client.requestInputMap(seal, mapping);
    check(Clock::now() - started < 100ms, "Mapped admission blocked on the worker state mutex");
    check(request, "Valid mapped input was lost solely to state mutex contention");
    const auto rejected = client.requestInputMap(seal, mappedRequest(30));
    check(!rejected, "One pending admission grew a second queue");
    const auto reservedThrough = GnomeProviderClientTestAccess::counter(client).load();
    check(reservedThrough > request, "Rejected CAS publication reused its allocated request ID");
    check(!client.requestViewportObservation(), "Ordinary admission replaced a queued map under contention");
    if (mode == 2 || mode == 3) {
      const auto deadline = mode == 2 ? mapping.deadline : points[7].deadline;
      waitUntil([&] { return Clock::now() >= deadline; }, "Admission deadline did not expire");
    }
    if (mode == 4)
      service.invalidateSeal();
    bool canceled = false;
    std::thread cleanup;
    if (mode == 5)
      cleanup = std::thread([&] { canceled = client.cancelInputDrain(request); });
    if (mode == 6)
      cleanup = std::thread([&] { client.stop(); });
    hold.release();
    if (cleanup.joinable())
      cleanup.join();
    if (mode == 5 || mode == 6) {
      if (mode == 5) {
        waitUntil([&] { return !GnomeProviderClientTestAccess::pending(client); }, "Canceled admission did not settle");
        check(canceled && !seal.live() && !client.takeInputMapUpdate(), "Canceled admission published a late result");
      } else {
        const auto result = mappedResult(client, request);
        check(!result.segment && !result.error.empty() && !seal.live(), "Stopped admission published a live result");
      }
    } else if (mode == 2 || mode == 3 || mode == 4) {
      const auto denied = batch ? mappedBatchResult(client, request).error : mappedResult(client, request).error;
      check(!denied.empty() && (mode == 4 || denied == "deadline-expired"), "Expired or revoked admission was renewed");
      check(
          service.qtMapRequests == 0 && service.qtBatchRequests == 0 && service.nativeBatchQueries == 0,
          "Expired or revoked admission reached native mapping"
      );
      if (mode != 4) {
        mapping.deadline = Clock::now() + 250ms;
        for (auto &point : points)
          point.deadline = mapping.deadline;
        const auto renewed = batch ? client.requestInputMapBatch(seal, points) : client.requestInputMap(seal, mapping);
        check(renewed > request, "Denied admission reused its local ID");
        const auto repeated = batch ? mappedBatchResult(client, renewed).error : mappedResult(client, renewed).error;
        check(repeated == "cutoff-not-increasing", "Replacing the deadline revived a denied cutoff request");
      }
    } else {
      // Inspect completion without consuming it or depending on coalesced
      // notification timing. Neither kind of admission may erase this result.
      waitUntil(
          [&] {
            return service.qtMapRequests + service.qtBatchRequests == 1 &&
                   GnomeProviderClientTestAccess::completed(client);
          },
          "Mapped completion was not published"
      );
      check(
          !client.requestInputMap(seal, mappedRequest(40)) && !client.requestViewportObservation(),
          "Unread mapped completion was overwritten"
      );
      if (batch) {
        const auto result = mappedBatchResult(client, request);
        check(result.error.empty() && result.segments.size() == points.size(), "Deferred batch did not complete");
        for (std::size_t i = 0; i < points.size(); ++i)
          check(
              result.segments[i].binding() == points[i].cutoff &&
                  result.segments[i].snapshot().routedPoint == points[i].point,
              "Deferred batch substituted an exact point"
          );
      } else {
        const auto result = mappedResult(client, request);
        check(
            result.error.empty() && result.segment && result.segment->binding() == mapping.cutoff &&
                result.segment->snapshot().routedPoint == mapping.point,
            "Deferred admission lost its exact point or request"
        );
      }
      if (mode == 7) {
        check(
            request == UINT64_MAX - 1 && GnomeProviderClientTestAccess::counter(client).load() == UINT64_MAX &&
                !client.requestInputMap(seal, mappedRequest(50)) && !client.requestViewportObservation() && seal.live(),
            "Exhausted request counter wrapped or mutated the retained owner"
        );
      } else {
        const auto next = client.requestInputMap(seal, mappedRequest(50));
        check(next > reservedThrough, "Rejected admission ID was reused after result consumption");
        const auto result = mappedResult(client, next);
        check(result.segment && result.segment->live(), "Consumed slot could not admit the next exact map");
      }
    }
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Admission fixture did not close");
    check(service.failure.empty(), "Admission fixture service failed");
  }
}
void mappedAdmissionCancellationAndRace(const PrivateBus &bus)
{
  for (bool race : {false, true}) {
    Service service(bus);
    service.advertiseDrain = true;
    service.orderingContract = "sha256:" + std::string(64, 'a');
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    auto initial = inputDrainRequest(10);
    initial.retainPrefix = true;
    const auto first = inputDrainResult(client, client.requestInputDrain(initial));
    check(first.segment && first.segment->seal(), "Admission race did not retain a prefix");
    const auto seal = *first.segment->seal();
    if (race) {
      std::promise<void> release;
      auto start = release.get_future().share();
      std::uint64_t mapped = 0, ordinary = 0;
      std::thread map([&] {
        start.wait();
        mapped = client.requestInputMap(seal, mappedRequest(11));
      });
      std::thread observe([&] {
        start.wait();
        ordinary = client.requestViewportObservation();
      });
      release.set_value();
      map.join();
      observe.join();
      check(mapped || ordinary, "Racing admissions lost both requests without ownership");
      if (ordinary) {
        const auto observed = viewportResult(client, ordinary);
        check(observed.observation && !seal.live(), "Ordinary winner lost its result or failed to revoke the old seal");
      }
      if (mapped) {
        const auto result = mappedResult(client, mapped);
        check(
            ordinary ? !result.segment : result.segment && result.segment->live(),
            "CAS/ordinary admission race overwrote an unread result or authorized a replaced seal"
        );
      }
    } else {
      service.delaySnapshot = true;
      const auto canceled = client.requestInputMap(seal, mappedRequest(11));
      check(canceled, "In-flight cancellation map was refused");
      waitUntil([&] { return !service.delayedReply.isEmpty(); }, "Map did not enter the real private Qt channel");
      const auto lateBytes = std::exchange(service.delayedReply, {});
      check(
          !client.cancelInputDrain(canceled + 1) && client.cancelInputDrain(canceled) && !seal.live(),
          "Cancellation lost the exact in-flight admission identity"
      );
      waitUntil(
          [&] { return !GnomeProviderClientTestAccess::pending(client); }, "Canceled running slot did not settle"
      );
      check(!client.takeInputMapUpdate(), "Canceled running map published a late completion");
      initial = inputDrainRequest(20);
      initial.retainPrefix = true;
      const auto fresh = inputDrainResult(client, client.requestInputDrain(initial));
      check(fresh.segment && fresh.segment->seal(), "Canceled slot blocked a new sealed owner");
      service.delaySnapshot = true;
      const auto next = client.requestInputMap(*fresh.segment->seal(), mappedRequest(21));
      check(next > canceled, "Replacement slot reused a canceled request ID");
      waitUntil([&] { return !service.delayedReply.isEmpty(); }, "Replacement map did not reach the Qt channel");
      GnomeViewportDecoder late;
      late.append(lateBytes);
      const auto object = late.take();
      check(object.has_value(), "Canceled reply fixture lost its frame");
      service.sendViewport(parseGnomeViewportReply(*object));
      check(
          !client.cancelInputDrain(canceled) && GnomeProviderClientTestAccess::pending(client),
          "Stale cancellation erased the replacement slot"
      );
      service.delayedUntil = Clock::now();
      const auto completed = mappedResult(client, next);
      check(
          completed.segment && completed.segment->live() && fresh.segment->seal()->live(),
          "Late canceled reply erased or completed the replacement request"
      );
    }
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Admission race fixture did not close");
    check(service.failure.empty(), "Admission race fixture service failed");
  }
}
void retainedPrefixBatches(const PrivateBus &bus)
{
  for (unsigned mode = 0; mode < 5; ++mode) {
    Service service(bus);
    service.advertiseDrain = true;
    service.orderingContract = "sha256:" + std::string(64, 'a');
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    auto initial = inputDrainRequest(10);
    initial.retainPrefix = true;
    const auto initialRequest = client.requestInputDrain(initial);
    check(initialRequest, "Batch initial seal refused");
    const auto first = inputDrainResult(client, initialRequest);
    check(first.segment && first.segment->seal(), "Batch initial prefix did not seal");
    const auto seal = *first.segment->seal();
    std::vector<GnomeProviderClient::InputDrainRequest> points;
    for (unsigned i = 0; i < 16; ++i) {
      auto item = inputDrainRequest(11 + i);
      item.cutoff.capture.sequence += i + 1;
      item.point.sequence = item.cutoff.capture.sequence;
      item.point.desktopX += i;
      points.push_back(item);
    }
    check(!client.requestInputMapBatch(seal, {}), "Empty batch reached native calls");
    auto bad = points;
    bad.push_back(bad.back());
    check(!client.requestInputMapBatch(seal, bad), "Oversized batch reached native calls");
    bad = points;
    bad[4].cutoff = bad[3].cutoff;
    check(!client.requestInputMapBatch(seal, bad), "Repeated batch stamp reached native calls");
    bad = points;
    ++bad.back().cutoff.forwarding.generation;
    check(!client.requestInputMapBatch(seal, bad), "Mixed-fence batch reached native calls");
    service.wrongBatchEcho = mode == 1;
    service.denyQtBatch = mode == 2;
    service.driftAfterBatch = mode == 3;
    service.delaySnapshot = mode == 4;
    if (mode == 4)
      points.front().deadline = std::chrono::steady_clock::now() + 25ms;
    const auto request = client.requestInputMapBatch(seal, points);
    check(request, "Valid sealed batch refused admission");
    std::optional<GnomeProviderClient::InputMapBatchUpdate> result;
    waitUntil(
        [&] {
          if (auto update = client.takeInputMapBatchUpdate(); update && update->request == request)
            result = std::move(update);
          return result.has_value();
        },
        "Sealed point batch did not complete"
    );
    if (mode == 0) {
      check(
          result->error.empty() && result->segments.size() == 16 && service.nativeBatchQueries == 2 &&
              service.qtBatchRequests == 1 && service.drainRequests == 1 && service.qtDrainRequests == 1,
          "Point batch did not use one native/Qt/native chain"
      );
      for (std::size_t i = 0; i < points.size(); ++i) {
        const auto &segment = result->segments[i];
        check(
            segment.live() && segment.seal() && segment.seal()->binding() == initial.cutoff &&
                segment.binding() == points[i].cutoff && segment.snapshot().routedPoint == points[i].point &&
                segment.snapshot().sourceX == (points[i].point.desktopX - 100) * 0.8,
            "Batch lost independent exact capture or mapping"
        );
      }
      check(client.cancelInputDrain(initialRequest) && !seal.live(), "Batch cancellation left seal live");
      for (const auto &segment : result->segments)
        check(!segment.live() && !segment.senderOrdering()->live(), "Batch cancellation left one item live");
    } else
      check(
          result->segments.empty() && !result->error.empty(), "Failed native/Qt/deadline batch published partial points"
      );
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Batch fixture did not close");
    check(service.failure.empty(), "Batch fixture service failed");
  }
}
void exactWindowAssociation(const PrivateBus &bus)
{
  for (unsigned mode = 0; mode < 3; ++mode) {
    Service service(bus);
    service.missingAssociationCapability = mode == 0;
    service.wrongWindowAssociation = mode == 1;
    service.duplicateWindowAssociation = mode == 2;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    if (mode == 0) {
      check(ready.closed && !ready.ready && !service.creates, "Missing per-window role capability created a session");
    } else {
      check(ready.ready, "Association fixture failed setup");
      const auto request = client.requestViewportObservation();
      check(request, "Association fixture refused observation");
      const auto result = viewportResult(client, request);
      check(!result.observation && !result.error.empty(), "Substituted/duplicate native role token acquired mapping");
    }
    client.stop();
    if (mode != 0)
      waitUntil([&] { return service.callerGone; }, "Association fixture did not close");
    check(service.failure.empty(), "Association fixture service failed");
  }
}
void inputPreparationFocusPromotionAndCleanup(const PrivateBus &bus)
{
  for (unsigned mode = 0; mode < 9; ++mode) {
    Service service(bus);
    service.advertiseDrain = service.advertisePreparation = true;
    service.orderingContract = "sha256:" + std::string(64, 'a');
    service.noPreparationTarget = mode == 1;
    service.wrongPreparationBinding = mode == 2;
    service.delayPreparationFocus = mode == 3 || mode == 4;
    service.preparationFocusDelayMs = mode == 4 ? 275 : 150;
    service.denyPreparationCleanup = mode == 5;
    service.invalidateFinalPreparation = mode == 6;
    service.activationRestackBeforeReply = mode == 7;
    service.invalidateAfterActivation = mode == 8;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Preparation fixture did not become ready");
    const auto initial = inputDrainRequest(10);
    GnomeProviderClient::InputPreparationRequest request{
        {10, 7, initial.cutoff.capture}, initial.point, Clock::now() + (mode == 3 ? 80ms : 500ms)
    };
    const auto origin = client.requestInputPreparation(request);
    check(origin && !client.requestInputPreparation(request), "Preparation did not retain one exact slot");
    std::optional<GnomeProviderClient::InputPreparationUpdate> prepared;
    waitUntil(
        [&] {
          prepared = client.takeInputPreparationUpdate();
          return prepared.has_value();
        },
        "Preparation focus update timed out"
    );
    check(prepared->request == origin, "Preparation update changed its local request");
    if (mode == 2) {
      check(!prepared->focus && !prepared->error.empty(), "Substituted preparation binding was accepted");
      waitUntil([&] { return !client.live() && service.callerGone; }, "Malformed preparation did not fail closed");
      check(!client.takeInputPreparationCleanupUpdate(), "Malformed native reply manufactured cleanup proof");
      check(service.failure.empty(), "Malformed preparation fixture failed");
      continue;
    }
    std::optional<GnomeProviderClient::InputDrainSeal> seal;
    if (mode == 1 || mode == 3 || mode == 6 || mode == 8) {
      check(!prepared->focus && !prepared->error.empty(), "Refused/late preparation granted focus");
      if (mode == 6)
        check(
            prepared->error == "GNOME preparation invalidated before focus publication" &&
                service.preparationPostActivationQueries == 2 && service.qtPreparationGate,
            "Final native invalidation was discarded before prepared focus publication"
        );
      if (mode == 8)
        check(
            prepared->error == "GNOME preparation focus changed before publication" && service.qtPreparationGate,
            "A restack after completed activation silently renewed the prepared revision"
        );
      if (mode == 1)
        check(
            prepared->outcome == InputPreparationOutcome::NoTarget && !service.preparationActivations,
            "No-target preparation activated or lost its explicit outcome"
        );
    } else {
      check(
          prepared->focus && prepared->focus->live() && prepared->focus->binding() == request.binding &&
              prepared->outcome == InputPreparationOutcome::None && prepared->focus->snapshot().paint.connection == 1 &&
              prepared->focus->snapshot().sourceX == 260 &&
              prepared->focus->snapshot().nativeRevision == service.revision && service.preparationActivations == 1,
          "Ordered preparation failed to return its fresh focus-only receipt"
      );
      const std::vector<std::string> expected{"qt-gate",  "native-select",   "native-drain",
                                              "qt-drain", "native-activate", "qt-focus"};
      check(service.preparationSteps == expected, "Preparation reordered its native/Qt/focus chain");
      auto promotion = inputDrainRequest(11);
      promotion.retainPrefix = true;
      auto wrong = promotion;
      ++wrong.cutoff.viewerConnection;
      check(!client.promoteInputPreparation(*prepared->focus, wrong), "Promotion substituted actual Viewer connection");
      const auto promoted = client.promoteInputPreparation(*prepared->focus, promotion);
      check(promoted && prepared->focus->live(), "Promotion prematurely revoked prepared continuity");
      const auto result = inputDrainResult(client, promoted);
      check(
          result.segment && result.segment->live() && result.segment->seal() && service.qtPreparationGate &&
              service.qtPreparationConsumed && prepared->focus->live(),
          "Promotion removed prep gate or failed to return the genuine All seal"
      );
      seal = *result.segment->seal();
      if (mode == 4) {
        waitUntil([&] { return Clock::now() > request.deadline + 20ms; }, "Preparation clock did not expire");
        check(
            !prepared->focus->live() && seal->live(), "Old focus TTL extended or revoked the independent prefix seal"
        );
      }
      service.qtGateGeneration = 0; // Host resumed All before Core cleanup.
    }
    check(!client.cancelInputPreparation(origin + 100, Clock::now() + 250ms), "Wrong preparation canceled the slot");
    check(client.cancelInputPreparation(origin, Clock::now() + 250ms), "Exact linked preparation cleanup was refused");
    check(!prepared->focus || !prepared->focus->live(), "Cancellation left copied prepared focus live");
    check(!seal || !seal->live(), "Cancellation left the linked prefix seal live");
    std::optional<GnomeProviderClient::InputPreparationCleanupUpdate> cleanup;
    waitUntil(
        [&] {
          cleanup = client.takeInputPreparationCleanupUpdate();
          return cleanup.has_value();
        },
        "Preparation cleanup did not settle"
    );
    check(
        cleanup->request == origin && cleanup->released == (mode != 5),
        "Preparation cleanup changed origin or manufactured a release"
    );
    if (mode != 5)
      check(
          cleanup->error.empty() && !service.qtPreparationGate && !service.nativePrepared &&
              service.preparationCleanups == 1,
          "Preparation cleanup omitted native/Qt gate retirement"
      );
    else
      check(
          !cleanup->error.empty() && service.qtPreparationGate,
          "Failed cleanup reopened preparation or claimed Released"
      );
    if (mode == 3) {
      waitUntil([&] { return service.delayedReply.isEmpty(); }, "Late focus fixture did not send its stale reply");
      check(!client.takeInputPreparationUpdate(), "Late focus reply revived a canceled preparation");
    }
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Preparation fixture did not close");
    check(service.failure.empty(), "Preparation fixture service failed");
  }
}
void inputDrainOrderingCapabilityRefusal(const PrivateBus &bus)
{
  for (int mode = 0; mode < 3; ++mode) {
    Service service(bus);
    service.advertiseDrain = true;
    service.orderingContract = mode == 0 ? "Shell-50.1" : "sha256:" + std::string(64, 'a');
    service.duplicateOrderingCapability = mode == 1;
    if (mode == 2)
      service.orderingContract += "\n";
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(
        !ready.ready && ready.closed && !client.live() && !service.creates,
        "Malformed or duplicate sender contract admitted a provider session"
    );
    client.stop();
    check(service.failure.empty(), "Ordering capability refusal fixture failed");
  }
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
          snapshot.surfaceX == 325 && snapshot.surfaceY == 225 && snapshot.sourceX == 260 && snapshot.sourceY == 180 &&
          snapshot.nativeRevision == service.revision,
      "Provider substituted its current cursor for the frozen Server point"
  );
  check(
      snapshot.desktopX != 400 && snapshot.desktopY != 300,
      "Provider reported the diagnostic cursor as routed coordinates"
  );
  const auto deniedId = client.requestRoutedPointObservation({3, 5, 900, 900});
  check(deniedId != 0 && !result.observation->live(), "New request did not revoke old routed observation");
  const auto denied = viewportResult(client, deniedId);
  check(
      !denied.observation && denied.error == "fixture-native-denied" && client.live(),
      "Point outside the native viewer hit was accepted or unexpectedly ended its session"
  );
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
  check(
      !denied.observation && denied.error.find("substituted") != std::string::npos,
      "Mismatched native point echo became a viewport observation"
  );
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
void stageUpdateObservationCapabilities(const PrivateBus &bus)
{
  // The unadvertised legacy provider remains usable for observation, but
  // cannot invent a compositor stage or original input-drain receipt.
  {
    Service service(bus);
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready && service.stageRequests == 0, "Legacy stage fixture did not start");
    const auto legacy = observed(client);
    check(
        legacy.live() && !legacy.snapshot().stageFramesObserved && service.stageRequests == 0,
        "Legacy provider implicitly claimed Clutter stage observation"
    );
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Legacy stage fixture did not close");
  }
  {
    Service service(bus);
    service.advertiseStage = true;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Stage-capable fixture not Ready");
    const auto routeId = client.requestRoutedPointObservation({9, 32, 425, 325});
    check(routeId != 0, "Stage-capable routed point request was refused");
    const auto result = viewportResult(client, routeId);
    if (!result.observation || !result.observation->live() || service.stageRequests != 1 ||
        !result.observation->snapshot().stageFramesObserved || result.observation->snapshot().desktopX != 425) {
      throw std::runtime_error(
          "Authenticated stage observation failed: requests=" + std::to_string(service.stageRequests) +
          " observation=" + (result.observation ? "present" : "absent") + " error=" + result.error +
          " service=" + service.failure + " client=" + (client.live() ? "live" : "closed")
      );
    }
    service.unavailableStage = true;
    const auto refusedId = client.requestViewportObservation();
    check(refusedId != 0, "Stage-denial request did not start");
    const auto refused = viewportResult(client, refusedId);
    check(
        !refused.observation && refused.error == "fixture-stage-unavailable" && client.live() &&
            service.stageRequests == 2,
        "A stage refusal silently permitted viewport observation or killed the session"
    );
    service.unavailableStage = false;
    check(observed(client).live(), "Fresh native stage after refusal could not recover");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Negotiated stage fixture did not close");
    check(service.failure.empty(), "Negotiated stage fixture failed");
  }
  {
    // Advertising a capability is not a successful stage observation. A
    // provider with a missing method must deny this request without silently
    // falling back to the earlier native/painted snapshot.
    Service service(bus);
    service.advertiseStage = true;
    service.unknownStageMethod = true;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Unknown stage method fixture not Ready");
    const auto request = client.requestRoutedPointObservation({9, 35, 425, 325});
    check(request != 0, "Unknown stage method request did not start");
    const auto denied = viewportResult(client, request);
    check(
        !denied.observation && denied.error.find("UnknownMethod") != std::string::npos && service.stageRequests == 1 &&
            !service.afterQtCheck && client.live(),
        "Missing advertised stage method silently fell back to a viewport observation"
    );
    service.unknownStageMethod = false;
    const auto fresh = observed(client);
    check(
        fresh.live() && fresh.snapshot().request != request && fresh.snapshot().stageFramesObserved &&
            service.stageRequests == 2,
        "A fresh successful stage observation could not recover after a missing method"
    );
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Unknown stage method fixture did not close");
    check(service.failure.empty(), "Unknown stage method fixture failed");
  }
  for (const bool signal : {false, true}) {
    // The real provider returns its current revision when native state
    // invalidates an in-flight stage check. The negative reply must retire
    // that observation, even if its Invalidated signal has not arrived yet.
    Service service(bus);
    service.advertiseStage = true;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Stage invalidation fixture not Ready");
    const auto previous = observed(client);
    const auto previousRevision = previous.snapshot().nativeRevision;
    service.afterQtCheck = false;
    service.changeNativeRevisionDuringStage = true;
    service.signalStageInvalidation = signal;
    const auto request = client.requestRoutedPointObservation({9, 40, 425, 325});
    check(request != 0, "Stage invalidation request did not start");
    const auto denied = viewportResult(client, request);
    check(
        !denied.observation && denied.error == "native-state-invalidated" && !service.afterQtCheck && client.live(),
        "A changed native stage denial killed the session or published an observation"
    );
    if (signal)
      waitUntil([&] { return !previous.live(); }, "Native stage invalidation retained an earlier observation");
    const auto fresh = observed(client);
    check(
        fresh.live() && fresh.snapshot().request != request && fresh.snapshot().nativeRevision > previousRevision &&
            fresh.snapshot().nativeRevision == service.revision && fresh.snapshot().stageFramesObserved,
        "Fresh stage observation could not recover from a native state change"
    );
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Stage invalidation fixture did not close");
    check(service.failure.empty(), "Stage invalidation fixture failed");
  }
  // A denial still authenticates the exact request and point. It cannot
  // disguise substitution, and a successful reply cannot change revision.
  for (int corruption = 0; corruption < 4; ++corruption) {
    Service service(bus);
    service.advertiseStage = true;
    service.unavailableStage = corruption != 0;
    service.wrongStageRevision = corruption == 0;
    service.wrongStagePoint = corruption == 1;
    service.wrongStageRequest = corruption == 2;
    service.emptyStageReason = corruption == 3;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Stage binding fixture not Ready");
    const auto request = client.requestViewportObservation();
    check(request != 0, "Stage binding request did not start");
    const auto denied = viewportResult(client, request);
    check(!denied.observation && !denied.error.empty(), "Malformed stage binding published an observation");
    waitUntil([&] { return !client.live(); }, "Malformed stage binding did not retire private provider");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Stage binding fixture did not close");
    check(service.failure.empty(), "Stage binding fixture failed");
  }
  // A dictionary with thirteen entries can still substitute an unknown
  // field or repeat a known one; GVariant lookup alone does not reject either.
  for (int corruption = 0; corruption < 3; ++corruption) {
    Service service(bus);
    service.advertiseStage = true;
    service.duplicateStageField = corruption == 0;
    service.substituteStageField = corruption == 1;
    service.wrongStageFieldType = corruption == 2;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Malformed stage dictionary fixture not Ready");
    const auto request = client.requestRoutedPointObservation({9, 38u + static_cast<unsigned>(corruption), 425, 325});
    check(request != 0, "Malformed stage dictionary request not queued");
    const auto result = viewportResult(client, request);
    check(
        !result.observation && result.error == "Malformed GNOME stage observation field schema" &&
            service.stageRequests == 1,
        "Malformed or duplicate native stage schema published an observation"
    );
    waitUntil([&] { return !client.live(); }, "Malformed stage dictionary did not retire private provider");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Malformed stage dictionary did not close");
    check(service.failure.empty(), "Malformed stage dictionary service failed");
  }
  for (const bool substitute : {true, false}) {
    Service service(bus);
    service.advertiseStage = true;
    service.wrongStagePoint = substitute;
    service.shortStageReply = !substitute;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    check(ready.ready, "Malformed stage fixture not Ready");
    const auto request = client.requestRoutedPointObservation({9, substitute ? 33u : 34u, 425, 325});
    check(request != 0, "Malformed stage request did not begin");
    const auto reply = viewportResult(client, request);
    check(
        !reply.observation && !reply.error.empty() && service.stageRequests == 1,
        "Substituted routed stage point or incomplete cycle created a viewport observation"
    );
    waitUntil([&] { return !client.live(); }, "Malformed authenticated stage reply did not retire provider session");
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Malformed stage fixture not cleaned");
  }
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
        !result.observation && result.error == "fixture-native-denied" && service.afterQtCheck &&
            service.nativeHitQueries >= 3 && client.live(),
        "A silently replaced native revision escaped the post-Qt queue boundary"
    );
    // Rejection is bounded and nonterminal: the new native revision may
    // establish its own fresh snapshot, not revive the first one.
    const auto next = observed(client);
    check(
        next.live() && next.snapshot().nativeRevision == service.revision,
        "Rejected post-Qt native change poisoned all future observations"
    );
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

void nativeWindowDragChannel(const PrivateBus &bus)
{
  for (unsigned fault = 0; fault != 4; ++fault) {
    Service service(bus);
    service.advertiseWindowDrag = fault != 1;
    service.wrongDragBinding = fault == 2;
    service.duplicateDragField = fault == 3;
    Configuration configuration;
    Wake wake;
    GnomeProviderClient client(configuration.load(), [&] { wake.changed(); });
    const auto ready = wake.next(client);
    Descriptor descriptor(ready.descriptor);
    GnomeWindowDragRequest request{
        1, 7, 18, service.preparationPaint().windowAssociation, GnomeWindowDragAction::Place, -200, 45
    };
    const auto send = [&](const GnomeWindowDragRequest &value) {
      const auto frame = gnomeViewportFrame(gnomeWindowDragJson(value));
      check(
          ::send(service.viewportPeer.value, frame.constData(), frame.size(), 0) == frame.size(),
          "Native drag request fixture write failed"
      );
    };
    send(request);
    if (fault >= 2) {
      waitUntil([&] { return !client.live(); }, "Malformed native drag result did not retire the channel");
      check(service.dragReplies.empty(), "Malformed compositor placement reached the Viewer");
    } else {
      waitUntil([&] { return !service.dragReplies.empty(); }, "Native drag result did not arrive");
      const auto &reply = service.dragReplies.back();
      check(
          reply.binding == request && reply.ok == (fault == 0) && service.nativeDragRequests == (fault == 0 ? 1u : 0u),
          "Native drag capability or exact binding was not enforced"
      );
      if (!fault) {
        check(
            reply.frame == QRect(-200, 45, 640, 480) && reply.revision == 1,
            "Native compositor geometry was not preserved"
        );
        request.request = 2;
        request.action = GnomeWindowDragAction::Watch;
        request.x = request.y = 0;
        send(request);
        waitUntil([&] { return service.dragReplies.size() == 2; }, "Native watcher was not acknowledged");
        check(service.dragReplies.back().ok, "Native watcher was refused");
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&values, "{sv}", "viewer", g_variant_new_string(viewerId));
        g_variant_builder_add(&values, "{sv}", "phase", g_variant_new_string("begin"));
        g_variant_builder_add(&values, "{sv}", "grab", g_variant_new_uint64(4));
        g_variant_builder_add(&values, "{sv}", "sequence", g_variant_new_uint64(1));
        for (const auto *key : {"x", "y", "frameX", "frameY"})
          g_variant_builder_add(&values, "{sv}", key, g_variant_new_int32(20));
        g_variant_builder_add(&values, "{sv}", "width", g_variant_new_int32(640));
        g_variant_builder_add(&values, "{sv}", "height", g_variant_new_int32(480));
        g_variant_builder_add(&values, "{sv}", "buttonReleased", g_variant_new_boolean(false));
        Error error;
        const auto association = request.association.toUtf8();
        check(
            g_dbus_connection_emit_signal(
                service.connection, service.caller.c_str(), objectPath, serviceName, "WindowDragChanged",
                g_variant_new("(susa{sv})", sessionId, generation, association.constData(), &values), &error.value
            ),
            "Native drag signal fixture failed"
        );
        waitUntil([&] { return service.dragEvents.size() == 1; }, "Native drag signal did not reach owned Viewer");
        check(
            service.dragEvents[0].association == request.association && service.dragEvents[0].grab == 4 &&
                service.dragEvents[0].phase == GnomeWindowDragEvent::Phase::Begin,
            "Native drag signal lost its exact association"
        );
        for (unsigned cycle = 0; cycle != 24; ++cycle) {
          // Retire the old window, including the original observed association,
          // then watch a fresh one on the same authenticated private channel.
          ++request.request;
          request.action = GnomeWindowDragAction::Unwatch;
          send(request);
          waitUntil([&] { return service.dragReplies.size() == request.request; }, "Unwatch reply missing");
          check(service.dragReplies.back().ok, "Unwatch failed");
          ++request.request;
          request.association =
              QStringLiteral("org.deskflow.window.") + QString::number(cycle, 16).rightJustified(64, '0');
          request.action = GnomeWindowDragAction::Watch;
          send(request);
          waitUntil([&] { return service.dragReplies.size() == request.request; }, "Replacement watch reply missing");
          check(service.dragReplies.back().ok, "Retired windows exhausted the live native watcher bound");
        }
        // Placement and observation use a separate counter from input viewport
        // operations. A fresh read remains usable after native drag traffic.
        const auto observation = client.requestViewportObservation();
        check(
            observation && viewportResult(client, observation).observation.has_value(),
            "Native drag traffic contaminated viewport request state"
        );
      }
    }
    client.stop();
    waitUntil([&] { return service.callerGone; }, "Native drag fixture session did not close");
    check(service.failure.empty(), "Native drag fixture failed");
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
    nativeWindowDragChannel(bus);
    std::puts(
        "PASS: native placement/watcher channel binds exact owned windows and rejects malformed compositor replies"
    );
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
    stageUpdateObservationCapabilities(bus);
    std::puts("PASS: optional native stage update counts/binding are tested without an input-drain permit");
    inputDrainBindingAndRevocation(bus);
    std::puts("PASS: compositor/Qt drain retains exact All cutoff and revokes with the private viewport");
    inputDrainMalformedAndInvalidated(bus);
    std::puts("PASS: malformed, substituted and invalidated drain replies cannot publish a segment");
    inputDrainDeadlineAndCapabilities(bus);
    std::puts("PASS: missing drain capability, inherited deadline and late replies preserve the finite request");
    inputDrainOrderingAndExactCancellation(bus);
    std::puts(
        "PASS: sender build evidence is exact, independently revocable, and late canceled calls cannot revive it"
    );
    retainedPrefixMaps(bus);
    mappedAdmissionContention(bus);
    mappedAdmissionCancellationAndRace(bus);
    std::puts("PASS: bounded map admission preserves contention, deadlines, cancellation, results and IDs");
    retainedPrefixBatches(bus);
    exactWindowAssociation(bus);
    inputPreparationFocusPromotionAndCleanup(bus);
    std::puts(
        "PASS: one retained prefix drain supports fresh stamped maps without extending old points or surviving resume"
    );
    inputDrainOrderingCapabilityRefusal(bus);
    std::puts("PASS: malformed and duplicate sender build capabilities fail before session creation");
    viewportWrongAuthenticatedPeer(bus);
    std::puts("PASS: wrong authenticated viewer peer ends the viewport session");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
