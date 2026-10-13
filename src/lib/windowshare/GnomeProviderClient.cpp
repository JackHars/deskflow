/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderClient.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <limits>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace deskflow::windowshare {
namespace {
constexpr auto service = "org.deskflow.Seamless.Gnome50";
constexpr auto object = "/org/deskflow/Seamless/Gnome50";
constexpr auto interface = "org.deskflow.Seamless.Gnome50";
constexpr int callTimeoutMs = 5000;
using Clock = std::chrono::steady_clock;

struct ChannelError : std::runtime_error
{
  using std::runtime_error::runtime_error;
};
struct DescriptorGuard
{
  int value = -1;
  ~DescriptorGuard()
  {
    if (value >= 0)
      ::close(value);
  }
};

struct Error
{
  GError *value = nullptr;
  ~Error()
  {
    g_clear_error(&value);
  }
  std::string message(const char *fallback) const
  {
    return value && value->message ? std::string(value->message).substr(0, 512) : fallback;
  }
};
struct Variant
{
  GVariant *value = nullptr;
  explicit Variant(GVariant *value = nullptr) : value(value)
  {
  }
  ~Variant()
  {
    if (value)
      g_variant_unref(value);
  }
  Variant(const Variant &) = delete;
  Variant &operator=(const Variant &) = delete;
};
struct DescriptorList
{
  GUnixFDList *value = nullptr;
  ~DescriptorList()
  {
    if (value)
      g_object_unref(value);
  }
};
bool boolean(GVariant *dictionary, const char *key)
{
  gboolean value = false;
  return g_variant_lookup(dictionary, key, "b", &value) && value;
}
bool bounded(const char *text)
{
  return text && *text && std::strlen(text) <= 128;
}
} // namespace

struct GnomeProviderClient::Impl
{
  GnomeProviderConfig configuration;
  std::function<void()> changed;
  std::atomic<bool> stopping = false, alive = false, routedPointSupported = false;
  std::mutex mutex;
  Update update;
  std::optional<ViewportUpdate> viewportUpdate;
  struct Request
  {
    std::uint64_t id;
    Clock::time_point deadline;
    std::optional<RoutedPoint> routedPoint;
  };
  std::optional<Request> viewportRequest;
  std::shared_ptr<std::atomic_bool> viewportRevoked;
  bool notified = false, viewportNotified = false, viewportBusy = false;
  std::uint64_t nextViewportRequest = 0, lastViewportRequest = 0;
  GCancellable *cancel = g_cancellable_new();
  std::thread worker;
  // Worker-owned native objects below; never dereferenced by the consumer.
  GDBusConnection *bus = nullptr;
  GMainContext *context = nullptr;
  GMainLoop *loop = nullptr;
  std::string owner, session, viewer;
  guint generation = 0, ownerSignal = 0, closeSignal = 0, invalidationSignal = 0;
  bool sessionCreated = false, terminal = false;
  DescriptorGuard viewportDescriptor;
  GnomeViewportDecoder viewportDecoder;
  std::uint64_t wireIssued = 0, wireRetired = 0, activeSnapshot = 0, invalidatedThrough = 0;
  std::uint64_t nativeInvalidations = 0;

  Impl(GnomeProviderConfig config, std::function<void()> notify)
      : configuration(std::move(config)),
        changed(std::move(notify))
  {
    try {
      worker = std::thread([this] { run(); });
    } catch (...) {
      g_object_unref(cancel);
      throw;
    }
  }
  ~Impl()
  {
    stop();
    if (worker.joinable())
      worker.join();
    if (update.descriptor >= 0)
      ::close(update.descriptor);
    g_object_unref(cancel);
  }
  void notify(bool viewport = false)
  {
    bool send = false;
    {
      const std::lock_guard lock(mutex);
      send = !std::exchange(viewport ? viewportNotified : notified, true);
    }
    if (send && changed) {
      try {
        changed();
      } catch (...) {
        stop();
      }
    }
  }
  void stop() noexcept
  {
    {
      const std::lock_guard lock(mutex);
      stopping.store(true, std::memory_order_release);
      alive.store(false, std::memory_order_release);
      if (viewportRevoked)
        viewportRevoked->store(true, std::memory_order_release);
    }
    g_cancellable_cancel(cancel);
  }
  void fail(std::string_view error) noexcept
  {
    terminal = true;
    alive.store(false, std::memory_order_release);
    try {
      const std::lock_guard lock(mutex);
      if (viewportRevoked)
        viewportRevoked->store(true, std::memory_order_release);
      if (update.descriptor >= 0)
        ::close(std::exchange(update.descriptor, -1));
      update.ready = false;
      update.closed = true;
      update.error.assign(error.substr(0, 512));
    } catch (...) {
      // Lifecycle failure must never unwind through a GLib C callback.
    }
    try {
      notify();
    } catch (...) {
      stopping.store(true, std::memory_order_release);
      g_cancellable_cancel(cancel);
    }
    if (loop)
      g_main_loop_quit(loop);
  }
  GVariant *call(const char *method, GVariant *parameters, const GVariantType *replyType)
  {
    Error error;
    auto *reply = g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface, method, parameters, replyType, G_DBUS_CALL_FLAGS_NO_AUTO_START,
        callTimeoutMs, cancel, &error.value
    );
    if (!reply)
      throw std::runtime_error(error.message("GNOME provider call failed"));
    return reply;
  }
  std::string busOwner(const char *name)
  {
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
        g_variant_new("(s)", name), G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, callTimeoutMs, cancel,
        &error.value
    ));
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME provider is not enabled"));
    const char *value = nullptr;
    g_variant_get(reply.value, "(&s)", &value);
    if (!bounded(value) || value[0] != ':')
      throw std::runtime_error("Invalid GNOME provider bus owner");
    return value;
  }
  void initialize()
  {
    Error error;
    auto *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, cancel, &error.value);
    if (!address)
      throw std::runtime_error(error.message("Cannot locate the session bus"));
    bus = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION
        ),
        nullptr, cancel, &error.value
    );
    g_free(address);
    if (!bus || !(g_dbus_connection_get_capabilities(bus) & G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING))
      throw std::runtime_error(error.message("GNOME provider requires a private Unix FD-capable session bus connection")
      );
    g_dbus_connection_set_exit_on_close(bus, false);
    owner = busOwner(service);
    if (owner != busOwner("org.gnome.Shell"))
      throw std::runtime_error("GNOME provider is not owned by the running GNOME Shell");
    // Subscribe before requests, then recheck both names after setup. Calls
    // always target the captured unique name, never a replacement service.
    ownerSignal = g_dbus_connection_signal_subscribe(
        bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
           gpointer data) {
          auto &self = *static_cast<Impl *>(data);
          const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
          g_variant_get(parameters, "(&s&s&s)", &name, &oldOwner, &newOwner);
          if ((!std::strcmp(name, service) || !std::strcmp(name, "org.gnome.Shell")) && self.owner != newOwner)
            self.fail("GNOME provider or Shell owner changed");
        },
        this, nullptr
    );
    closeSignal = g_dbus_connection_signal_subscribe(
        bus, owner.c_str(), interface, "Closed", object, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
           gpointer data) {
          auto &self = *static_cast<Impl *>(data);
          if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sus)"))) {
            self.fail("Malformed GNOME provider close signal");
            return;
          }
          const char *id = nullptr, *reason = nullptr;
          guint value = 0;
          g_variant_get(parameters, "(&su&s)", &id, &value, &reason);
          if (self.session == id && self.generation == value)
            self.fail(reason);
        },
        this, nullptr
    );
    invalidationSignal = g_dbus_connection_signal_subscribe(
        bus, owner.c_str(), interface, "Invalidated", object, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
           gpointer data) {
          auto &self = *static_cast<Impl *>(data);
          try {
            if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(suus)")))
              throw ChannelError("Malformed GNOME invalidation signal");
            const char *id = nullptr, *reason = nullptr;
            guint value = 0, revision = 0;
            g_variant_get(parameters, "(&suu&s)", &id, &value, &revision, &reason);
            if (self.session == id && self.generation == value) {
              if (self.nativeInvalidations == std::numeric_limits<std::uint64_t>::max())
                throw ChannelError("GNOME invalidation counter exhausted");
              ++self.nativeInvalidations;
              self.revokeViewport(reason);
            }
          } catch (const std::exception &error) {
            self.fail(error.what());
          } catch (...) {
            self.fail("GNOME invalidation failed");
          }
        },
        this, nullptr
    );
    Variant capabilities(call("GetCapabilities", nullptr, G_VARIANT_TYPE("(usa{sv})")));
    guint api = 0;
    const char *shellVersion = nullptr;
    GVariant *values = nullptr;
    g_variant_get(capabilities.value, "(u&s@a{sv})", &api, &shellVersion, &values);
    Variant dictionary(values);
    const char *backend = nullptr;
    if (api != 1 || !bounded(shellVersion) || std::strncmp(shellVersion, "50", 2) ||
        (shellVersion[2] != 0 && shellVersion[2] != '.') || !boolean(values, "exactWaylandClientBinding") ||
        !boolean(values, "eisSessionOwned") || !boolean(values, "nativeHitTesting") ||
        !boolean(values, "viewportChannel") || !g_variant_lookup(values, "backend", "&s", &backend) ||
        std::strcmp(backend, "MetaBackendNative"))
      throw std::runtime_error("Unsupported GNOME provider capabilities or Shell version");
    routedPointSupported.store(boolean(values, "routedPointObservation"), std::memory_order_release);
    DescriptorList descriptors;
    Variant created(g_dbus_connection_call_with_unix_fd_list_sync(
        bus, owner.c_str(), object, interface, "CreateSession", nullptr, G_VARIANT_TYPE("(suh)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, callTimeoutMs, nullptr, &descriptors.value, cancel, &error.value
    ));
    if (!created.value) {
      throw std::runtime_error(error.message("Cannot create GNOME-owned EIS session"));
    }
    const char *id = nullptr;
    gint handle = -1;
    g_variant_get(created.value, "(&suh)", &id, &generation, &handle);
    if (!bounded(id) || !generation || !descriptors.value || g_unix_fd_list_get_length(descriptors.value) != 1 ||
        handle != 0) {
      throw std::runtime_error("Invalid GNOME-owned EIS session reply");
    }
    session = id;
    sessionCreated = true;
    int descriptor = g_unix_fd_list_get(descriptors.value, handle, &error.value);
    if (descriptor < 0)
      throw std::runtime_error(error.message("Cannot adopt GNOME EIS descriptor"));
    DescriptorGuard guard{descriptor};
    int kind = 0;
    socklen_t length = sizeof(kind);
    sockaddr_storage addressValue{};
    socklen_t addressLength = sizeof(addressValue);
    if (::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &kind, &length) || kind != SOCK_STREAM ||
        ::getsockname(descriptor, reinterpret_cast<sockaddr *>(&addressValue), &addressLength) ||
        addressValue.ss_family != AF_UNIX || ::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
      throw std::runtime_error("GNOME EIS descriptor is not a Unix stream socket");
    GVariantBuilder arguments;
    g_variant_builder_init(&arguments, G_VARIANT_TYPE("as"));
    for (const auto &argument : configuration.viewerArguments())
      g_variant_builder_add(&arguments, "s", argument.toUtf8().constData());
    int endpoints[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, endpoints))
      throw std::runtime_error("Cannot create private GNOME viewport channel");
    viewportDescriptor.value = endpoints[0];
    DescriptorGuard child{endpoints[1]};
    for (const auto fd : endpoints) {
      const auto flags = ::fcntl(fd, F_GETFL);
      if (flags < 0 || ::fcntl(fd, F_SETFD, FD_CLOEXEC) || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK))
        throw std::runtime_error("Cannot protect private GNOME viewport descriptor");
#ifdef SO_NOSIGPIPE
      const int noSigpipe = 1;
      if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe)))
        throw std::runtime_error("Cannot protect GNOME viewport writes");
#endif
    }
    DescriptorList launchDescriptors{g_unix_fd_list_new()}, returnedDescriptors;
    const auto viewportHandle = g_unix_fd_list_append(launchDescriptors.value, child.value, &error.value);
    if (viewportHandle != 0)
      throw std::runtime_error(error.message("Cannot pass GNOME viewport descriptor"));
    Variant launched(g_dbus_connection_call_with_unix_fd_list_sync(
        bus, owner.c_str(), object, interface, "LaunchViewerWithViewport",
        g_variant_new(
            "(susash)", session.c_str(), generation, configuration.viewerExecutable().toUtf8().constData(), &arguments,
            viewportHandle
        ),
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, callTimeoutMs, launchDescriptors.value,
        &returnedDescriptors.value, cancel, &error.value
    ));
    if (!launched.value || (returnedDescriptors.value && g_unix_fd_list_get_length(returnedDescriptors.value)))
      throw std::runtime_error(error.message("Cannot launch GNOME viewer with its private viewport channel"));
    const char *viewerId = nullptr;
    g_variant_get(launched.value, "(&s)", &viewerId);
    if (!bounded(viewerId))
      throw std::runtime_error("Invalid GNOME viewer identity");
    viewer = viewerId;
    if (busOwner(service) != owner || busOwner("org.gnome.Shell") != owner || stopping.load())
      throw std::runtime_error("GNOME provider changed during setup");
    // Synchronous setup leaves lifecycle signals queued on this private
    // context. Drain them before making the descriptor visible. A terminal
    // callback can never be undone by the subsequent Ready publication.
    unsigned dispatched = 0;
    while (g_main_context_pending(context)) {
      if (++dispatched > 64)
        throw std::runtime_error("GNOME provider lifecycle did not settle during setup");
      g_main_context_iteration(context, false);
    }
    {
      const std::lock_guard lock(mutex);
      if (terminal || stopping.load() || g_dbus_connection_is_closed(bus))
        throw std::runtime_error("GNOME provider ended during setup");
      update.descriptor = std::exchange(guard.value, -1);
      update.ready = true;
      update.session = session;
      update.viewer = viewer;
      update.generation = generation;
      alive.store(true, std::memory_order_release);
    }
    notify();
  }
  void revokeViewport(std::string_view reason)
  {
    bool changed = false;
    {
      const std::lock_guard lock(mutex);
      if (viewportRevoked && !viewportRevoked->exchange(true, std::memory_order_acq_rel)) {
        viewportUpdate = ViewportUpdate{lastViewportRequest, std::string(reason.substr(0, 128)), std::nullopt};
        changed = true;
      }
    }
    if (changed)
      notify(true);
  }
  void checkpoint(Clock::time_point deadline)
  {
    unsigned dispatched = 0;
    while (g_main_context_pending(context)) {
      if (++dispatched > 64)
        throw ChannelError("GNOME lifecycle notifications exceeded their bound");
      g_main_context_iteration(context, false);
    }
    if (terminal || stopping.load() || !alive.load() || g_dbus_connection_is_closed(bus))
      throw std::runtime_error("GNOME viewport session ended");
    if (Clock::now() >= deadline)
      throw std::runtime_error("viewport-timeout");
  }
  int remainingMs(Clock::time_point deadline)
  {
    checkpoint(deadline);
    return static_cast<int>(std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()
    ));
  }
  void waitSocket(short events, Clock::time_point deadline)
  {
    pollfd pollDescriptor{viewportDescriptor.value, events, 0};
    const auto result = ::poll(&pollDescriptor, 1, std::min(remainingMs(deadline), 10));
    if ((result < 0 && errno != EINTR) || (pollDescriptor.revents & (POLLERR | POLLNVAL)))
      throw ChannelError("GNOME viewport channel failed");
    // POLLHUP is consumed by recv(), after any final buffered invalidation.
    checkpoint(deadline);
  }
  std::optional<GnomeViewportReply> readViewportReply()
  {
    try {
      if (const auto object = viewportDecoder.take())
        return parseGnomeViewportReply(*object);
      char bytes[4096];
      const auto count = ::recv(viewportDescriptor.value, bytes, sizeof(bytes), MSG_DONTWAIT);
      if (count == 0)
        throw ChannelError("GNOME viewport channel closed");
      if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          return std::nullopt;
        throw ChannelError("GNOME viewport read failed");
      }
      viewportDecoder.append(QByteArray(bytes, static_cast<qsizetype>(count)));
      if (const auto object = viewportDecoder.take())
        return parseGnomeViewportReply(*object);
      return std::nullopt;
    } catch (const std::exception &error) {
      throw ChannelError(error.what());
    }
  }
  bool retiredReply(const GnomeViewportReply &reply)
  {
    if (reply.operation == GnomeViewportOperation::Invalidated) {
      if (!reply.snapshot || reply.snapshot > wireIssued)
        throw ChannelError("GNOME viewport invalidation names an unknown snapshot");
      invalidatedThrough = std::max(invalidatedThrough, reply.snapshot);
      if (activeSnapshot && reply.snapshot >= activeSnapshot)
        revokeViewport(reply.reason.toStdString());
      return true;
    }
    return reply.request <= wireRetired;
  }
  void drainViewport()
  {
    for (unsigned i = 0; i < 16; ++i) {
      const auto reply = readViewportReply();
      if (!reply)
        return;
      if (!retiredReply(*reply))
        throw ChannelError("Unexpected GNOME viewport reply");
    }
    throw ChannelError("GNOME viewport reply queue exceeded its bound");
  }
  GnomeViewportReply viewportCall(
      GnomeViewportOperation operation, Clock::time_point deadline, std::uint64_t snapshot = 0, double x = 0,
      double y = 0
  )
  {
    checkpoint(deadline);
    if (wireIssued == std::numeric_limits<std::uint64_t>::max())
      throw ChannelError("GNOME viewport request counter exhausted");
    const auto id = ++wireIssued;
    const auto frame = gnomeViewportFrame(gnomeViewportJson(GnomeViewportRequest{operation, id, snapshot, x, y}));
    qsizetype sent = 0;
    try {
      while (sent < frame.size()) {
        checkpoint(deadline);
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto count = ::send(
            viewportDescriptor.value, frame.constData() + sent, static_cast<std::size_t>(frame.size() - sent), flags
        );
        if (count > 0) {
          sent += count;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
          waitSocket(POLLOUT, deadline);
        } else {
          throw ChannelError("GNOME viewport write failed");
        }
      }
    } catch (...) {
      if (sent > 0 && sent < frame.size())
        throw ChannelError("GNOME viewport write ended within a frame");
      throw;
    }
    unsigned received = 0;
    for (;;) {
      checkpoint(deadline);
      const auto reply = readViewportReply();
      if (!reply) {
        waitSocket(POLLIN, deadline);
        continue;
      }
      if (++received > 16)
        throw ChannelError("GNOME viewport reply queue exceeded its bound");
      if (retiredReply(*reply))
        continue;
      if (reply->request != id || reply->operation != operation ||
          (reply->ok && operation != GnomeViewportOperation::Snapshot && reply->snapshot != snapshot))
        throw ChannelError("GNOME viewport reply does not match its request");
      wireRetired = id;
      if (!reply->ok)
        throw std::runtime_error(reply->reason.toStdString());
      if (snapshot && invalidatedThrough >= snapshot)
        throw std::runtime_error("Qt viewport changed during observation");
      return *reply;
    }
  }
  struct NativeViewport
  {
    std::string window, surface;
    std::uint32_t revision = 0;
    double x = 0, y = 0, localX = 0, localY = 0;
  };
  NativeViewport
  nativeViewport(
      Clock::time_point deadline, const NativeViewport *expected = nullptr, std::uint64_t request = 0,
      const RoutedPoint *routedPoint = nullptr
  )
  {
    if (routedPoint && !expected)
      throw ChannelError("Routed point is missing its native session binding");
    const auto requestText = std::to_string(request);
    const auto timeout = remainingMs(deadline);
    const double x = routedPoint ? routedPoint->desktopX : expected ? expected->x : 0;
    const double y = routedPoint ? routedPoint->desktopY : expected ? expected->y : 0;
    Variant parameters(g_variant_ref_sink(
        expected ? g_variant_new(
                       "(sussddu)", session.c_str(), generation, viewer.c_str(), requestText.c_str(), x, y,
                       expected->revision
                   )
                 : g_variant_new("(sus)", session.c_str(), generation, viewer.c_str())
    ));
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface,
        expected ? routedPoint ? "ValidateRoutedPoint" : "ValidateCurrentHit" : "GetViewer", parameters.value,
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, timeout, cancel, &error.value
    ));
    checkpoint(deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME native viewport query failed"));
    Variant values(g_variant_get_child_value(reply.value, 0));
    if (g_variant_n_children(values.value) > 24)
      throw ChannelError("GNOME native viewport reply exceeds its bound");
    const auto text = [&](const char *key) -> std::string {
      const char *value = nullptr;
      if (!g_variant_lookup(values.value, key, "&s", &value) || !bounded(value))
        throw ChannelError("Invalid GNOME native viewport identity");
      return value;
    };
    const auto number = [&](const char *key) {
      double value = 0;
      if (!g_variant_lookup(values.value, key, "d", &value) || !std::isfinite(value) || std::abs(value) > 1e7)
        throw ChannelError("Invalid GNOME native viewport coordinate");
      return value;
    };
    guint value = 0;
    gboolean available = false, observationOnly = false, deliveryReady = true;
    if (text("session") != session || text("viewer") != viewer ||
        !g_variant_lookup(values.value, "generation", "u", &value) || value != generation ||
        !g_variant_lookup(values.value, "available", "b", &available) ||
        !g_variant_lookup(values.value, "observationOnly", "b", &observationOnly) || !observationOnly ||
        !g_variant_lookup(values.value, "deliveryReady", "b", &deliveryReady) || deliveryReady ||
        (expected && text("request") != requestText))
      throw ChannelError("GNOME native viewport reply changed its binding");
    if (!available)
      throw std::runtime_error(text("reason"));
    NativeViewport result;
    result.window = text("window");
    result.surface = text("surface");
    if (!g_variant_lookup(values.value, "revision", "u", &result.revision) || !result.revision ||
        !boolean(values.value, "focused"))
      throw ChannelError("GNOME native viewport is not focused or revisioned");
    result.x = number("pointerX");
    result.y = number("pointerY");
    if (expected) {
      if (routedPoint) {
        // Echoes are part of the exact-point contract. PointerX/Y remain
        // diagnostic cursor samples and are never substituted for this point.
        if (number("routedX") != routedPoint->desktopX || number("routedY") != routedPoint->desktopY)
          throw ChannelError("GNOME native viewport substituted the routed point");
        result.x = routedPoint->desktopX;
        result.y = routedPoint->desktopY;
      }
      result.localX = number("localX");
      result.localY = number("localY");
      if (result.window != expected->window || result.surface != expected->surface ||
          result.revision != expected->revision ||
          (!routedPoint && (result.x != expected->x || result.y != expected->y)))
        throw std::runtime_error("GNOME native viewport changed during observation");
    }
    return result;
  }
  std::uint64_t scheduleViewport(std::optional<RoutedPoint> routedPoint)
  {
    const auto requestedAt = Clock::now();
    const std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock.owns_lock() || !alive.load(std::memory_order_acquire) || stopping.load() ||
        (routedPoint && !routedPointSupported.load(std::memory_order_acquire)) || viewportBusy || viewportUpdate ||
        nextViewportRequest == std::numeric_limits<std::uint64_t>::max())
      return 0;
    if (viewportRevoked)
      viewportRevoked->store(true, std::memory_order_release);
    const auto id = ++nextViewportRequest;
    viewportRequest = Request{id, requestedAt + std::chrono::milliseconds(gnomeViewportLifetimeMs), routedPoint};
    viewportBusy = true;
    return id;
  }
  void observePendingViewport()
  {
    drainViewport();
    std::optional<Request> request;
    {
      const std::lock_guard lock(mutex);
      request = std::exchange(viewportRequest, std::nullopt);
    }
    if (!request)
      return;
    ViewportUpdate result{request->id, {}, std::nullopt};
    bool channelFailed = false;
    try {
      const auto native = nativeViewport(request->deadline);
      const auto nativeGeneration = nativeInvalidations;
      const auto painted = viewportCall(GnomeViewportOperation::Snapshot, request->deadline);
      activeSnapshot = painted.request;
      if (painted.state.peerFingerprint != configuration.peerFingerprint())
        throw ChannelError("GNOME viewer reported a different authenticated peer");
      const auto *routed = request->routedPoint ? &*request->routedPoint : nullptr;
      const auto hit = nativeViewport(request->deadline, &native, request->id, routed);
      const auto mapped =
          viewportCall(GnomeViewportOperation::Map, request->deadline, activeSnapshot, hit.localX, hit.localY);
      if (mapped.x < 0 || mapped.y < 0 || mapped.x >= painted.state.sourceWidth ||
          mapped.y >= painted.state.sourceHeight)
        throw ChannelError("GNOME viewport mapped outside its source");
      const auto fresh = nativeViewport(request->deadline, &native, request->id, routed);
      if (fresh.localX != hit.localX || fresh.localY != hit.localY)
        throw std::runtime_error("GNOME native surface transform changed");
      viewportCall(GnomeViewportOperation::Check, request->deadline, activeSnapshot);
      // Qt's real GUI/default Wayland queue may take up to 200ms to finish its
      // ordered observations. A previously valid compositor hit cannot be
      // retained through that interval without asking the exact GNOME-owned
      // session again. Some native focus/occlusion/layout changes lack a
      // synchronous Invalidated signal on the worker's private D-Bus context.
      // This check uses the frozen routed point and request identity again,
      // never the current cursor; it remains evidence only, NOT input drain.
      const auto afterQt = nativeViewport(request->deadline, &native, request->id, routed);
      if (afterQt.localX != hit.localX || afterQt.localY != hit.localY ||
          afterQt.x != hit.x || afterQt.y != hit.y)
        throw std::runtime_error("GNOME native hit changed after Qt queue observation");
      drainViewport();
      checkpoint(request->deadline);
      if (nativeGeneration != nativeInvalidations || invalidatedThrough >= activeSnapshot)
        throw std::runtime_error("GNOME viewport invalidated during observation");
      auto snapshot = std::make_shared<ViewportSnapshot>();
      snapshot->request = request->id;
      snapshot->providerOwner = owner;
      snapshot->session = session;
      snapshot->generation = generation;
      snapshot->viewer = viewer;
      snapshot->window = native.window;
      snapshot->surface = native.surface;
      snapshot->nativeRevision = native.revision;
      snapshot->desktopX = hit.x;
      snapshot->desktopY = hit.y;
      snapshot->routedPoint = request->routedPoint;
      snapshot->surfaceX = hit.localX;
      snapshot->surfaceY = hit.localY;
      snapshot->sourceX = mapped.x;
      snapshot->sourceY = mapped.y;
      snapshot->paint = painted.state;
      ViewportObservation observation;
      observation.m_snapshot = std::move(snapshot);
      observation.m_revoked = std::make_shared<std::atomic_bool>(false);
      observation.m_deadline = request->deadline;
      result.observation = std::move(observation);
    } catch (const ChannelError &error) {
      channelFailed = true;
      result.error = error.what();
    } catch (const std::exception &error) {
      result.error = std::string(error.what()).substr(0, 128);
    }
    wireRetired = wireIssued; // Late replies can only be discarded, never reused.
    {
      const std::lock_guard lock(mutex);
      if (result.observation && (stopping.load() || !alive.load() || Clock::now() >= request->deadline)) {
        result.observation.reset();
        result.error = "GNOME viewport ended before publication";
      }
      lastViewportRequest = request->id;
      viewportRevoked = result.observation ? result.observation->m_revoked : nullptr;
      viewportUpdate = std::move(result);
      viewportBusy = false;
    }
    notify(true);
    if (channelFailed)
      fail("GNOME viewport channel failed");
  }
  void cleanup() noexcept
  {
    if (viewportDescriptor.value >= 0)
      ::close(std::exchange(viewportDescriptor.value, -1));
    if (bus) {
      if (ownerSignal)
        g_dbus_connection_signal_unsubscribe(bus, ownerSignal);
      if (closeSignal)
        g_dbus_connection_signal_unsubscribe(bus, closeSignal);
      if (invalidationSignal)
        g_dbus_connection_signal_unsubscribe(bus, invalidationSignal);
      if (sessionCreated && !g_dbus_connection_is_closed(bus)) {
        // Cleanup is bounded independently of the canceled request lifetime.
        auto *reply = g_dbus_connection_call_sync(
            bus, owner.c_str(), object, interface, "CloseSession", g_variant_new("(su)", session.c_str(), generation),
            G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr
        );
        if (reply)
          g_variant_unref(reply);
      }
      g_dbus_connection_close_sync(bus, nullptr, nullptr);
      g_object_unref(bus);
      bus = nullptr;
    }
    if (loop) {
      g_main_loop_unref(loop);
      loop = nullptr;
    }
    if (context) {
      g_main_context_pop_thread_default(context);
      g_main_context_unref(context);
      context = nullptr;
    }
  }
  void run() noexcept
  {
    try {
      context = g_main_context_new();
      g_main_context_push_thread_default(context);
      loop = g_main_loop_new(context, false);
      initialize();
      auto *source = g_timeout_source_new(10);
      g_source_set_callback(
          source,
          [](gpointer data) -> gboolean {
            auto &self = *static_cast<Impl *>(data);
            if (self.stopping.load() || g_dbus_connection_is_closed(self.bus)) {
              self.fail("GNOME provider connection ended");
              return G_SOURCE_REMOVE;
            }
            try {
              self.observePendingViewport();
            } catch (const std::exception &error) {
              self.fail(error.what());
              return G_SOURCE_REMOVE;
            } catch (...) {
              self.fail("GNOME viewport worker failed");
              return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          },
          this, nullptr
      );
      g_source_attach(source, context);
      if (!terminal && !stopping.load())
        g_main_loop_run(loop);
      g_source_destroy(source);
      g_source_unref(source);
    } catch (const std::exception &error) {
      try {
        fail(error.what());
      } catch (...) {
        alive.store(false);
      }
    } catch (...) {
      try {
        fail("GNOME provider setup failed");
      } catch (...) {
        alive.store(false);
      }
    }
    alive.store(false, std::memory_order_release);
    cleanup();
  }
};

GnomeProviderClient::GnomeProviderClient(GnomeProviderConfig configuration, std::function<void()> changed)
    : m_impl(std::make_unique<Impl>(std::move(configuration), std::move(changed)))
{
}
GnomeProviderClient::~GnomeProviderClient() = default;
GnomeProviderClient::Update GnomeProviderClient::takeUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  auto result = m_impl->update;
  result.descriptor = std::exchange(m_impl->update.descriptor, -1);
  result.ready = result.ready && m_impl->alive.load(std::memory_order_acquire);
  m_impl->notified = false;
  return result;
}
std::uint64_t GnomeProviderClient::requestViewportObservation()
{
  return m_impl->scheduleViewport(std::nullopt);
}
std::uint64_t GnomeProviderClient::requestRoutedPointObservation(RoutedPoint point)
{
  // Invalid/absent source identity or unbounded native coordinates never
  // enter the worker queue or replace a valid retained observation.
  const auto exactStageCoordinate = [](double value) {
    constexpr double maximumCoordinate = 10000000.0;
    return std::isfinite(value) && std::abs(value) <= maximumCoordinate &&
           static_cast<double>(static_cast<float>(value)) == value;
  };
  if (!point.captureEpoch || !point.sequence || !exactStageCoordinate(point.desktopX) ||
      !exactStageCoordinate(point.desktopY))
    return 0;
  return m_impl->scheduleViewport(point);
}
std::optional<GnomeProviderClient::ViewportUpdate> GnomeProviderClient::takeViewportUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  return std::exchange(m_impl->viewportUpdate, std::nullopt);
}
bool GnomeProviderClient::ViewportObservation::live() const noexcept
{
  return m_snapshot && m_revoked && !m_revoked->load(std::memory_order_acquire) && Clock::now() < m_deadline;
}
const GnomeProviderClient::ViewportSnapshot &GnomeProviderClient::ViewportObservation::snapshot() const noexcept
{
  static const ViewportSnapshot empty;
  return m_snapshot ? *m_snapshot : empty;
}
bool GnomeProviderClient::live() const noexcept
{
  return m_impl->alive.load(std::memory_order_acquire);
}
void GnomeProviderClient::stop() noexcept
{
  m_impl->stop();
}

} // namespace deskflow::windowshare
