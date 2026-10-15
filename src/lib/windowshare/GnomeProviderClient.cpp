/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderClient.h"
#include "GnomeWindowDragProtocol.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <limits>
#include <map>
#include <mutex>
#include <poll.h>
#include <set>
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
constexpr auto orderingLimit = "mutter-eis-sync-build-unverified";
bool validOrderingContract(std::string_view value)
{
  return value.empty() ||
         (value.size() == 71 && value.starts_with("sha256:") && std::all_of(value.begin() + 7, value.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
          }));
}
std::string uniqueOrderingCapability(GVariant *values, const char *name)
{
  std::string result;
  unsigned found = 0;
  for (gsize i = 0; i < g_variant_n_children(values); ++i) {
    auto *entry = g_variant_get_child_value(values, i);
    auto *key = g_variant_get_child_value(entry, 0);
    if (!std::strcmp(g_variant_get_string(key, nullptr), name)) {
      ++found;
      auto *wrapped = g_variant_get_child_value(entry, 1);
      auto *value = g_variant_get_variant(wrapped);
      if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) && g_variant_get_size(value) <= 129)
        result = g_variant_get_string(value, nullptr);
      else
        found += 2;
      g_variant_unref(value);
      g_variant_unref(wrapped);
    }
    g_variant_unref(key);
    g_variant_unref(entry);
  }
  if (found != 1)
    throw std::runtime_error("Malformed GNOME sender-ordering capability");
  return result;
}
using Clock = std::chrono::steady_clock;

struct ChannelError : std::runtime_error
{
  using std::runtime_error::runtime_error;
};
const char *diagnosticOperation(GnomeViewportOperation operation)
{
  switch (operation) {
  case GnomeViewportOperation::Snapshot:
    return "snapshot";
  case GnomeViewportOperation::Map:
    return "map";
  case GnomeViewportOperation::Check:
    return "check";
  case GnomeViewportOperation::Invalidated:
    return "invalidated";
  case GnomeViewportOperation::InputDrain:
    return "input-drain";
  case GnomeViewportOperation::InputSeal:
    return "input-seal";
  case GnomeViewportOperation::InputMap:
    return "input-map";
  case GnomeViewportOperation::SealInvalidated:
    return "seal-invalidated";
  case GnomeViewportOperation::InputMapBatch:
    return "input-map-batch";
  case GnomeViewportOperation::PreparationInventory:
    return "preparation-inventory";
  case GnomeViewportOperation::PreparationMap:
    return "preparation-map";
  case GnomeViewportOperation::PreparationDrain:
    return "preparation-drain";
  case GnomeViewportOperation::PreparationFocus:
    return "preparation-focus";
  case GnomeViewportOperation::PreparationPromote:
    return "preparation-promote";
  case GnomeViewportOperation::PreparationCleanup:
    return "preparation-cleanup";
  }
  return "unknown";
}
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
// Strictly validate the provider-owned two-stage observation dictionary.
// GVariant's lookup returns the first matching entry, which alone cannot
// reject a duplicate key or an unrecognized substituted field. This is
// read-only evidence, but it must never admit malformed native bindings.
template <std::size_t N>
bool exactFields(GVariant *dictionary, const std::array<std::pair<const char *, const char *>, N> &schema)
{
  static_assert(N < 32);
  if (!dictionary || !g_variant_is_of_type(dictionary, G_VARIANT_TYPE("a{sv}")) ||
      g_variant_n_children(dictionary) != schema.size())
    return false;
  unsigned seen = 0;
  GVariantIter iterator;
  g_variant_iter_init(&iterator, dictionary);
  const char *key = nullptr;
  GVariant *value = nullptr;
  while (g_variant_iter_next(&iterator, "{&sv}", &key, &value)) {
    bool accepted = false;
    for (std::size_t index = 0; index < schema.size(); ++index) {
      if (std::strcmp(key, schema[index].first) != 0)
        continue;
      if (!(seen & (1u << index)) && g_variant_is_of_type(value, G_VARIANT_TYPE(schema[index].second))) {
        seen |= 1u << index;
        accepted = true;
      }
      break;
    }
    g_variant_unref(value);
    if (!accepted)
      return false;
  }
  return seen == ((1u << schema.size()) - 1);
}
bool exactStageFields(GVariant *dictionary)
{
  static constexpr std::array<std::pair<const char *, const char *>, 13> schema{
      {{"available", "b"},
       {"reason", "s"},
       {"observationOnly", "b"},
       {"deliveryReady", "b"},
       {"session", "s"},
       {"viewer", "s"},
       {"request", "s"},
       {"generation", "u"},
       {"revision", "u"},
       {"stageUpdates", "u"},
       {"sameStageView", "b"},
       {"stageX", "d"},
       {"stageY", "d"}}
  };
  return exactFields(dictionary, schema);
}
constexpr std::array<std::pair<const char *, const char *>, 13> inputDrainBindingSchema{
    {{"cutoffRequest", "s"},
     {"coreConnectionGeneration", "u"},
     {"captureEpoch", "s"},
     {"captureSequence", "s"},
     {"capturePart", "u"},
     {"viewerConnection", "s"},
     {"ownerSession", "s"},
     {"ownerEpoch", "s"},
     {"forwardingGeneration", "s"},
     {"focusRequest", "s"},
     {"focusRevision", "s"},
     {"geometry", "s"},
     {"scope", "s"}}
};
GVariant *inputDrainBinding(const InputCutoffBinding &binding)
{
  GVariantBuilder values;
  g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
  const auto number = [&](const char *key, std::uint64_t value) {
    g_variant_builder_add(&values, "{sv}", key, g_variant_new_string(std::to_string(value).c_str()));
  };
  number("cutoffRequest", binding.request);
  g_variant_builder_add(
      &values, "{sv}", "coreConnectionGeneration", g_variant_new_uint32(binding.coreConnectionGeneration)
  );
  number("captureEpoch", binding.capture.epoch);
  number("captureSequence", binding.capture.sequence);
  g_variant_builder_add(&values, "{sv}", "capturePart", g_variant_new_uint32(binding.capture.part));
  number("viewerConnection", binding.viewerConnection);
  number("ownerSession", binding.forwarding.owner.session);
  number("ownerEpoch", binding.forwarding.owner.epoch);
  number("forwardingGeneration", binding.forwarding.generation);
  number("focusRequest", binding.forwarding.focusRequest);
  number("focusRevision", binding.forwarding.focusRevision);
  number("geometry", binding.forwarding.geometry);
  g_variant_builder_add(&values, "{sv}", "scope", g_variant_new_string("all"));
  return g_variant_builder_end(&values);
}
bool matchingInputDrainBinding(GVariant *values, const InputCutoffBinding &binding)
{
  if (!exactFields(values, inputDrainBindingSchema))
    return false;
  Variant expected(g_variant_ref_sink(inputDrainBinding(binding)));
  for (const auto &[key, type] : inputDrainBindingSchema) {
    Variant wanted(g_variant_lookup_value(expected.value, key, G_VARIANT_TYPE(type)));
    Variant actual(g_variant_lookup_value(values, key, G_VARIANT_TYPE(type)));
    if (!actual.value || !g_variant_equal(actual.value, wanted.value))
      return false;
  }
  return true;
}
constexpr std::array<std::pair<const char *, const char *>, 5> inputPreparationBindingSchema{
    {{"preparationRequest", "s"},
     {"coreConnectionGeneration", "u"},
     {"captureEpoch", "s"},
     {"captureSequence", "s"},
     {"capturePart", "u"}}
};
GVariant *inputPreparationBinding(const InputPreparationBinding &binding)
{
  GVariantBuilder values;
  g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(
      &values, "{sv}", "preparationRequest", g_variant_new_string(std::to_string(binding.request).c_str())
  );
  g_variant_builder_add(
      &values, "{sv}", "coreConnectionGeneration", g_variant_new_uint32(binding.coreConnectionGeneration)
  );
  g_variant_builder_add(
      &values, "{sv}", "captureEpoch", g_variant_new_string(std::to_string(binding.capture.epoch).c_str())
  );
  g_variant_builder_add(
      &values, "{sv}", "captureSequence", g_variant_new_string(std::to_string(binding.capture.sequence).c_str())
  );
  g_variant_builder_add(&values, "{sv}", "capturePart", g_variant_new_uint32(binding.capture.part));
  return g_variant_builder_end(&values);
}
bool matchingInputPreparationBinding(GVariant *values, const InputPreparationBinding &binding)
{
  if (!exactFields(values, inputPreparationBindingSchema))
    return false;
  Variant expected(g_variant_ref_sink(inputPreparationBinding(binding)));
  for (const auto &[key, type] : inputPreparationBindingSchema) {
    Variant wanted(g_variant_lookup_value(expected.value, key, G_VARIANT_TYPE(type)));
    Variant actual(g_variant_lookup_value(values, key, G_VARIANT_TYPE(type)));
    if (!actual.value || !g_variant_equal(actual.value, wanted.value))
      return false;
  }
  return true;
}
} // namespace

struct GnomeProviderClient::Impl
{
  GnomeProviderConfig configuration;
  std::function<void()> changed;
  std::atomic<bool> stopping = false, alive = false, routedPointSupported = false, inputDrainSupported = false,
                    routedPointsSupported = false, inputPreparationSupported = false;
  std::string orderingContract;       // Captured from the exact provider owner once.
  bool stageUpdatesSupported = false; // Worker-owned; read-only Clutter observation only.
  std::mutex mutex;
  Update update;
  std::optional<ViewportUpdate> viewportUpdate;
  std::optional<InputDrainUpdate> inputDrainUpdate;
  std::optional<InputDrainUpdate> inputMapUpdate;
  std::optional<InputMapBatchUpdate> inputMapBatchUpdate;
  std::optional<InputPreparationUpdate> inputPreparationUpdate;
  std::optional<InputPreparationCleanupUpdate> inputPreparationCleanupUpdate;
  struct NativeViewport
  {
    std::string window, surface;
    std::uint32_t revision = 0;
    double x = 0, y = 0, localX = 0, localY = 0;
  };
  struct Preparation
  {
    enum class Action
    {
      None,
      Start,
      Promote,
      Cleanup
    };
    std::uint64_t id = 0, promotionId = 0;
    InputPreparationRequest request;
    std::shared_ptr<std::atomic<InputPreparationState>> state =
        std::make_shared<std::atomic<InputPreparationState>>(InputPreparationState::Preparing);
    std::shared_ptr<std::atomic_bool> operationRevoked = std::make_shared<std::atomic_bool>(false);
    Action action = Action::Start; // Protected by mutex.
    Clock::time_point cleanupDeadline{};
    std::optional<InputDrainRequest> promotion;
    // Everything below belongs to the private worker. Cancel never reads it.
    bool gateAttempted = false, noGate = false, nativeSelected = false, nativeSelectionAttempted = false;
    GnomePreparationInventory inventory;
    GnomePreparationTarget target;
    NativeViewport native;
    std::optional<PreparedInputFocus> focus;
  };
  std::shared_ptr<Preparation> preparation;
  std::shared_ptr<Preparation> activePreparationCleanup; // Worker only; deadline is mutex-protected.
  std::optional<InputDrainSeal> prefixSeal;
  InputCutoffBinding lastSealMap;
  struct Request
  {
    std::uint64_t id;
    Clock::time_point deadline;
    std::optional<RoutedPoint> routedPoint;
    std::optional<InputCutoffBinding> cutoff;
    std::shared_ptr<std::atomic_bool> revoked;
    bool retainPrefix = false;
    std::optional<InputDrainSeal> seal;
    std::vector<InputDrainRequest> maps{};
  };
  struct MapAdmission
  {
    enum class Phase
    {
      Waiting,
      Running,
      Complete
    };
    Request request;
    Phase phase = Phase::Waiting;
    std::optional<InputDrainUpdate> update;
    std::optional<InputMapBatchUpdate> batchUpdate;
    std::string terminalError;
  };
  // Publication alone is lock-free. After CAS transfers ownership, every
  // dereference and deletion is protected by mutex, including cancellation
  // and unread completion. A running canceled slot remains occupied until
  // the worker settles that exact request.
  static_assert(std::atomic<MapAdmission *>::is_always_lock_free);
  std::atomic<MapAdmission *> mapAdmission = nullptr;
  std::optional<Request> viewportRequest;
  std::shared_ptr<std::atomic_bool> viewportRevoked, latestDrainRevoked;
  std::uint64_t latestDrainRequest = 0;
  std::shared_ptr<std::atomic_bool> activeRequestRevoked; // Worker only.
  std::shared_ptr<std::atomic_bool> activeSealRevoked;    // Worker only.
  std::optional<InputCutoffBinding> activeSealCreation;   // Worker only.
  bool processingViewport = false; // Worker-context reentrancy guard, including nested GLib iteration.
  bool notified = false, viewportNotified = false, viewportBusy = false;
  std::atomic<std::uint64_t> nextViewportRequest = 0;
  std::uint64_t lastViewportRequest = 0;
  std::uint64_t lastInputCutoffRequest = 0;
  bool lastWasInputDrain = false;
  bool lastWasInputMap = false;
  bool lastWasInputMapBatch = false;
  GCancellable *cancel = g_cancellable_new();
  std::thread worker;
  // Worker-owned native objects below; never dereferenced by the consumer.
  GDBusConnection *bus = nullptr;
  GMainContext *context = nullptr;
  GMainContext *requestContext = nullptr; // Retained until worker join; admission only wakes this context.
  GMainLoop *loop = nullptr;
  std::string owner, session, viewer;
  guint generation = 0, ownerSignal = 0, closeSignal = 0, invalidationSignal = 0;
  bool sessionCreated = false, terminal = false;
  DescriptorGuard viewportDescriptor;
  GnomeViewportDecoder viewportDecoder;
  std::uint64_t wireIssued = 0, wireRetired = 0, activeSnapshot = 0, invalidatedThrough = 0;
  GnomeViewportOperation wireOperation = GnomeViewportOperation::Snapshot; // Worker-only diagnostics.
  std::uint64_t nativeInvalidations = 0;
  std::string lastNativeInvalidation;
  std::string windowAssociation;
  bool windowDragSupported = false;
  guint windowDragSignal = 0;
  std::uint64_t lastWindowDragRequest = 0;
  struct PendingWindowDrag
  {
    GnomeWindowDragRequest binding;
    Clock::time_point deadline;
  };
  std::deque<PendingWindowDrag> windowDragRequests;
  std::deque<QByteArray> windowDragOutput;
  std::map<QString, bool> windowDragWatches;

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
    {
      // Also owns a publication that raced the terminal alive transition.
      const std::lock_guard lock(mutex);
      clearMapAdmission(true);
    }
    if (requestContext)
      g_main_context_unref(requestContext);
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
      if (prefixSeal)
        prefixSeal->m_revoked->store(true, std::memory_order_release);
      if (preparation)
        preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
      endMapAdmission("provider-stopped");
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
      if (prefixSeal)
        prefixSeal->m_revoked->store(true, std::memory_order_release);
      if (preparation)
        preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
      endMapAdmission(error);
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
      throw std::runtime_error(
          error.message("GNOME provider requires a private Unix FD-capable session bus connection")
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
              self.lastNativeInvalidation = std::string(reason).substr(0, 48);
              self.invalidatePreparation(reason);
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
        !boolean(values, "viewportChannel") || !boolean(values, "perWindowAssociation") ||
        !g_variant_lookup(values, "backend", "&s", &backend) || std::strcmp(backend, "MetaBackendNative"))
      throw std::runtime_error("Unsupported GNOME provider capabilities or Shell version");
    routedPointSupported.store(boolean(values, "routedPointObservation"), std::memory_order_release);
    routedPointsSupported.store(boolean(values, "routedPointsObservation"), std::memory_order_release);
    stageUpdatesSupported = boolean(values, "stageUpdateSequence");
    windowDragSupported = boolean(values, "windowDragPlacement") && boolean(values, "ownedWindowMoveObservation");
    if (windowDragSupported) {
      windowDragSignal = g_dbus_connection_signal_subscribe(
          bus, owner.c_str(), interface, "WindowDragChanged", object, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
          [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
             gpointer data) {
            auto &self = *static_cast<Impl *>(data);
            try {
              self.windowDragChanged(parameters);
            } catch (const std::exception &error) {
              self.fail(error.what());
            } catch (...) {
              self.fail("Invalid native window drag observation");
            }
          },
          this, nullptr
      );
    }
    if (boolean(values, "inputDrainSegment")) {
      orderingContract = uniqueOrderingCapability(values, "inputDrainOrderingContract");
      const auto limit = uniqueOrderingCapability(values, "inputDrainOrderingLimit");
      if (!validOrderingContract(orderingContract) || limit != (orderingContract.empty() ? orderingLimit : ""))
        throw std::runtime_error("Unsupported GNOME input-drain ordering contract");
      inputDrainSupported.store(true, std::memory_order_release);
    }
    inputPreparationSupported.store(
        boolean(values, "inputPreparation") && !orderingContract.empty(), std::memory_order_release
    );
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
      requestContext = g_main_context_ref(context);
      alive.store(true, std::memory_order_release);
    }
    notify();
  }
  void revokeViewport(std::string_view reason, bool revokeSeal = true, const char *source = "native")
  {
    bool changed = false;
    {
      const std::lock_guard lock(mutex);
      if (revokeSeal && prefixSeal && !prefixSeal->m_revoked->exchange(true, std::memory_order_acq_rel)) {
        changed = true;
        // A seal can revoke after its last finite point already expired. Keep
        // that distinct from point expiry in bounded native acceptance logs.
        std::fprintf(
            stderr,
            "GNOME prefix seal revoked source=%s request=%llu reason=%.*s nativeInvalidations=%llu lastNative=%s\n",
            source, static_cast<unsigned long long>(prefixSeal->binding().request),
            static_cast<int>(std::min<std::size_t>(reason.size(), 64)), reason.data(),
            static_cast<unsigned long long>(nativeInvalidations), lastNativeInvalidation.c_str()
        );
      }
      if (viewportRevoked && !viewportRevoked->exchange(true, std::memory_order_acq_rel)) {
        // Finite point expiry/replacement never turns historical closed-gate
        // completion into a failure. Existing point copies still revoke now.
        const bool pointOnly = !revokeSeal && prefixSeal && prefixSeal->live();
        if (!pointOnly) {
          if (lastWasInputMapBatch)
            publishMap(InputMapBatchUpdate{lastViewportRequest, std::string(reason.substr(0, 128)), {}});
          else if (lastWasInputMap)
            publishMap(InputDrainUpdate{lastViewportRequest, std::string(reason.substr(0, 128)), std::nullopt});
          else if (lastWasInputDrain)
            inputDrainUpdate = InputDrainUpdate{lastViewportRequest, std::string(reason.substr(0, 128)), std::nullopt};
          else
            viewportUpdate = ViewportUpdate{lastViewportRequest, std::string(reason.substr(0, 128)), std::nullopt};
        }
        changed = true;
      }
    }
    if (changed)
      notify(true);
  }
  void invalidatePreparation(std::string_view reason)
  {
    bool notifyChange = false;
    {
      const std::lock_guard lock(mutex);
      if (!preparation)
        return;
      const auto state = preparation->state->load(std::memory_order_acquire);
      if (state == InputPreparationState::Prepared || state == InputPreparationState::Promoting ||
          state == InputPreparationState::Sealed) {
        preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
        preparation->operationRevoked->store(true, std::memory_order_release);
        if (state == InputPreparationState::Prepared) {
          inputPreparationUpdate = InputPreparationUpdate{
              preparation->id, std::string(reason.substr(0, 128)), {}, InputPreparationOutcome::Failed
          };
          notifyChange = true;
        }
      }
    }
    if (notifyChange)
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
    if (activeRequestRevoked && activeRequestRevoked->load(std::memory_order_acquire))
      throw std::runtime_error("input-drain-canceled");
    if (activeSealRevoked && activeSealRevoked->load(std::memory_order_acquire))
      throw std::runtime_error("input-prefix-seal-revoked");
    if (activePreparationCleanup) {
      const std::lock_guard lock(mutex);
      if (Clock::now() >= activePreparationCleanup->cleanupDeadline)
        throw std::runtime_error("preparation-cleanup-timeout");
    }
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
  void queueWindowDrag(const QJsonObject &object)
  {
    const auto request = parseGnomeWindowDragRequest(object);
    if (request.request <= lastWindowDragRequest || windowDragRequests.size() >= 16)
      throw ChannelError("Replayed or excessive native window drag request");
    lastWindowDragRequest = request.request;
    windowDragRequests.push_back({request, Clock::now() + std::chrono::milliseconds(200)});
  }
  void queueWindowDragOutput(const QJsonObject &object)
  {
    if (windowDragOutput.size() >= 16)
      throw ChannelError("Native window drag output queue exceeded its bound");
    windowDragOutput.push_back(gnomeViewportFrame(object));
  }
  void windowDragChanged(GVariant *parameters)
  {
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(susa{sv})")))
      throw ChannelError("Malformed native window drag signal");
    const char *id = nullptr, *association = nullptr;
    guint nativeGeneration = 0;
    GVariant *values = nullptr;
    g_variant_get(parameters, "(&su&s@a{sv})", &id, &nativeGeneration, &association, &values);
    Variant dictionary(values);
    if (session != id || generation != nativeGeneration || terminal)
      return;
    static constexpr std::array<std::pair<const char *, const char *>, 11> schema{
        {{"viewer", "s"},
         {"phase", "s"},
         {"grab", "t"},
         {"sequence", "t"},
         {"x", "i"},
         {"y", "i"},
         {"frameX", "i"},
         {"frameY", "i"},
         {"width", "i"},
         {"height", "i"},
         {"buttonReleased", "b"}}
    };
    if (!exactFields(values, schema))
      throw ChannelError("Malformed native window drag fields");
    const char *nativeViewer = nullptr, *phase = nullptr;
    g_variant_lookup(values, "viewer", "&s", &nativeViewer);
    if (viewer != nativeViewer)
      return;
    const auto watching = windowDragWatches.find(QString::fromUtf8(association));
    if (watching == windowDragWatches.end() || !watching->second)
      return;
    GnomeWindowDragEvent event;
    event.association = QString::fromUtf8(association);
    guint64 grab = 0, sequence = 0;
    gint x = 0, y = 0, width = 0, height = 0;
    g_variant_lookup(values, "grab", "t", &grab);
    g_variant_lookup(values, "sequence", "t", &sequence);
    g_variant_lookup(values, "phase", "&s", &phase);
    g_variant_lookup(values, "x", "i", &event.x);
    g_variant_lookup(values, "y", "i", &event.y);
    g_variant_lookup(values, "frameX", "i", &x);
    g_variant_lookup(values, "frameY", "i", &y);
    g_variant_lookup(values, "width", "i", &width);
    g_variant_lookup(values, "height", "i", &height);
    event.grab = grab;
    event.sequence = sequence;
    event.frame = QRect(x, y, width, height);
    event.buttonReleased = boolean(values, "buttonReleased");
    if (!std::strcmp(phase, "begin"))
      event.phase = GnomeWindowDragEvent::Phase::Begin;
    else if (!std::strcmp(phase, "motion"))
      event.phase = GnomeWindowDragEvent::Phase::Motion;
    else if (!std::strcmp(phase, "end"))
      event.phase = GnomeWindowDragEvent::Phase::End;
    else if (std::strcmp(phase, "cancel"))
      throw ChannelError("Unknown native window drag phase");
    queueWindowDragOutput(gnomeWindowDragJson(event));
  }
  void processWindowDrags()
  {
    // This worker owns both directions of the inherited stream. Run only
    // between viewport operations, so native placement cannot split a prefix
    // seal/drain or interleave bytes into an in-flight viewport frame.
    if (!windowDragRequests.empty()) {
      const auto pending = windowDragRequests.front();
      windowDragRequests.pop_front();
      const auto &request = pending.binding;
      GnomeWindowDragReply result{request, false, {}, 0};
      try {
        bool inputBusy = false;
        {
          const std::lock_guard lock(mutex);
          inputBusy = viewportBusy || prefixSeal.has_value() ||
                      (preparation && preparation->state->load() != InputPreparationState::Released &&
                       preparation->state->load() != InputPreparationState::Failed);
        }
        if (!windowDragSupported || inputBusy || Clock::now() >= pending.deadline || terminal || stopping.load())
          throw std::runtime_error("Native window drag unavailable");
        const auto association = request.association.toUtf8();
        Error error;
        const auto action = request.action;
        if (action == GnomeWindowDragAction::Watch || action == GnomeWindowDragAction::Unwatch) {
          if (action == GnomeWindowDragAction::Watch && !windowDragWatches.contains(request.association) &&
              windowDragWatches.size() >= 16)
            throw ChannelError("Too many native window drag watches");
          // Retire locally even if the compositor window was already destroyed
          // or its unwatch call fails. Such signals can no longer leave this worker.
          if (action == GnomeWindowDragAction::Unwatch)
            windowDragWatches.erase(request.association);
          Variant reply(g_dbus_connection_call_sync(
              bus, owner.c_str(), object, interface, "WatchWindowDrag",
              g_variant_new(
                  "(sussb)", session.c_str(), generation, viewer.c_str(), association.constData(),
                  action == GnomeWindowDragAction::Watch
              ),
              G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 100, cancel, &error.value
          ));
          result.ok = reply.value && Clock::now() < pending.deadline;
          if (result.ok && action == GnomeWindowDragAction::Watch)
            windowDragWatches[request.association] = true;
        } else {
          const auto name = gnomeWindowDragActionName(action).toUtf8();
          Variant reply(g_dbus_connection_call_sync(
              bus, owner.c_str(), object, interface, "WindowDragPreview",
              g_variant_new(
                  "(sussttsii)", session.c_str(), generation, viewer.c_str(), association.constData(),
                  guint64(request.drag), guint64(request.sequence), name.constData(), request.x, request.y
              ),
              G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 100, cancel, &error.value
          ));
          if (!reply.value || Clock::now() >= pending.deadline)
            throw std::runtime_error("Native window placement timed out or failed");
          Variant values(g_variant_get_child_value(reply.value, 0));
          static constexpr std::array<std::pair<const char *, const char *>, 12> schema{
              {{"available", "b"},
               {"session", "s"},
               {"generation", "u"},
               {"viewer", "s"},
               {"association", "s"},
               {"drag", "t"},
               {"sequence", "t"},
               {"x", "i"},
               {"y", "i"},
               {"width", "i"},
               {"height", "i"},
               {"revision", "u"}}
          };
          if (!exactFields(values.value, schema))
            throw ChannelError("Malformed native window placement reply");
          const char *id = nullptr, *nativeViewer = nullptr, *nativeAssociation = nullptr;
          guint nativeGeneration = 0, revision = 0;
          guint64 drag = 0, sequence = 0;
          gint x = 0, y = 0, width = 0, height = 0;
          g_variant_lookup(values.value, "session", "&s", &id);
          g_variant_lookup(values.value, "viewer", "&s", &nativeViewer);
          g_variant_lookup(values.value, "association", "&s", &nativeAssociation);
          g_variant_lookup(values.value, "generation", "u", &nativeGeneration);
          g_variant_lookup(values.value, "drag", "t", &drag);
          g_variant_lookup(values.value, "sequence", "t", &sequence);
          g_variant_lookup(values.value, "revision", "u", &revision);
          g_variant_lookup(values.value, "x", "i", &x);
          g_variant_lookup(values.value, "y", "i", &y);
          g_variant_lookup(values.value, "width", "i", &width);
          g_variant_lookup(values.value, "height", "i", &height);
          if (!boolean(values.value, "available") || session != id || generation != nativeGeneration ||
              viewer != nativeViewer || request.association != QString::fromUtf8(nativeAssociation) ||
              request.drag != drag || request.sequence != sequence ||
              (action == GnomeWindowDragAction::Place && (x != request.x || y != request.y)))
            throw ChannelError("Native window placement changed its exact binding");
          result.frame = QRect(x, y, width, height);
          result.revision = revision;
          result.ok = true;
        }
      } catch (const ChannelError &) {
        throw;
      } catch (...) {
        result.ok = false;
      }
      queueWindowDragOutput(gnomeWindowDragJson(result));
    }
    // Signal callbacks enqueue only, including during checkpoint reentrancy.
    // All writes complete one framed message before the next writer runs.
    unsigned sent = 0;
    while (!windowDragOutput.empty()) {
      if (++sent > 16)
        throw ChannelError("Native drag notifications exceeded their bound");
      const auto frame = std::move(windowDragOutput.front());
      windowDragOutput.pop_front();
      writeViewportFrame(frame, Clock::now() + std::chrono::milliseconds(100));
    }
  }
  std::optional<GnomeViewportReply> readViewportObject(const QJsonObject &object)
  {
    if (isGnomeWindowDrag(object)) {
      queueWindowDrag(object);
      return std::nullopt;
    }
    return parseGnomeViewportReply(object);
  }
  std::optional<GnomeViewportReply> readViewportReply()
  {
    try {
      if (const auto object = viewportDecoder.take())
        return readViewportObject(*object);
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
        return readViewportObject(*object);
      return std::nullopt;
    } catch (const std::exception &error) {
      throw ChannelError(error.what());
    }
  }
  bool retiredReply(const GnomeViewportReply &reply)
  {
    if (reply.operation == GnomeViewportOperation::SealInvalidated) {
      bool matching = false;
      {
        const std::lock_guard lock(mutex);
        if (reply.cutoff.request > lastInputCutoffRequest)
          throw ChannelError("GNOME invalidation names an unknown prefix seal");
        if (prefixSeal && reply.cutoff.request == prefixSeal->binding().request) {
          if (reply.cutoff != prefixSeal->binding())
            throw ChannelError("GNOME prefix seal invalidation changed its binding");
          matching = true;
        }
      }
      if (matching)
        revokeViewport(reply.reason.toStdString(), true, "qt-seal");
      else if (activeSealCreation && reply.cutoff.request == activeSealCreation->request) {
        if (reply.cutoff != *activeSealCreation)
          throw ChannelError("GNOME pending prefix invalidation changed its binding");
        if (activeRequestRevoked)
          activeRequestRevoked->store(true, std::memory_order_release);
      }
      return true;
    }
    if (reply.operation == GnomeViewportOperation::Invalidated) {
      if (!reply.snapshot || reply.snapshot > wireIssued)
        throw ChannelError("GNOME viewport invalidation names an unknown snapshot");
      invalidatedThrough = std::max(invalidatedThrough, reply.snapshot);
      if (activeSnapshot && reply.snapshot >= activeSnapshot)
        revokeViewport(reply.reason.toStdString(), false);
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
  void writeViewportFrame(const QByteArray &frame, Clock::time_point deadline)
  {
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
  }
  GnomeViewportReply viewportCall(
      GnomeViewportOperation operation, Clock::time_point deadline, std::uint64_t snapshot = 0, double x = 0,
      double y = 0, const InputCutoffBinding *cutoff = nullptr, std::uint64_t seal = 0,
      std::vector<GnomeViewportRequest::MapItem> points = {},
      const InputPreparationBinding *preparationBinding = nullptr, const GnomePreparationTarget *target = nullptr
  )
  {
    checkpoint(deadline);
    if (wireIssued == std::numeric_limits<std::uint64_t>::max())
      throw ChannelError("GNOME viewport request counter exhausted");
    const auto id = ++wireIssued;
    wireOperation = operation;
    GnomeViewportRequest request{operation, id, snapshot, x, y, {}, 0, seal};
    request.points = std::move(points);
    if (cutoff) {
      request.cutoff = *cutoff;
      request.budgetMs = static_cast<std::uint32_t>(std::min(250, remainingMs(deadline)));
    }
    if (preparationBinding) {
      request.preparation = *preparationBinding;
      request.budgetMs = static_cast<std::uint32_t>(std::min(500, remainingMs(deadline)));
      if (target)
        request.target = *target;
    }
    const auto frame = gnomeViewportFrame(gnomeViewportJson(request));
    writeViewportFrame(frame, deadline);
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
          (reply->ok && !preparationBinding && operation != GnomeViewportOperation::Snapshot &&
           reply->snapshot != snapshot) ||
          (cutoff && (reply->cutoff != *cutoff || reply->snapshot != snapshot)) || reply->seal != seal)
        throw ChannelError("GNOME viewport reply does not match its request");
      if (preparationBinding && (reply->preparation != *preparationBinding || (target && reply->target != *target)))
        throw ChannelError("GNOME preparation reply substituted its exact binding");
      wireRetired = id;
      if (!reply->ok)
        throw std::runtime_error(reply->reason.toStdString());
      if (operation == GnomeViewportOperation::InputMapBatch) {
        if (reply->points.size() != request.points.size())
          throw ChannelError("GNOME viewport batch changed its length");
        for (std::size_t i = 0; i < request.points.size(); ++i)
          if (reply->points[i].cutoff != request.points[i].cutoff)
            throw ChannelError("GNOME viewport batch changed its exact capture");
      }
      if (snapshot && invalidatedThrough >= snapshot)
        throw std::runtime_error("Qt viewport changed during observation");
      return *reply;
    }
  }
  NativeViewport nativeViewport(
      Clock::time_point deadline, const NativeViewport *expected = nullptr, std::uint64_t request = 0,
      const RoutedPoint *routedPoint = nullptr, const char *phase = "initial-hit"
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
                 : g_variant_new("(suss)", session.c_str(), generation, viewer.c_str(), windowAssociation.c_str())
    ));
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface,
        expected ? routedPoint ? "ValidateRoutedPoint" : "ValidateCurrentHit" : "GetViewerWithAssociation",
        parameters.value, G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, timeout, cancel, &error.value
    ));
    checkpoint(deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME native viewport query failed"));
    Variant values(g_variant_get_child_value(reply.value, 0));
    if (g_variant_n_children(values.value) > 24)
      throw ChannelError("GNOME native viewport reply exceeds its bound");
    std::set<std::string> nativeFields;
    for (gsize i = 0; i < g_variant_n_children(values.value); ++i) {
      Variant entry(g_variant_get_child_value(values.value, i));
      Variant key(g_variant_get_child_value(entry.value, 0));
      if (!nativeFields.insert(g_variant_get_string(key.value, nullptr)).second)
        throw ChannelError("GNOME native viewport repeated a field");
    }
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
    if (text("session") != session || text("viewer") != viewer || text("windowAssociation") != windowAssociation ||
        !g_variant_lookup(values.value, "generation", "u", &value) || value != generation ||
        !g_variant_lookup(values.value, "available", "b", &available) ||
        !g_variant_lookup(values.value, "observationOnly", "b", &observationOnly) || !observationOnly ||
        !g_variant_lookup(values.value, "deliveryReady", "b", &deliveryReady) || deliveryReady ||
        (expected && text("request") != requestText))
      throw ChannelError("GNOME native viewport reply changed its binding");
    if (!available) {
      const auto reason = text("reason");
      if (reason == "stale-revision" && expected) {
        guint returnedRevision = 0;
        g_variant_lookup(values.value, "revision", "u", &returnedRevision);
        std::fprintf(
            stderr, "GNOME native hit stale: phase=%s request=%llu expected=%u actual=%u signal=%s\n", phase,
            static_cast<unsigned long long>(request), expected->revision, returnedRevision,
            lastNativeInvalidation.c_str()
        );
      }
      throw std::runtime_error(reason);
    }
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
  std::vector<NativeViewport> nativePoints(const Request &request, const NativeViewport &expected)
  {
    GVariantBuilder points;
    g_variant_builder_init(&points, G_VARIANT_TYPE("a(ssudd)"));
    for (const auto &item : request.maps)
      g_variant_builder_add(
          &points, "(ssudd)", std::to_string(item.cutoff.request).c_str(),
          std::to_string(item.cutoff.capture.sequence).c_str(), guint(item.cutoff.capture.part), item.point.desktopX,
          item.point.desktopY
      );
    const auto requestText = std::to_string(request.id);
    const auto epoch = std::to_string(request.maps.front().cutoff.capture.epoch);
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface, "ValidateRoutedPoints",
        g_variant_new(
            "(susssa(ssudd)u)", session.c_str(), generation, viewer.c_str(), requestText.c_str(), epoch.c_str(),
            &points, expected.revision
        ),
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, remainingMs(request.deadline), cancel, &error.value
    ));
    checkpoint(request.deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME native point batch failed"));
    Variant values(g_variant_get_child_value(reply.value, 0));
    static constexpr std::array<std::pair<const char *, const char *>, 11> schema{
        {{"session", "s"},
         {"generation", "u"},
         {"viewer", "s"},
         {"revision", "u"},
         {"request", "s"},
         {"captureEpoch", "s"},
         {"window", "s"},
         {"surface", "s"},
         {"available", "b"},
         {"reason", "s"},
         {"points", "a(ssudddd)"}}
    };
    if (!exactFields(values.value, schema))
      throw ChannelError("Malformed GNOME native point batch schema");
    const auto text = [&](const char *key) -> std::string {
      const char *value = nullptr;
      if (!g_variant_lookup(values.value, key, "&s", &value) || !value || std::strlen(value) > 128)
        throw ChannelError("Malformed GNOME native point batch identity");
      return value;
    };
    guint returnedGeneration = 0, revision = 0;
    if (text("session") != session || text("viewer") != viewer || text("request") != requestText ||
        text("captureEpoch") != epoch || !g_variant_lookup(values.value, "generation", "u", &returnedGeneration) ||
        returnedGeneration != generation || !g_variant_lookup(values.value, "revision", "u", &revision))
      throw ChannelError("GNOME native point batch changed its binding");
    Variant returned(g_variant_lookup_value(values.value, "points", G_VARIANT_TYPE("a(ssudddd)")));
    if (!boolean(values.value, "available")) {
      if (g_variant_n_children(returned.value) || text("reason").empty())
        throw ChannelError("GNOME rejected point batch returned partial results");
      throw std::runtime_error(text("reason"));
    }
    if (!text("reason").empty() || revision != expected.revision || text("window") != expected.window ||
        text("surface") != expected.surface || g_variant_n_children(returned.value) != request.maps.size())
      throw ChannelError("GNOME native point batch substituted its snapshot");
    std::vector<NativeViewport> result;
    for (std::size_t i = 0; i < request.maps.size(); ++i) {
      Variant tuple(g_variant_get_child_value(returned.value, i));
      const char *cutoff = nullptr, *sequence = nullptr;
      guint part = 0;
      double x = 0, y = 0, localX = 0, localY = 0;
      g_variant_get(tuple.value, "(&s&sudddd)", &cutoff, &sequence, &part, &x, &y, &localX, &localY);
      const auto &item = request.maps[i];
      if (std::to_string(item.cutoff.request) != cutoff || std::to_string(item.cutoff.capture.sequence) != sequence ||
          part != item.cutoff.capture.part || x != item.point.desktopX || y != item.point.desktopY ||
          !std::isfinite(localX) || !std::isfinite(localY) || std::abs(localX) > 1e7 || std::abs(localY) > 1e7)
        throw ChannelError("GNOME native point batch substituted a capture or coordinate");
      result.push_back({expected.window, expected.surface, revision, x, y, localX, localY});
    }
    return result;
  }
  std::vector<InputDrainSegment> observeMapBatch(const Request &request)
  {
    const auto &seal = *request.seal;
    const auto &origin = *seal.m_origin;
    const auto painted = viewportCall(GnomeViewportOperation::Snapshot, request.deadline);
    activeSnapshot = painted.request;
    windowAssociation = painted.state.windowAssociation.toStdString();
    const auto native = nativeViewport(request.deadline);
    const auto nativeGeneration = nativeInvalidations;
    if (!seal.live() || painted.state != origin.paint || native.window != origin.window ||
        native.surface != origin.surface || native.revision != origin.nativeRevision)
      throw std::runtime_error("GNOME input batch prefix lifetime changed");
    const auto hits = nativePoints(request, native);
    std::vector<GnomeViewportRequest::MapItem> points;
    for (std::size_t i = 0; i < hits.size(); ++i)
      points.push_back({request.maps[i].cutoff, hits[i].localX, hits[i].localY});
    const auto mapped = viewportCall(
        GnomeViewportOperation::InputMapBatch, request.deadline, activeSnapshot, 0, 0, &seal.binding(), 0,
        std::move(points)
    );
    const auto fresh = nativePoints(request, native);
    drainViewport();
    checkpoint(request.deadline);
    if (!seal.live() || nativeGeneration != nativeInvalidations || invalidatedThrough >= activeSnapshot)
      throw std::runtime_error("GNOME input batch invalidated before publication");
    std::vector<InputDrainSegment> result;
    for (std::size_t i = 0; i < hits.size(); ++i) {
      const auto &hit = hits[i];
      if (fresh[i].localX != hit.localX || fresh[i].localY != hit.localY || mapped.points[i].x < 0 ||
          mapped.points[i].y < 0 || mapped.points[i].x >= painted.state.sourceWidth ||
          mapped.points[i].y >= painted.state.sourceHeight)
        throw std::runtime_error("GNOME input batch transform changed");
      auto snapshot = std::make_shared<ViewportSnapshot>();
      snapshot->request = request.id;
      snapshot->providerOwner = owner;
      snapshot->session = session;
      snapshot->generation = generation;
      snapshot->viewer = viewer;
      snapshot->window = native.window;
      snapshot->surface = native.surface;
      snapshot->nativeRevision = native.revision;
      snapshot->desktopX = hit.x;
      snapshot->desktopY = hit.y;
      snapshot->routedPoint = request.maps[i].point;
      snapshot->surfaceX = hit.localX;
      snapshot->surfaceY = hit.localY;
      snapshot->sourceX = mapped.points[i].x;
      snapshot->sourceY = mapped.points[i].y;
      snapshot->paint = painted.state;
      InputDrainSegment segment;
      segment.m_viewport.m_snapshot = std::move(snapshot);
      segment.m_viewport.m_revoked = request.revoked;
      segment.m_viewport.m_deadline = request.maps[i].deadline;
      segment.m_cutoff = request.maps[i].cutoff;
      segment.m_seal = seal;
      SenderOrderingEvidence proof;
      proof.m_viewport = segment.m_viewport;
      proof.m_contract = seal.contract();
      proof.m_sealRevoked = seal.m_revoked;
      segment.m_senderOrdering = std::move(proof);
      result.push_back(std::move(segment));
    }
    return result;
  }
  // Optional real GNOME StageView observation between native-hit and Qt
  // queue checks. Clutter's before/after-update pairs are not bound to one
  // EIS sender event, and therefore NEVER constitute old-input drain.
  void observeStageFrames(Clock::time_point deadline, const NativeViewport &native, std::uint64_t request)
  {
    if (!stageUpdatesSupported)
      return; // Older/unsupported providers retain read-only diagnostics.
    const std::string requestText = std::to_string(request);
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface, "ObserveStageFrames",
        g_variant_new(
            "(sussddu)", session.c_str(), generation, viewer.c_str(), requestText.c_str(), native.x, native.y,
            native.revision
        ),
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, remainingMs(deadline), cancel, &error.value
    ));
    checkpoint(deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME stage observation incomplete"));
    Variant values(g_variant_get_child_value(reply.value, 0));
    if (!exactStageFields(values.value))
      throw ChannelError("Malformed GNOME stage observation field schema");
    const auto text = [&](const char *key) -> std::string {
      const char *value = nullptr;
      if (!g_variant_lookup(values.value, key, "&s", &value) || !value || std::strlen(value) > 128)
        throw ChannelError("Malformed GNOME stage observation binding");
      return value;
    };
    guint returnedGeneration = 0, revision = 0, cycles = 0;
    gboolean available = false, observationOnly = false, deliveryReady = true, sameView = false;
    double stageX = 0, stageY = 0;
    if (text("session") != session || text("viewer") != viewer || text("request") != requestText ||
        !g_variant_lookup(values.value, "generation", "u", &returnedGeneration) || returnedGeneration != generation ||
        !g_variant_lookup(values.value, "revision", "u", &revision) || revision < native.revision ||
        !g_variant_lookup(values.value, "available", "b", &available) ||
        !g_variant_lookup(values.value, "observationOnly", "b", &observationOnly) || !observationOnly ||
        !g_variant_lookup(values.value, "deliveryReady", "b", &deliveryReady) || deliveryReady ||
        !g_variant_lookup(values.value, "sameStageView", "b", &sameView) ||
        !g_variant_lookup(values.value, "stageUpdates", "u", &cycles) ||
        !g_variant_lookup(values.value, "stageX", "d", &stageX) ||
        !g_variant_lookup(values.value, "stageY", "d", &stageY) || !std::isfinite(stageX) || !std::isfinite(stageY) ||
        stageX != native.x || stageY != native.y)
      throw ChannelError("GNOME stage observation substituted its exact native binding");
    const auto reason = text("reason");
    if (!available) {
      // A native change ends the provider's pending stage check with a
      // negative reply carrying its newer revision. Reject this observation,
      // but keep the authenticated channel for a fresh request. Identity and
      // point echoes above remain mandatory even for a denial.
      if (reason.empty())
        throw ChannelError("GNOME stage observation denied without a reason");
      throw std::runtime_error(reason);
    }
    if (revision != native.revision)
      throw ChannelError("GNOME stage observation substituted its exact native binding");
    if (!reason.empty() || !sameView || cycles != 2)
      throw ChannelError("GNOME stage observation lacked two same-view update pairs");
  }

  void observeInputDrain(
      Clock::time_point deadline, const NativeViewport &native, std::uint64_t request, const InputCutoffBinding &cutoff,
      const InputPreparationBinding *preparationBinding = nullptr, bool cleanup = false
  )
  {
    static constexpr std::array<std::pair<const char *, const char *>, 13> schema{
        {{"available", "b"},
         {"reason", "s"},
         {"session", "s"},
         {"generation", "u"},
         {"viewer", "s"},
         {"request", "s"},
         {"revision", "u"},
         {"stageX", "d"},
         {"stageY", "d"},
         {"binding", "a{sv}"},
         {"segment", "s"},
         {"orderingLimit", "s"},
         {"orderingContract", "s"}}
    };
    const auto requestText = std::to_string(request);
    const auto budget = static_cast<guint>(std::min(120, remainingMs(deadline)));
    Error error;
    auto *parameters =
        preparationBinding
            ? g_variant_new(
                  "(sussddu@a{sv}ub)", session.c_str(), generation, viewer.c_str(), requestText.c_str(), native.x,
                  native.y, native.revision, inputPreparationBinding(*preparationBinding), budget, cleanup
              )
            : g_variant_new(
                  "(sussddu@a{sv}u)", session.c_str(), generation, viewer.c_str(), requestText.c_str(), native.x,
                  native.y, native.revision, inputDrainBinding(cutoff), budget
              );
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface, preparationBinding ? "DrainInputPreparation" : "DrainInputPrefix",
        parameters, G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, remainingMs(deadline), cancel,
        &error.value
    ));
    checkpoint(deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME input drain incomplete"));
    Variant values(g_variant_get_child_value(reply.value, 0));
    if (!exactFields(values.value, schema))
      throw ChannelError("Malformed GNOME input-drain field schema");
    const auto text = [&](const char *key) {
      const char *value = nullptr;
      if (!g_variant_lookup(values.value, key, "&s", &value) || !value || std::strlen(value) > 128)
        throw ChannelError("Malformed GNOME input-drain text");
      return std::string(value);
    };
    guint returnedGeneration = 0, revision = 0;
    double x = 0, y = 0;
    Variant binding(g_variant_lookup_value(values.value, "binding", G_VARIANT_TYPE("a{sv}")));
    if (text("session") != session || text("viewer") != viewer || text("request") != requestText ||
        text("segment") != "clutter-queue-and-stage" ||
        text("orderingLimit") != (orderingContract.empty() ? orderingLimit : "") ||
        text("orderingContract") != orderingContract ||
        !g_variant_lookup(values.value, "generation", "u", &returnedGeneration) || returnedGeneration != generation ||
        !g_variant_lookup(values.value, "revision", "u", &revision) || revision != native.revision ||
        !g_variant_lookup(values.value, "stageX", "d", &x) || !std::isfinite(x) || x != native.x ||
        !g_variant_lookup(values.value, "stageY", "d", &y) || !std::isfinite(y) || y != native.y ||
        !(preparationBinding ? matchingInputPreparationBinding(binding.value, *preparationBinding)
                             : matchingInputDrainBinding(binding.value, cutoff)))
      throw ChannelError("GNOME input drain substituted its exact cutoff binding");
    const auto reason = text("reason");
    if (!boolean(values.value, "available"))
      throw std::runtime_error(reason.empty() ? "GNOME compositor input drain unavailable" : reason);
    if (!reason.empty())
      throw ChannelError("GNOME input drain returned contradictory completion");
  }
  GnomeViewportReply preparationCall(
      Preparation &prep, GnomeViewportOperation operation, Clock::time_point deadline, double x = 0, double y = 0,
      const InputCutoffBinding *all = nullptr
  )
  {
    return viewportCall(
        operation, deadline, 0, x, y, all, 0, {}, &prep.request.binding,
        operation == GnomeViewportOperation::PreparationInventory ? nullptr : &prep.target
    );
  }
  struct PreparationNative
  {
    NativeViewport native;
    QString association;
    std::string reason;
    bool available = false, focused = false;
  };
  PreparationNative
  nativePreparation(Preparation &prep, const char *method, Clock::time_point deadline, bool cleanup = false)
  {
    const auto &point = prep.request.point;
    const auto id = std::to_string(prep.id);
    GVariant *parameters = nullptr;
    if (!std::strcmp(method, "SelectInputPreparation")) {
      GVariantBuilder associations;
      g_variant_builder_init(&associations, G_VARIANT_TYPE("as"));
      for (const auto &target : prep.inventory.targets)
        g_variant_builder_add(&associations, "s", target.windowAssociation.toUtf8().constData());
      parameters = g_variant_new(
          "(sussddas@a{sv}u)", session.c_str(), generation, viewer.c_str(), id.c_str(), point.desktopX, point.desktopY,
          &associations, inputPreparationBinding(prep.request.binding), guint(std::min(500, remainingMs(deadline)))
      );
    } else if (!std::strcmp(method, "QueryInputPreparation")) {
      parameters = g_variant_new(
          "(sussddu@a{sv}b)", session.c_str(), generation, viewer.c_str(), id.c_str(), point.desktopX, point.desktopY,
          prep.native.revision, inputPreparationBinding(prep.request.binding), cleanup
      );
    } else {
      parameters = g_variant_new(
          "(sussddu@a{sv}u)", session.c_str(), generation, viewer.c_str(), id.c_str(), point.desktopX, point.desktopY,
          prep.native.revision, inputPreparationBinding(prep.request.binding),
          guint(std::min(500, remainingMs(deadline)))
      );
    }
    const bool finish = !std::strcmp(method, "FinishInputPreparation");
    Error error;
    Variant reply(g_dbus_connection_call_sync(
        bus, owner.c_str(), object, interface, method, parameters,
        finish ? G_VARIANT_TYPE("()") : G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
        remainingMs(deadline), cancel, &error.value
    ));
    checkpoint(deadline);
    if (!reply.value)
      throw std::runtime_error(error.message("GNOME input preparation call failed"));
    if (finish)
      return {};
    static constexpr std::array<std::pair<const char *, const char *>, 17> schema{
        {{"available", "b"},
         {"reason", "s"},
         {"session", "s"},
         {"generation", "u"},
         {"viewer", "s"},
         {"request", "s"},
         {"binding", "a{sv}"},
         {"association", "s"},
         {"window", "s"},
         {"surface", "s"},
         {"revision", "u"},
         {"stageX", "d"},
         {"stageY", "d"},
         {"localX", "d"},
         {"localY", "d"},
         {"focused", "b"},
         {"orderingContract", "s"}}
    };
    Variant values(g_variant_get_child_value(reply.value, 0));
    if (!exactFields(values.value, schema))
      throw ChannelError("Malformed GNOME preparation reply schema");
    const auto text = [&](const char *key) {
      const char *value = nullptr;
      if (!g_variant_lookup(values.value, key, "&s", &value) || !value || std::strlen(value) > 128)
        throw ChannelError("Malformed GNOME preparation text");
      return std::string(value);
    };
    const auto number = [&](const char *key) {
      double value = 0;
      if (!g_variant_lookup(values.value, key, "d", &value) || !std::isfinite(value) || std::abs(value) > 1e7)
        throw ChannelError("Malformed GNOME preparation point");
      return value;
    };
    Variant binding(g_variant_lookup_value(values.value, "binding", G_VARIANT_TYPE("a{sv}")));
    guint returnedGeneration = 0, revision = 0;
    if (text("session") != session || text("viewer") != viewer || text("request") != id ||
        text("orderingContract") != orderingContract || orderingContract.empty() ||
        !g_variant_lookup(values.value, "generation", "u", &returnedGeneration) || returnedGeneration != generation ||
        !g_variant_lookup(values.value, "revision", "u", &revision) || !revision ||
        !matchingInputPreparationBinding(binding.value, prep.request.binding) || number("stageX") != point.desktopX ||
        number("stageY") != point.desktopY)
      throw ChannelError("GNOME preparation substituted its native binding");
    PreparationNative result;
    result.available = boolean(values.value, "available");
    result.focused = boolean(values.value, "focused");
    result.reason = text("reason");
    result.association = QString::fromStdString(text("association"));
    result.native = {text("window"), text("surface"),  revision,        point.desktopX,
                     point.desktopY, number("localX"), number("localY")};
    if (result.native.window.empty() || result.native.surface.empty() || result.available != result.reason.empty() ||
        std::none_of(prep.inventory.targets.begin(), prep.inventory.targets.end(), [&](const auto &target) {
          return target.windowAssociation == result.association;
        }))
      throw ChannelError("GNOME preparation changed its owned target");
    if (prep.nativeSelected &&
        (prep.target.windowAssociation != result.association || prep.native.window != result.native.window ||
         prep.native.surface != result.native.surface))
      throw ChannelError("GNOME preparation replaced its retained native target");
    return result;
  }
  void wakeWorker()
  {
    auto *source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto &self = *static_cast<Impl *>(data);
          if (!self.stopping.load() && self.alive.load()) {
            try {
              self.observePendingViewport();
            } catch (const std::exception &error) {
              self.fail(error.what());
            }
          }
          return G_SOURCE_REMOVE;
        },
        this, nullptr
    );
    g_source_attach(source, requestContext);
    g_source_unref(source);
  }
  void adoptPreparationNative(Preparation &prep, const PreparationNative &native)
  {
    const auto found =
        std::find_if(prep.inventory.targets.begin(), prep.inventory.targets.end(), [&](const auto &target) {
          return target.windowAssociation == native.association;
        });
    if (found == prep.inventory.targets.end())
      throw ChannelError("GNOME preparation target was not in its private inventory");
    prep.target = *found;
    prep.native = native.native;
    prep.nativeSelected = true;
    windowAssociation = native.association.toStdString();
  }
  void processPreparation()
  {
    std::shared_ptr<Preparation> prep;
    Preparation::Action action = Preparation::Action::None;
    Clock::time_point cleanupDeadline;
    std::optional<InputDrainRequest> promotion;
    {
      const std::lock_guard lock(mutex);
      prep = preparation;
      if (!prep)
        return;
      action = std::exchange(prep->action, Preparation::Action::None);
      cleanupDeadline = prep->cleanupDeadline;
      promotion = prep->promotion;
    }
    if (action == Preparation::Action::None) {
      if (prep->state->load(std::memory_order_acquire) == InputPreparationState::Prepared &&
          Clock::now() >= prep->request.deadline)
        invalidatePreparation("preparation-expired");
      return;
    }
    activeRequestRevoked = action == Preparation::Action::Cleanup ? nullptr : prep->operationRevoked;
    activePreparationCleanup = action == Preparation::Action::Cleanup ? prep : nullptr;
    activeSealRevoked.reset();
    activeSealCreation.reset();
    std::string error;
    InputPreparationOutcome outcome = InputPreparationOutcome::Failed;
    bool channelFailed = false, released = false;
    std::optional<PreparedInputFocus> focused;
    try {
      if (action == Preparation::Action::Start) {
        const auto deadline = prep->request.deadline;
        prep->gateAttempted = true;
        try {
          prep->inventory = preparationCall(*prep, GnomeViewportOperation::PreparationInventory, deadline).inventory;
        } catch (const std::runtime_error &failure) {
          if (std::string_view(failure.what()) == "preparation-no-target") {
            prep->noGate = true; // Closed-schema denial proves inventory installed no gate.
            outcome = InputPreparationOutcome::NoTarget;
          }
          throw;
        }
        if (prep->inventory.peerFingerprint != configuration.peerFingerprint())
          throw ChannelError("GNOME preparation inventory changed its authenticated peer");
        prep->nativeSelectionAttempted = true;
        const auto native = nativePreparation(*prep, "SelectInputPreparation", deadline);
        adoptPreparationNative(*prep, native);
        if (!native.available) {
          if (native.reason == "preparation-no-target")
            outcome = InputPreparationOutcome::NoTarget;
          throw std::runtime_error(native.reason);
        }
        preparationCall(
            *prep, GnomeViewportOperation::PreparationMap, deadline, native.native.localX, native.native.localY
        );
        const auto before = nativePreparation(*prep, "QueryInputPreparation", deadline);
        if (!before.available || before.native.revision != native.native.revision ||
            before.native.localX != native.native.localX || before.native.localY != native.native.localY)
          throw std::runtime_error("GNOME preparation target changed before drain");
        // The caller already matched its own requested PONG and finished the
        // full libei batch. This distinct native segment has no fabricated All.
        observeInputDrain(deadline, before.native, prep->id, {}, &prep->request.binding);
        preparationCall(*prep, GnomeViewportOperation::PreparationDrain, deadline);
        const auto drained = nativePreparation(*prep, "QueryInputPreparation", deadline);
        if (!drained.available || drained.native.revision != before.native.revision)
          throw std::runtime_error("GNOME preparation changed after Qt drain");
        prep->native = drained.native;
        const auto activated = nativePreparation(*prep, "ActivateInputPreparation", deadline);
        if (!activated.available || !activated.focused)
          throw std::runtime_error("GNOME preparation activation did not focus its exact target");
        prep->native = activated.native;
        // Selection and our exact activation may invalidate earlier native
        // state. From this completed activation onward, any native change is
        // a revocation even while the public state is still Preparing.
        const auto focusedNativeGeneration = nativeInvalidations;
        const auto focus = preparationCall(*prep, GnomeViewportOperation::PreparationFocus, deadline);
        if (focus.state.connection != prep->inventory.connection ||
            focus.state.peerFingerprint != prep->inventory.peerFingerprint)
          throw ChannelError("GNOME prepared focus replaced its actual Viewer connection");
        const auto final = nativePreparation(*prep, "QueryInputPreparation", deadline);
        if (!final.available || !final.focused || final.native.revision != activated.native.revision) {
          std::fprintf(
              stderr,
              "GNOME preparation changed: request=%llu activated=%u final=%u available=%d focused=%d "
              "nativeInvalidations=%llu->%llu lastNative=%s\n",
              static_cast<unsigned long long>(prep->id), activated.native.revision, final.native.revision,
              final.available, final.focused, static_cast<unsigned long long>(focusedNativeGeneration),
              static_cast<unsigned long long>(nativeInvalidations), lastNativeInvalidation.c_str()
          );
          throw std::runtime_error("GNOME preparation focus changed before publication");
        }
        const auto mapped = preparationCall(
            *prep, GnomeViewportOperation::PreparationMap, deadline, final.native.localX, final.native.localY
        );
        const auto afterMap = nativePreparation(*prep, "QueryInputPreparation", deadline);
        if (!afterMap.available || afterMap.native.revision != final.native.revision || !afterMap.focused ||
            afterMap.native.localX != final.native.localX || afterMap.native.localY != final.native.localY ||
            mapped.x >= focus.state.sourceWidth || mapped.y >= focus.state.sourceHeight)
          throw std::runtime_error("GNOME preparation point changed after focus mapping");
        drainViewport();
        checkpoint(deadline);
        if (nativeInvalidations != focusedNativeGeneration)
          throw std::runtime_error("GNOME preparation invalidated before focus publication");
        auto snapshot = std::make_shared<ViewportSnapshot>();
        snapshot->request = prep->id;
        snapshot->providerOwner = owner;
        snapshot->session = session;
        snapshot->generation = generation;
        snapshot->viewer = viewer;
        snapshot->window = final.native.window;
        snapshot->surface = final.native.surface;
        snapshot->nativeRevision = final.native.revision;
        snapshot->desktopX = final.native.x;
        snapshot->desktopY = final.native.y;
        snapshot->surfaceX = final.native.localX;
        snapshot->surfaceY = final.native.localY;
        snapshot->sourceX = mapped.x;
        snapshot->sourceY = mapped.y;
        snapshot->routedPoint = prep->request.point;
        snapshot->paint = focus.state;
        PreparedInputFocus result;
        result.m_binding = prep->request.binding;
        result.m_snapshot = std::move(snapshot);
        result.m_state = prep->state;
        result.m_deadline = deadline;
        result.m_contract = orderingContract;
        result.m_provider = this;
        result.m_request = prep->id;
        focused = result;
        prep->focus = std::move(result);
        outcome = InputPreparationOutcome::None;
      } else if (action == Preparation::Action::Promote) {
        if (!promotion || !prep->focus || !prep->focus->live())
          throw std::runtime_error("GNOME preparation no longer permits promotion");
        const auto &request = *promotion;
        const auto native = nativePreparation(*prep, "QueryInputPreparation", request.deadline);
        const auto &expected = prep->focus->snapshot();
        if (!native.available || !native.focused || native.native.revision != expected.nativeRevision ||
            native.native.localX != expected.surfaceX || native.native.localY != expected.surfaceY)
          throw std::runtime_error("GNOME preparation target changed before promotion");
        preparationCall(*prep, GnomeViewportOperation::PreparationPromote, request.deadline, 0, 0, &request.cutoff);
        checkpoint(request.deadline);
        const std::lock_guard lock(mutex);
        if (prep->state->load(std::memory_order_acquire) != InputPreparationState::Promoting)
          throw std::runtime_error("GNOME preparation promotion canceled");
        viewportRequest = Request{
            prep->promotionId, request.deadline, request.point, request.cutoff, prep->operationRevoked, true, {}, {}
        };
        latestDrainRequest = prep->promotionId;
        latestDrainRevoked = prep->operationRevoked;
      } else {
        checkpoint(cleanupDeadline);
        if (!prep->gateAttempted || prep->noGate) {
          released = true;
        } else {
          // If cancellation raced target selection, recover only the same
          // exact native preparation binding, never a replacement lifetime.
          if (!prep->nativeSelected) {
            const auto native = nativePreparation(
                *prep, prep->nativeSelectionAttempted ? "QueryInputPreparation" : "SelectInputPreparation",
                cleanupDeadline, true
            );
            adoptPreparationNative(*prep, native);
          }
          const auto native = nativePreparation(*prep, "QueryInputPreparation", cleanupDeadline, true);
          prep->native = native.native;
          observeInputDrain(cleanupDeadline, native.native, prep->id, {}, &prep->request.binding, true);
          preparationCall(*prep, GnomeViewportOperation::PreparationCleanup, cleanupDeadline);
          nativePreparation(*prep, "FinishInputPreparation", cleanupDeadline);
          drainViewport();
          checkpoint(cleanupDeadline);
          released = true;
        }
      }
    } catch (const ChannelError &failure) {
      std::fprintf(
          stderr, "GNOME preparation channel failure request=%llu operation=%s wireRequest=%llu reason=%.160s\n",
          static_cast<unsigned long long>(prep->id), diagnosticOperation(wireOperation),
          static_cast<unsigned long long>(wireIssued), failure.what()
      );
      channelFailed = true;
      error = failure.what();
    } catch (const std::exception &failure) {
      error = std::string(failure.what()).substr(0, 128);
    }
    wireRetired = wireIssued;
    bool publish = false;
    {
      const std::lock_guard lock(mutex);
      const auto state = prep->state->load(std::memory_order_acquire);
      if (action == Preparation::Action::Cleanup) {
        prep->state->store(
            released ? InputPreparationState::Released : InputPreparationState::Failed, std::memory_order_release
        );
        inputPreparationCleanupUpdate = InputPreparationCleanupUpdate{prep->id, error, released};
        viewportBusy = false;
        publish = true;
      } else if (state != InputPreparationState::Canceling) {
        if (action == Preparation::Action::Start) {
          if (!focused || Clock::now() >= prep->request.deadline || stopping.load() || !alive.load()) {
            focused.reset();
            if (error.empty())
              error = "GNOME preparation ended before publication";
          }
          prep->state->store(
              focused ? InputPreparationState::Prepared : InputPreparationState::Failed, std::memory_order_release
          );
          inputPreparationUpdate = InputPreparationUpdate{prep->id, error, std::move(focused), outcome};
          viewportBusy = false;
          publish = true;
        } else if (!error.empty()) {
          prep->state->store(InputPreparationState::Failed, std::memory_order_release);
          inputDrainUpdate = InputDrainUpdate{prep->promotionId, error, {}};
          viewportBusy = false;
          publish = true;
        }
      }
    }
    activeRequestRevoked.reset();
    activePreparationCleanup.reset();
    if (publish)
      notify(true);
    if (channelFailed)
      fail("GNOME preparation private channel failed");
  }
  std::uint64_t reserveViewportRequest()
  {
    auto current = nextViewportRequest.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max()) {
      if (nextViewportRequest.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        return current + 1;
    }
    return 0;
  }
  // Caller holds mutex. Exchange prevents a new publisher from borrowing the
  // retired object; a publisher never dereferences a pointer after transfer.
  void clearMapAdmission(bool revoke)
  {
    std::unique_ptr<MapAdmission> pending(mapAdmission.exchange(nullptr, std::memory_order_acq_rel));
    if (pending && revoke)
      pending->request.revoked->store(true, std::memory_order_release);
  }
  bool cancelMapAdmission(std::uint64_t request, bool terminal = false)
  {
    auto *pending = mapAdmission.load(std::memory_order_acquire);
    if (!pending || (!terminal && pending->request.id != request &&
                     (!pending->request.seal || pending->request.seal->m_request != request)))
      return false;
    pending->request.revoked->store(true, std::memory_order_release);
    if (pending->request.seal)
      pending->request.seal->m_revoked->store(true, std::memory_order_release);
    if (pending->phase != MapAdmission::Phase::Running)
      clearMapAdmission(false);
    return true;
  }
  void endMapAdmission(std::string_view reason)
  {
    auto *pending = mapAdmission.load(std::memory_order_acquire);
    if (!pending)
      return;
    pending->request.revoked->store(true, std::memory_order_release);
    if (pending->request.seal)
      pending->request.seal->m_revoked->store(true, std::memory_order_release);
    pending->terminalError = std::string(reason.substr(0, 128));
    if (pending->phase == MapAdmission::Phase::Running)
      return; // Only final worker settlement can make this result consumable.
    if (pending->request.maps.empty()) {
      if (!pending->update || pending->update->error.empty())
        publishMap(InputDrainUpdate{pending->request.id, pending->terminalError, {}}, true);
    } else if (!pending->batchUpdate || pending->batchUpdate->error.empty()) {
      publishMap(InputMapBatchUpdate{pending->request.id, pending->terminalError, {}}, true);
    }
  }
  void publishMap(InputDrainUpdate result, bool settled = false)
  {
    auto *pending = mapAdmission.load(std::memory_order_acquire);
    if (pending && pending->request.id == result.request) {
      pending->update = std::move(result);
      if (settled || pending->phase != MapAdmission::Phase::Running)
        pending->phase = MapAdmission::Phase::Complete;
    } else {
      inputMapUpdate = std::move(result);
    }
  }
  void publishMap(InputMapBatchUpdate result, bool settled = false)
  {
    auto *pending = mapAdmission.load(std::memory_order_acquire);
    if (pending && pending->request.id == result.request) {
      pending->batchUpdate = std::move(result);
      if (settled || pending->phase != MapAdmission::Phase::Running)
        pending->phase = MapAdmission::Phase::Complete;
    } else {
      inputMapBatchUpdate = std::move(result);
    }
  }
  const char *viewportAdmissionRefusal(const Request &request)
  {
    if (!alive.load(std::memory_order_acquire))
      return "provider-not-live";
    if (stopping.load())
      return "provider-stopping";
    if (request.routedPoint && !routedPointSupported.load(std::memory_order_acquire))
      return "routed-point-unsupported";
    if (viewportBusy)
      return "viewport-busy";
    if (viewportUpdate)
      return "unread-viewport-update";
    if (inputDrainUpdate)
      return "unread-drain-update";
    if (inputMapUpdate)
      return "unread-map-update";
    if (inputMapBatchUpdate)
      return "unread-batch-update";
    if (request.deadline <= Clock::now())
      return "deadline-expired";
    if (!request.maps.empty() && !routedPointsSupported.load(std::memory_order_acquire))
      return "routed-batch-unsupported";
    if (request.cutoff && !inputDrainSupported.load(std::memory_order_acquire))
      return "input-drain-unsupported";
    if (request.cutoff && request.cutoff->request <= lastInputCutoffRequest)
      return "cutoff-not-increasing";
    if (preparation &&
        (!request.seal || preparation->state->load(std::memory_order_acquire) != InputPreparationState::Sealed))
      return "preparation-not-sealed";
    if (request.retainPrefix && (!request.cutoff || orderingContract.empty() || request.seal))
      return "prefix-retention-unavailable";
    if (request.seal && !request.cutoff)
      return "seal-without-cutoff";
    if (request.seal && !prefixSeal)
      return "seal-not-retained";
    if (request.seal && !request.seal->live())
      return "seal-revoked";
    if (request.seal && request.seal->m_provider != this)
      return "seal-provider-changed";
    if (request.seal && request.seal->m_revoked != prefixSeal->m_revoked)
      return "seal-replaced";
    if (request.seal &&
        !continuesInputCutoffSeal(lastSealMap, request.maps.empty() ? *request.cutoff : request.maps.front().cutoff))
      return "seal-map-not-continuing";
    return nullptr;
  }
  void admitViewport(Request request)
  {
    if (!request.seal && prefixSeal)
      prefixSeal->m_revoked->store(true, std::memory_order_release);
    if (viewportRevoked)
      viewportRevoked->store(true, std::memory_order_release);
    latestDrainRequest = request.cutoff ? request.id : 0;
    latestDrainRevoked = request.cutoff ? request.revoked : nullptr;
    if (request.cutoff)
      lastInputCutoffRequest = request.cutoff->request;
    if (request.seal)
      lastSealMap = *request.cutoff;
    viewportRequest = std::move(request);
    viewportBusy = true;
  }
  std::uint64_t scheduleMap(
      std::optional<RoutedPoint> point, InputCutoffBinding cutoff, Clock::time_point inheritedDeadline,
      const InputDrainSeal &seal, std::vector<InputDrainRequest> maps = {}
  )
  {
    const auto requestedAt = Clock::now();
    const auto deadline = std::min(inheritedDeadline, requestedAt + inputCutoffMaximumLifetime);
    if (!alive.load(std::memory_order_acquire) || stopping.load() || !inputDrainSupported.load() ||
        (point && !routedPointSupported.load()) || (!maps.empty() && !routedPointsSupported.load()) ||
        deadline <= requestedAt || !seal.live() || seal.m_provider != this)
      return 0;
    const auto id = reserveViewportRequest();
    if (!id)
      return 0;
    for (auto &item : maps)
      item.deadline = std::min(item.deadline, requestedAt + inputCutoffMaximumLifetime);
    auto pending = std::make_unique<MapAdmission>(MapAdmission{
        Request{id, deadline, point, cutoff, std::make_shared<std::atomic_bool>(false), false, seal, std::move(maps)},
        MapAdmission::Phase::Waiting,
        {},
        {},
        {}
    });
    MapAdmission *empty = nullptr;
    if (!mapAdmission.compare_exchange_strong(
            empty, pending.get(), std::memory_order_release, std::memory_order_relaxed
        ))
      return 0;        // The reserved ID is deliberately never reused.
    pending.release(); // Only mutex-protected worker/consumer paths own it now.
    wakeWorker();
    return id;
  }
  std::uint64_t scheduleViewport(
      std::optional<RoutedPoint> routedPoint, std::optional<InputCutoffBinding> cutoff = std::nullopt,
      Clock::time_point inheritedDeadline = Clock::time_point::max(), bool retainPrefix = false
  )
  {
    const auto deadline = std::min(inheritedDeadline, Clock::now() + inputCutoffMaximumLifetime);
    Request request{0, deadline, routedPoint, cutoff, {}, retainPrefix, {}, {}};
    const std::unique_lock lock(mutex, std::try_to_lock);
    const char *reason = !lock.owns_lock()                              ? "lock-contention"
                         : mapAdmission.load(std::memory_order_acquire) ? "pending-map-admission"
                                                                        : viewportAdmissionRefusal(request);
    if (!reason) {
      request.id = reserveViewportRequest();
      if (!request.id)
        reason = "request-counter-exhausted";
    }
    if (reason) {
      std::fprintf(
          stderr,
          "GNOME viewport admission refused operation=%s cutoff=%llu locked=%u previousRequest=%llu "
          "previousCutoff=%llu previousMap=%llu reason=%s\n",
          retainPrefix ? "input-seal"
          : cutoff     ? "input-drain"
                       : "observation",
          static_cast<unsigned long long>(cutoff ? cutoff->request : 0), static_cast<unsigned>(lock.owns_lock()),
          static_cast<unsigned long long>(nextViewportRequest.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(lock.owns_lock() ? lastInputCutoffRequest : 0),
          static_cast<unsigned long long>(lock.owns_lock() ? lastSealMap.request : 0), reason
      );
      return 0;
    }
    const auto id = request.id;
    request.revoked = std::make_shared<std::atomic_bool>(false);
    admitViewport(std::move(request));
    wakeWorker();
    return id;
  }
  void observePendingViewport()
  {
    if (processingViewport)
      return;
    processingViewport = true;
    struct Unwind
    {
      bool &active;
      ~Unwind()
      {
        active = false;
      }
    } unwind{processingViewport};
    drainViewport();
    processWindowDrags();
    processPreparation();
    std::optional<Request> request;
    bool admissionDenied = false;
    {
      const std::lock_guard lock(mutex);
      auto *pending = mapAdmission.load(std::memory_order_acquire);
      if (pending && pending->phase == MapAdmission::Phase::Waiting) {
        if (const auto *reason = viewportAdmissionRefusal(pending->request)) {
          // A nonzero pending ID consumed this cutoff even when it could not
          // enter native work. A new deadline cannot revive the same request.
          lastInputCutoffRequest = std::max(lastInputCutoffRequest, pending->request.cutoff->request);
          if (pending->request.maps.empty())
            publishMap(InputDrainUpdate{pending->request.id, reason, {}}, true);
          else
            publishMap(InputMapBatchUpdate{pending->request.id, reason, {}}, true);
          admissionDenied = true;
        } else {
          pending->phase = MapAdmission::Phase::Running;
          admitViewport(pending->request);
        }
      }
      request = std::exchange(viewportRequest, std::nullopt);
    }
    if (admissionDenied)
      notify(true);
    if (!request)
      return;
    activeRequestRevoked = request->revoked;
    activeSealRevoked = request->seal ? request->seal->m_revoked : nullptr;
    activeSealCreation = request->retainPrefix ? request->cutoff : std::nullopt;
    ViewportUpdate result{request->id, {}, std::nullopt};
    std::vector<InputDrainSegment> batchResult;
    bool channelFailed = false;
    try {
      if (!request->maps.empty()) {
        batchResult = observeMapBatch(*request);
      } else {
        const auto painted = viewportCall(GnomeViewportOperation::Snapshot, request->deadline);
        activeSnapshot = painted.request;
        if (painted.state.peerFingerprint != configuration.peerFingerprint())
          throw ChannelError("GNOME viewer reported a different authenticated peer");
        if (request->cutoff) {
          const auto &binding = request->cutoff->forwarding;
          if (painted.state.connection != request->cutoff->viewerConnection ||
              painted.state.session != binding.owner.session || painted.state.epoch != binding.owner.epoch ||
              painted.state.geometry != binding.geometry || painted.state.focusRequest != binding.focusRequest ||
              painted.state.focusRevision != binding.focusRevision)
            throw std::runtime_error("GNOME input-drain Viewer binding changed");
        }
        windowAssociation = painted.state.windowAssociation.toStdString();
        const auto native = nativeViewport(request->deadline);
        const auto nativeGeneration = nativeInvalidations;
        if (request->seal) {
          const auto &origin = *request->seal->m_origin;
          if (!request->seal->live() || painted.state != origin.paint || native.window != origin.window ||
              native.surface != origin.surface || native.revision != origin.nativeRevision)
            throw std::runtime_error("GNOME input prefix lifetime changed");
        }
        const auto *routed = request->routedPoint ? &*request->routedPoint : nullptr;
        const auto hit = nativeViewport(request->deadline, &native, request->id, routed);
        const auto mapped = viewportCall(
            request->seal ? GnomeViewportOperation::InputMap : GnomeViewportOperation::Map, request->deadline,
            activeSnapshot, hit.localX, hit.localY, request->seal ? &*request->cutoff : nullptr,
            request->seal ? request->seal->binding().request : 0
        );
        if (mapped.x < 0 || mapped.y < 0 || mapped.x >= painted.state.sourceWidth ||
            mapped.y >= painted.state.sourceHeight)
          throw ChannelError("GNOME viewport mapped outside its source");
        const auto fresh = nativeViewport(request->deadline, &native, request->id, routed, "mapped-hit");
        if (fresh.localX != hit.localX || fresh.localY != hit.localY)
          throw std::runtime_error("GNOME native surface transform changed");
        // The explicit cutoff path requires a compositor queue/stage segment
        // followed by the same exact All-fenced Qt operation. Generic viewport
        // observations retain their separate, nonauthorizing frame checks.
        if (request->seal) {
          // The exact All fence remains continuously closed. The old prefix was
          // already consumed; this request validates only its new stamped point.
          checkpoint(request->deadline);
        } else if (request->cutoff) {
          observeInputDrain(request->deadline, hit, request->id, *request->cutoff);
          if (nativeGeneration != nativeInvalidations || invalidatedThrough >= activeSnapshot)
            throw std::runtime_error("GNOME input drain invalidated before Qt consumption");
          viewportCall(
              request->retainPrefix ? GnomeViewportOperation::InputSeal : GnomeViewportOperation::InputDrain,
              request->deadline, activeSnapshot, 0, 0, &*request->cutoff
          );
        } else {
          observeStageFrames(request->deadline, hit, request->id);
          viewportCall(GnomeViewportOperation::Check, request->deadline, activeSnapshot);
        }
        // Qt's real GUI/default Wayland queue may take up to 200ms to finish its
        // ordered observations. A previously valid compositor hit cannot be
        // retained through that interval without asking the exact GNOME-owned
        // session again. Some native focus/occlusion/layout changes lack a
        // synchronous Invalidated signal on the worker's private D-Bus context.
        // This check uses the frozen routed point and request identity again,
        // never the current cursor; it remains evidence only, NOT input drain.
        const auto afterQt = nativeViewport(request->deadline, &native, request->id, routed, "after-qt-hit");
        if (afterQt.localX != hit.localX || afterQt.localY != hit.localY || afterQt.x != hit.x || afterQt.y != hit.y)
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
        snapshot->stageFramesObserved = !request->cutoff && stageUpdatesSupported;
        ViewportObservation observation;
        observation.m_snapshot = std::move(snapshot);
        observation.m_revoked = request->revoked;
        observation.m_deadline = request->deadline;
        result.observation = std::move(observation);
      }
    } catch (const ChannelError &error) {
      std::fprintf(
          stderr,
          "GNOME viewport channel failure request=%llu operation=%s wireRequest=%llu snapshot=%llu reason=%.160s\n",
          static_cast<unsigned long long>(request->id), diagnosticOperation(wireOperation),
          static_cast<unsigned long long>(wireIssued), static_cast<unsigned long long>(activeSnapshot), error.what()
      );
      channelFailed = true;
      result.error = error.what();
    } catch (const std::exception &error) {
      result.error = std::string(error.what()).substr(0, 128);
    }
    wireRetired = wireIssued; // Late replies can only be discarded, never reused.
    bool publish = true;
    {
      const std::lock_guard lock(mutex);
      if (!batchResult.empty() &&
          (stopping.load() || !alive.load() || Clock::now() >= request->deadline || !request->seal->live())) {
        batchResult.clear();
        result.error = "GNOME input batch ended before publication";
      }
      if (result.observation && (stopping.load() || !alive.load() || Clock::now() >= request->deadline)) {
        result.observation.reset();
        result.error = "GNOME viewport ended before publication";
      }
      if (result.observation && request->seal && !request->seal->live()) {
        result.observation.reset();
        result.error = "GNOME input prefix ended before mapping publication";
      }
      if (preparation && preparation->promotionId == request->id) {
        if (preparation->state->load(std::memory_order_acquire) != InputPreparationState::Promoting ||
            Clock::now() >= preparation->request.deadline) {
          result.observation.reset();
          result.error = "GNOME preparation promotion ended before publication";
        } else {
          preparation->state->store(
              result.observation ? InputPreparationState::Sealed : InputPreparationState::Failed,
              std::memory_order_release
          );
        }
      }
      publish = !request->revoked->load(std::memory_order_acquire);
      if (publish) {
        lastViewportRequest = request->id;
        lastWasInputDrain = request->cutoff.has_value();
        lastWasInputMap = request->seal.has_value();
        lastWasInputMapBatch = !request->maps.empty();
        viewportRevoked = !batchResult.empty() ? request->revoked
                          : result.observation ? result.observation->m_revoked
                                               : nullptr;
        if (!request->maps.empty()) {
          publishMap(InputMapBatchUpdate{request->id, std::move(result.error), std::move(batchResult)}, true);
        } else if (request->cutoff) {
          InputDrainUpdate drain{result.request, std::move(result.error), std::nullopt};
          if (result.observation) {
            InputDrainSegment segment;
            segment.m_viewport = std::move(*result.observation);
            segment.m_cutoff = *request->cutoff;
            if (request->retainPrefix) {
              InputDrainSeal seal;
              seal.m_revoked = std::make_shared<std::atomic_bool>(false);
              seal.m_origin = segment.m_viewport.m_snapshot;
              seal.m_binding = segment.m_cutoff;
              seal.m_contract = orderingContract;
              seal.m_provider = this;
              seal.m_request = request->id;
              prefixSeal = seal;
              lastSealMap = seal.binding();
              segment.m_seal = std::move(seal);
            } else if (request->seal)
              segment.m_seal = request->seal;
            if (!orderingContract.empty()) {
              SenderOrderingEvidence proof;
              proof.m_viewport = segment.m_viewport;
              proof.m_contract = orderingContract;
              proof.m_sealRevoked = segment.m_seal ? segment.m_seal->m_revoked : nullptr;
              segment.m_senderOrdering = std::move(proof);
            }
            drain.segment = std::move(segment);
          }
          if (request->seal)
            publishMap(std::move(drain), true);
          else
            inputDrainUpdate = std::move(drain);
        } else
          viewportUpdate = std::move(result);
      } else if (
          const auto *pending = mapAdmission.load(std::memory_order_acquire);
          pending && pending->request.id == request->id
      ) {
        if (pending->terminalError.empty()) {
          clearMapAdmission(false);
        } else {
          if (request->maps.empty())
            publishMap(InputDrainUpdate{request->id, pending->terminalError, {}}, true);
          else
            publishMap(InputMapBatchUpdate{request->id, pending->terminalError, {}}, true);
          publish = true;
        }
      }
      viewportBusy = false;
    }
    activeRequestRevoked.reset();
    activeSealRevoked.reset();
    activeSealCreation.reset();
    if (publish)
      notify(true); // Exact cancellation suppresses late completion/error updates.
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
      if (windowDragSignal)
        g_dbus_connection_signal_unsubscribe(bus, windowDragSignal);
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
std::mutex &GnomeProviderClient::stateMutexForTesting()
{
  return m_impl->mutex;
}
std::atomic<std::uint64_t> &GnomeProviderClient::requestCounterForTesting()
{
  return m_impl->nextViewportRequest;
}
bool GnomeProviderClient::pendingMapForTesting(bool completed) const
{
  if (!completed)
    return m_impl->mapAdmission.load(std::memory_order_acquire) != nullptr;
  const std::lock_guard lock(m_impl->mutex);
  const auto *pending = m_impl->mapAdmission.load(std::memory_order_acquire);
  return pending && pending->phase == Impl::MapAdmission::Phase::Complete;
}
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
std::uint64_t GnomeProviderClient::requestInputDrain(InputDrainRequest request)
{
  const auto exactCoordinate = [](double value) {
    return std::isfinite(value) && std::abs(value) <= 10000000.0 &&
           static_cast<double>(static_cast<float>(value)) == value;
  };
  if (!validInputCutoffBinding(request.cutoff) || request.point.captureEpoch != request.cutoff.capture.epoch ||
      request.point.sequence != request.cutoff.capture.sequence || !exactCoordinate(request.point.desktopX) ||
      !exactCoordinate(request.point.desktopY) || request.deadline <= Clock::now())
    return 0;
  return m_impl->scheduleViewport(request.point, request.cutoff, request.deadline, request.retainPrefix);
}
std::uint64_t GnomeProviderClient::requestInputPreparation(InputPreparationRequest request)
{
  const auto now = Clock::now();
  const auto exact = [](double value) {
    return std::isfinite(value) && std::abs(value) <= 10000000.0 && double(float(value)) == value;
  };
  if (!validInputPreparationBinding(request.binding) || request.point.captureEpoch != request.binding.capture.epoch ||
      request.point.sequence != request.binding.capture.sequence || !exact(request.point.desktopX) ||
      !exact(request.point.desktopY) || request.deadline <= now)
    return 0;
  const std::lock_guard lock(m_impl->mutex);
  if (!m_impl->alive.load() || m_impl->stopping.load() || !m_impl->inputPreparationSupported.load() ||
      m_impl->preparation || m_impl->mapAdmission.load(std::memory_order_acquire) || m_impl->viewportBusy ||
      m_impl->viewportUpdate || m_impl->inputDrainUpdate || m_impl->inputMapUpdate || m_impl->inputMapBatchUpdate ||
      m_impl->inputPreparationCleanupUpdate || (m_impl->prefixSeal && m_impl->prefixSeal->live()) ||
      request.binding.request <= m_impl->lastInputCutoffRequest)
    return 0;
  const auto id = m_impl->reserveViewportRequest();
  if (!id)
    return 0;
  request.deadline = std::min(request.deadline, now + inputPreparationMaximumLifetime);
  auto prep = std::make_shared<Impl::Preparation>();
  prep->id = id;
  prep->request = request;
  m_impl->lastInputCutoffRequest = request.binding.request;
  if (m_impl->viewportRevoked)
    m_impl->viewportRevoked->store(true, std::memory_order_release);
  m_impl->preparation = prep;
  m_impl->viewportBusy = true;
  m_impl->wakeWorker();
  return prep->id;
}
std::optional<GnomeProviderClient::InputPreparationUpdate> GnomeProviderClient::takeInputPreparationUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  return std::exchange(m_impl->inputPreparationUpdate, {});
}
std::uint64_t
GnomeProviderClient::promoteInputPreparation(const PreparedInputFocus &prepared, InputDrainRequest request)
{
  const auto now = Clock::now();
  if (!prepared.live() || !request.retainPrefix || !validInputCutoffBinding(request.cutoff) ||
      request.deadline <= now || request.point != prepared.snapshot().routedPoint ||
      request.cutoff.request <= prepared.binding().request || request.cutoff.capture != prepared.binding().capture ||
      request.cutoff.coreConnectionGeneration != prepared.binding().coreConnectionGeneration)
    return 0;
  const auto &paint = prepared.snapshot().paint;
  const auto &all = request.cutoff.forwarding;
  if (request.cutoff.viewerConnection != paint.connection || all.owner != Owner{paint.session, paint.epoch} ||
      all.geometry != paint.geometry || all.focusRequest != paint.focusRequest ||
      all.focusRevision != paint.focusRevision)
    return 0;
  const std::lock_guard lock(m_impl->mutex);
  const auto prep = m_impl->preparation;
  if (!prep || prepared.m_provider != m_impl.get() || prepared.m_request != prep->id ||
      prepared.m_state != prep->state ||
      prep->state->load(std::memory_order_acquire) != InputPreparationState::Prepared || m_impl->viewportBusy ||
      m_impl->mapAdmission.load(std::memory_order_acquire) || m_impl->inputPreparationUpdate ||
      m_impl->inputDrainUpdate || request.cutoff.request <= m_impl->lastInputCutoffRequest)
    return 0;
  const auto id = m_impl->reserveViewportRequest();
  if (!id)
    return 0;
  request.deadline = std::min({request.deadline, prep->request.deadline, now + inputCutoffMaximumLifetime});
  prep->promotion = request;
  prep->promotionId = id;
  prep->state->store(InputPreparationState::Promoting, std::memory_order_release);
  prep->action = Impl::Preparation::Action::Promote;
  m_impl->lastInputCutoffRequest = request.cutoff.request;
  m_impl->viewportBusy = true;
  m_impl->wakeWorker();
  return prep->promotionId;
}
bool GnomeProviderClient::cancelInputPreparation(std::uint64_t request, Clock::time_point cleanupDeadline)
{
  const auto now = Clock::now();
  const std::lock_guard lock(m_impl->mutex);
  const auto prep = m_impl->preparation;
  if (!request || !prep || prep->id != request || !m_impl->alive.load() || m_impl->stopping.load() ||
      cleanupDeadline <= now)
    return false;
  const auto state = prep->state->load(std::memory_order_acquire);
  if (state == InputPreparationState::Released)
    return m_impl->inputPreparationCleanupUpdate.has_value();
  if (state == InputPreparationState::Canceling) {
    prep->cleanupDeadline = std::min(prep->cleanupDeadline, cleanupDeadline);
    return true;
  }
  prep->state->store(InputPreparationState::Canceling, std::memory_order_release);
  m_impl->cancelMapAdmission(0, true);
  prep->operationRevoked->store(true, std::memory_order_release);
  prep->cleanupDeadline = std::min(cleanupDeadline, now + inputCutoffMaximumLifetime);
  prep->action = Impl::Preparation::Action::Cleanup;
  if (m_impl->prefixSeal)
    m_impl->prefixSeal->m_revoked->store(true, std::memory_order_release);
  if (m_impl->latestDrainRevoked)
    m_impl->latestDrainRevoked->store(true, std::memory_order_release);
  if (m_impl->viewportRevoked)
    m_impl->viewportRevoked->store(true, std::memory_order_release);
  m_impl->viewportRequest.reset();
  m_impl->inputPreparationUpdate.reset();
  m_impl->inputDrainUpdate.reset();
  m_impl->inputMapUpdate.reset();
  m_impl->inputMapBatchUpdate.reset();
  m_impl->viewportBusy = true;
  m_impl->wakeWorker();
  return true;
}
std::optional<GnomeProviderClient::InputPreparationCleanupUpdate>
GnomeProviderClient::takeInputPreparationCleanupUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  auto result = std::exchange(m_impl->inputPreparationCleanupUpdate, {});
  if (result && m_impl->preparation && m_impl->preparation->id == result->request && result->released) {
    m_impl->preparation.reset();
    m_impl->prefixSeal.reset();
    m_impl->latestDrainRequest = 0;
    m_impl->latestDrainRevoked.reset();
  }
  return result;
}
bool GnomeProviderClient::PreparedInputFocus::live() const noexcept
{
  if (!m_state || !m_snapshot || Clock::now() >= m_deadline)
    return false;
  const auto state = m_state->load(std::memory_order_acquire);
  return state == InputPreparationState::Prepared || state == InputPreparationState::Promoting ||
         state == InputPreparationState::Sealed;
}
const InputPreparationBinding &GnomeProviderClient::PreparedInputFocus::binding() const noexcept
{
  return m_binding;
}
const GnomeProviderClient::ViewportSnapshot &GnomeProviderClient::PreparedInputFocus::snapshot() const noexcept
{
  return *m_snapshot;
}
const std::string &GnomeProviderClient::PreparedInputFocus::contract() const noexcept
{
  return m_contract;
}
std::uint64_t GnomeProviderClient::requestInputMap(const InputDrainSeal &seal, InputDrainRequest request)
{
  const auto exactCoordinate = [](double value) {
    return std::isfinite(value) && std::abs(value) <= 10000000.0 &&
           static_cast<double>(static_cast<float>(value)) == value;
  };
  if (request.retainPrefix || !seal.live() || !continuesInputCutoffSeal(seal.binding(), request.cutoff) ||
      request.point.captureEpoch != request.cutoff.capture.epoch ||
      request.point.sequence != request.cutoff.capture.sequence || !exactCoordinate(request.point.desktopX) ||
      !exactCoordinate(request.point.desktopY) || request.deadline <= Clock::now())
    return 0;
  return m_impl->scheduleMap(request.point, request.cutoff, request.deadline, seal);
}
std::uint64_t
GnomeProviderClient::requestInputMapBatch(const InputDrainSeal &seal, std::vector<InputDrainRequest> requests)
{
  if (!seal.live() || requests.empty() || requests.size() > 16)
    return 0;
  auto previous = seal.binding();
  const auto now = Clock::now();
  auto deadline = now + inputCutoffMaximumLifetime;
  for (const auto &request : requests) {
    const auto exact = [](double value) {
      return std::isfinite(value) && std::abs(value) <= 10000000.0 &&
             static_cast<double>(static_cast<float>(value)) == value;
    };
    if (request.retainPrefix || !continuesInputCutoffSeal(previous, request.cutoff) || request.deadline <= now ||
        request.point.captureEpoch != request.cutoff.capture.epoch ||
        request.point.sequence != request.cutoff.capture.sequence || !exact(request.point.desktopX) ||
        !exact(request.point.desktopY))
      return 0;
    deadline = std::min(deadline, request.deadline);
    previous = request.cutoff;
  }
  return m_impl->scheduleMap(std::nullopt, previous, deadline, seal, std::move(requests));
}
bool GnomeProviderClient::cancelInputDrain(std::uint64_t request)
{
  {
    std::unique_lock lock(m_impl->mutex);
    if (m_impl->preparation && m_impl->preparation->promotionId == request) {
      const auto origin = m_impl->preparation->id;
      lock.unlock();
      return cancelInputPreparation(origin, Clock::now() + inputCutoffMaximumLifetime);
    }
  }
  const std::lock_guard lock(m_impl->mutex);
  const bool canceledMap = m_impl->cancelMapAdmission(request);
  const bool sealRequest = m_impl->prefixSeal && m_impl->prefixSeal->m_request == request;
  if (!request || (!sealRequest && !canceledMap && request != m_impl->latestDrainRequest) ||
      !m_impl->latestDrainRevoked)
    return canceledMap;
  if (m_impl->prefixSeal)
    m_impl->prefixSeal->m_revoked->store(true, std::memory_order_release);
  m_impl->latestDrainRevoked->store(true, std::memory_order_release);
  const auto current = m_impl->latestDrainRequest;
  m_impl->latestDrainRequest = 0;
  m_impl->latestDrainRevoked.reset();
  if (m_impl->viewportRequest && m_impl->viewportRequest->id == current) {
    m_impl->viewportRequest.reset();
    m_impl->viewportBusy = false;
  }
  if (m_impl->inputDrainUpdate && m_impl->inputDrainUpdate->request == current)
    m_impl->inputDrainUpdate.reset();
  if (m_impl->inputMapUpdate && m_impl->inputMapUpdate->request == current)
    m_impl->inputMapUpdate.reset();
  if (m_impl->inputMapBatchUpdate && m_impl->inputMapBatchUpdate->request == current)
    m_impl->inputMapBatchUpdate.reset();
  m_impl->viewportNotified = false;
  return true;
}
std::optional<GnomeProviderClient::InputDrainUpdate> GnomeProviderClient::takeInputMapUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  if (auto result = std::exchange(m_impl->inputMapUpdate, std::nullopt))
    return result;
  auto *pending = m_impl->mapAdmission.load(std::memory_order_acquire);
  if (!pending || pending->phase != Impl::MapAdmission::Phase::Complete || !pending->update)
    return {};
  auto result = std::move(pending->update);
  m_impl->clearMapAdmission(false);
  return result;
}
std::optional<GnomeProviderClient::InputMapBatchUpdate> GnomeProviderClient::takeInputMapBatchUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  if (auto result = std::exchange(m_impl->inputMapBatchUpdate, std::nullopt))
    return result;
  auto *pending = m_impl->mapAdmission.load(std::memory_order_acquire);
  if (!pending || pending->phase != Impl::MapAdmission::Phase::Complete || !pending->batchUpdate)
    return {};
  auto result = std::move(pending->batchUpdate);
  m_impl->clearMapAdmission(false);
  return result;
}
std::optional<GnomeProviderClient::InputDrainUpdate> GnomeProviderClient::takeInputDrainUpdate()
{
  const std::lock_guard lock(m_impl->mutex);
  m_impl->viewportNotified = false;
  return std::exchange(m_impl->inputDrainUpdate, std::nullopt);
}
bool GnomeProviderClient::InputDrainSegment::live() const noexcept
{
  return m_viewport.live() && (!m_seal || m_seal->live());
}
const GnomeProviderClient::ViewportSnapshot &GnomeProviderClient::InputDrainSegment::snapshot() const noexcept
{
  return m_viewport.snapshot();
}
const InputCutoffBinding &GnomeProviderClient::InputDrainSegment::binding() const noexcept
{
  return m_cutoff;
}
const char *GnomeProviderClient::InputDrainSegment::orderingLimitation() const noexcept
{
  return m_senderOrdering ? "" : orderingLimit;
}
const std::optional<GnomeProviderClient::SenderOrderingEvidence> &
GnomeProviderClient::InputDrainSegment::senderOrdering() const noexcept
{
  return m_senderOrdering;
}
const std::optional<GnomeProviderClient::InputDrainSeal> &GnomeProviderClient::InputDrainSegment::seal() const noexcept
{
  return m_seal;
}
bool GnomeProviderClient::InputDrainSeal::live() const noexcept
{
  return m_revoked && !m_revoked->load(std::memory_order_acquire) && m_origin && !m_contract.empty();
}
const InputCutoffBinding &GnomeProviderClient::InputDrainSeal::binding() const noexcept
{
  return m_binding;
}
const std::string &GnomeProviderClient::InputDrainSeal::contract() const noexcept
{
  return m_contract;
}
bool GnomeProviderClient::SenderOrderingEvidence::live() const noexcept
{
  return !m_contract.empty() && m_viewport.live() &&
         (!m_sealRevoked || !m_sealRevoked->load(std::memory_order_acquire));
}
const std::string &GnomeProviderClient::SenderOrderingEvidence::contract() const noexcept
{
  return m_contract;
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
