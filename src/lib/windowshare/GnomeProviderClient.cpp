/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderClient.h"

#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <mutex>
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
  std::atomic<bool> stopping = false, alive = false;
  std::mutex mutex;
  Update update;
  bool notified = false;
  GCancellable *cancel = g_cancellable_new();
  std::thread worker;
  // Worker-owned native objects below; never dereferenced by the consumer.
  GDBusConnection *bus = nullptr;
  GMainContext *context = nullptr;
  GMainLoop *loop = nullptr;
  std::string owner, session, viewer;
  guint generation = 0, ownerSignal = 0, closeSignal = 0;
  bool sessionCreated = false, terminal = false;

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
  void notify()
  {
    bool send = false;
    {
      const std::lock_guard lock(mutex);
      send = !std::exchange(notified, true);
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
    }
    g_cancellable_cancel(cancel);
  }
  void fail(std::string_view error) noexcept
  {
    terminal = true;
    alive.store(false, std::memory_order_release);
    try {
      const std::lock_guard lock(mutex);
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
        !g_variant_lookup(values, "backend", "&s", &backend) || std::strcmp(backend, "MetaBackendNative"))
      throw std::runtime_error("Unsupported GNOME provider capabilities or Shell version");
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
    struct DescriptorGuard
    {
      int value;
      ~DescriptorGuard()
      {
        if (value >= 0)
          ::close(value);
      }
    } guard{descriptor};
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
    Variant launched(call(
        "LaunchViewer",
        g_variant_new(
            "(susas)", session.c_str(), generation, configuration.viewerExecutable().toUtf8().constData(), &arguments
        ),
        G_VARIANT_TYPE("(s)")
    ));
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
  void cleanup() noexcept
  {
    if (bus) {
      if (ownerSignal)
        g_dbus_connection_signal_unsubscribe(bus, ownerSignal);
      if (closeSignal)
        g_dbus_connection_signal_unsubscribe(bus, closeSignal);
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
      auto *source = g_timeout_source_new(100);
      g_source_set_callback(
          source,
          [](gpointer data) -> gboolean {
            auto &self = *static_cast<Impl *>(data);
            if (self.stopping.load() || g_dbus_connection_is_closed(self.bus)) {
              self.fail("GNOME provider connection ended");
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
bool GnomeProviderClient::live() const noexcept
{
  return m_impl->alive.load(std::memory_order_acquire);
}
void GnomeProviderClient::stop() noexcept
{
  m_impl->stop();
}

} // namespace deskflow::windowshare
