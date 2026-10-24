/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: MIT
 */
// Real-compositor acceptance driver, never a production input backend. The
// only EIS descriptor and every observation belong to one production client.
#include "GnomeProviderClient.h"
#include "GnomeViewportProtocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <libei.h>
#include <memory>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

using namespace deskflow::windowshare;
using Clock = std::chrono::steady_clock;
namespace {
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
void output(QJsonObject value)
{
  const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
  // A 16-point batch includes each complete native/paint/ordering receipt.
  // Keep this equal to Driver.event's bound; scalar receipts were <16KiB.
  require(bytes.size() <= 65536, "Fixture output exceeds bound");
  require(
      std::fwrite(bytes.constData(), 1, bytes.size(), stdout) == static_cast<std::size_t>(bytes.size()),
      "Cannot write fixture response"
  );
  std::fflush(stdout);
}
QJsonValue number(std::uint64_t value)
{
  return QJsonValue(static_cast<qint64>(value));
}
std::uint64_t integer(const QJsonObject &object, const char *name)
{
  const auto value = object.value(QLatin1String(name));
  const auto n = value.toDouble(-1);
  require(
      value.isDouble() && std::isfinite(n) && n >= 1 && n <= 9007199254740991.0 && std::floor(n) == n,
      "Invalid fixture request number"
  );
  return static_cast<std::uint64_t>(n);
}
std::int32_t scrollDelta(const QJsonObject &object, const char *name)
{
  const auto value = object.value(QLatin1String(name));
  const auto n = value.toDouble();
  require(
      value.isDouble() && std::isfinite(n) && std::floor(n) == n && n >= -1200 && n <= 1200,
      "Invalid bounded discrete scroll delta"
  );
  return static_cast<std::int32_t>(n);
}
QJsonObject snapshot(const GnomeProviderClient::ViewportSnapshot &value)
{
  GnomeViewportReply paint;
  paint.operation = GnomeViewportOperation::Snapshot;
  paint.request = value.request;
  paint.ok = true;
  paint.state = value.paint;
  return {
      {"request", QString::number(value.request)},
      {"providerOwner", QString::fromStdString(value.providerOwner)},
      {"session", QString::fromStdString(value.session)},
      {"generation", static_cast<qint64>(value.generation)},
      {"viewer", QString::fromStdString(value.viewer)},
      {"window", QString::fromStdString(value.window)},
      {"surface", QString::fromStdString(value.surface)},
      {"nativeRevision", static_cast<qint64>(value.nativeRevision)},
      {"desktopX", value.desktopX},
      {"desktopY", value.desktopY},
      {"surfaceX", value.surfaceX},
      {"surfaceY", value.surfaceY},
      {"sourceX", value.sourceX},
      {"sourceY", value.sourceY},
      {"paint", gnomeViewportJson(paint).value("state")}
  };
}
QJsonObject cutoff(const InputCutoffBinding &value)
{
  return {
      {"request", QString::number(value.request)},
      {"coreConnectionGeneration", static_cast<qint64>(value.coreConnectionGeneration)},
      {"captureEpoch", QString::number(value.capture.epoch)},
      {"captureSequence", QString::number(value.capture.sequence)},
      {"capturePart", value.capture.part},
      {"viewerConnection", QString::number(value.viewerConnection)},
      {"session", QString::number(value.forwarding.owner.session)},
      {"epoch", QString::number(value.forwarding.owner.epoch)},
      {"generation", QString::number(value.forwarding.generation)},
      {"focusRequest", QString::number(value.forwarding.focusRequest)},
      {"focusRevision", QString::number(value.forwarding.focusRevision)},
      {"geometry", QString::number(value.forwarding.geometry)},
      {"scope", "all"}
  };
}
QJsonObject segmentReceipt(const GnomeProviderClient::InputDrainSegment &segment)
{
  QJsonObject result{
      {"live", segment.live()},
      {"snapshot", snapshot(segment.snapshot())},
      {"cutoff", cutoff(segment.binding())},
      {"orderingLimitation", QString::fromUtf8(segment.orderingLimitation())},
      {"originalDeliveryAuthority", false}
  };
  const auto &ordering = segment.senderOrdering();
  result.insert(
      "senderOrdering",
      ordering ? QJsonValue(
                     QJsonObject{{"live", ordering->live()}, {"contract", QString::fromStdString(ordering->contract())}}
                 )
               : QJsonValue::Null
  );
  const auto &seal = segment.seal();
  result.insert(
      "seal", seal ? QJsonValue(
                         QJsonObject{
                             {"live", seal->live()},
                             {"origin", cutoff(seal->binding())},
                             {"contract", QString::fromStdString(seal->contract())}
                         }
                     )
                   : QJsonValue::Null
  );
  return result;
}
QJsonObject preparationBinding(const InputPreparationBinding &value)
{
  return {
      {"request", QString::number(value.request)},
      {"coreConnectionGeneration", static_cast<qint64>(value.coreConnectionGeneration)},
      {"captureEpoch", QString::number(value.capture.epoch)},
      {"captureSequence", QString::number(value.capture.sequence)},
      {"capturePart", value.capture.part}
  };
}
const char *preparationOutcome(InputPreparationOutcome value)
{
  switch (value) {
  case InputPreparationOutcome::None:
    return "none";
  case InputPreparationOutcome::NoTarget:
    return "no-target";
  case InputPreparationOutcome::Unsupported:
    return "unsupported";
  case InputPreparationOutcome::Failed:
    return "failed";
  }
  throw std::runtime_error("Invalid preparation outcome");
}
struct Ei
{
  ei *context = ei_new_sender(nullptr);
  ei_device *pointer = nullptr, *keyboard = nullptr;
  struct ei_ping *pending = nullptr;
  bool connected = false, pointerReady = false, keyboardReady = false;
  ~Ei()
  {
    if (pending)
      ei_ping_unref(pending);
    if (pointer)
      ei_device_unref(pointer);
    if (keyboard)
      ei_device_unref(keyboard);
    if (context)
      ei_unref(context);
  }
};
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  if (argc != 2)
    return 2;
  try {
    std::atomic_bool changed{true};
    GnomeProviderClient client(GnomeProviderConfig::load(QString::fromLocal8Bit(argv[1])), [&] {
      changed.store(true, std::memory_order_release);
    });
    Ei input;
    require(input.context, "Cannot create real libei sender");
    ei_configure_name(input.context, "Deskflow disposable Qt viewport acceptance");
    const int previousFlags = fcntl(STDIN_FILENO, F_GETFL);
    require(
        previousFlags >= 0 && fcntl(STDIN_FILENO, F_SETFL, previousFlags | O_NONBLOCK) == 0,
        "Cannot bound fixture control input"
    );
    QByteArray incoming;
    bool adopted = false, announced = false;
    std::uint64_t lastId = 0, pingId = 0, observationId = 0, observationRequest = 0, completedRequest = 0;
    std::optional<GnomeProviderClient::ViewportObservation> retained;
    std::optional<GnomeProviderClient::InputDrainRequest> pendingDrain;
    std::optional<GnomeProviderClient::InputDrainSegment> retainedDrain;
    std::optional<GnomeProviderClient::InputDrainSeal> retainedSeal;
    std::vector<GnomeProviderClient::InputDrainSegment> retainedBatch;
    std::optional<Clock::time_point> drainDeadline;
    std::optional<Clock::time_point> retainedDrainDeadline;
    std::uint64_t drainCommandId = 0, drainRequest = 0, completedDrainRequest = 0;
    std::uint64_t requestedPings = 0, requestedDrains = 0, requestedMaps = 0;
    std::uint64_t requestedBatches = 0;
    bool mapCommand = false;
    std::optional<GnomeProviderClient::InputPreparationRequest> pendingPreparation;
    std::optional<GnomeProviderClient::PreparedInputFocus> prepared;
    std::optional<Clock::time_point> preparationDeadline, preparationPingDeadline;
    std::uint64_t preparationCommandId = 0, preparationRequest = 0, cleanupCommandId = 0;
    std::uint64_t emulationSequence = 1, idleStops = 0, emulationRestarts = 0;
    bool emulating = false;
    auto lastSubmission = Clock::now();
    const auto ensureEmulating = [&] {
      require(input.pointerReady && input.keyboardReady, "Cannot restart an unready EI device");
      if (!emulating) {
        ++emulationSequence;
        ei_device_start_emulating(input.pointer, emulationSequence);
        ei_device_start_emulating(input.keyboard, emulationSequence);
        emulating = true;
        ++emulationRestarts;
      }
      lastSubmission = Clock::now();
    };
    GnomeProviderClient::Update identity;
    pid_t eisPeerPid = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(150);
    while (Clock::now() < deadline) {
      require(!drainDeadline || Clock::now() < *drainDeadline, "Input drain exceeded its inherited deadline");
      require(
          !preparationPingDeadline || Clock::now() < *preparationPingDeadline,
          "Preparation EI prefix PONG exceeded 250 ms"
      );
      QCoreApplication::processEvents();
      if (changed.exchange(false, std::memory_order_acq_rel)) {
        auto update = client.takeUpdate();
        if (update.descriptor >= 0) {
          require(!adopted && update.ready && !update.closed, "Unexpected provider descriptor lifecycle");
          struct ucred credentials{};
          socklen_t length = sizeof(credentials);
          int type = 0;
          socklen_t typeLength = sizeof(type);
          const bool valid = getsockopt(update.descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
                             length == sizeof(credentials) && credentials.uid == getuid() && credentials.pid > 0 &&
                             getsockopt(update.descriptor, SOL_SOCKET, SO_TYPE, &type, &typeLength) == 0 &&
                             type == SOCK_STREAM;
          if (!valid) {
            close(update.descriptor);
            throw std::runtime_error("Provider EIS descriptor has invalid local credentials/type");
          }
          eisPeerPid = credentials.pid;
          // libei owns this descriptor even if adoption fails.
          const int result = ei_setup_backend_fd(input.context, update.descriptor);
          update.descriptor = -1;
          require(result == 0, "Cannot adopt production client's EIS descriptor");
          adopted = true;
          identity = std::move(update);
        }
        if (update.closed)
          throw std::runtime_error("Production GNOME client closed: " + update.error);
      }
      if (auto update = client.takeViewportUpdate()) {
        if (!observationId) {
          // The production client also reports asynchronous revocation of a
          // completed observation. It is not a reply to a new stdin command.
          require(
              completedRequest && update->request == completedRequest && !update->observation &&
                  !update->error.empty() && (!retained || !retained->live()),
              "Unexpected unsolicited viewport observation update"
          );
          std::fprintf(stderr, "Viewport observation revoked: %s\n", update->error.c_str());
        } else {
          require(update->request == observationRequest, "Unexpected viewport observation reply");
          QJsonObject reply{
              {"event", "observed"},
              {"id", number(observationId)},
              {"clientRequest", QString::number(update->request)},
              {"error", QString::fromStdString(update->error)}
          };
          if (update->observation) {
            retained = std::move(update->observation);
            reply.insert("live", retained->live());
            reply.insert("snapshot", snapshot(retained->snapshot()));
          } else {
            reply.insert("live", false);
          }
          output(std::move(reply));
          completedRequest = observationRequest;
          observationId = observationRequest = 0;
        }
      }
      const auto drainUpdate = [&](std::optional<GnomeProviderClient::InputDrainUpdate> update, bool mapped) {
        if (!update)
          return;
        if (!drainCommandId) {
          require(
              completedDrainRequest && update->request == completedDrainRequest && !update->segment &&
                  !update->error.empty() && (!retainedDrain || !retainedDrain->live()),
              "Unexpected unsolicited input drain update"
          );
        } else {
          require(update->request == drainRequest && mapped == mapCommand, "Wrong input drain/map reply");
          QJsonObject reply{
              {"event", mapped ? "mapped" : "drained"},
              {"id", number(drainCommandId)},
              {"clientRequest", QString::number(update->request)},
              {"error", QString::fromStdString(update->error)},
              {"originalDeliveryAuthority", false},
              {"requestedPings", number(requestedPings)},
              {"requestedDrains", number(requestedDrains)},
              {"requestedMaps", number(requestedMaps)},
              {"requestedBatches", number(requestedBatches)}
          };
          if (update->segment) {
            retainedDrain = std::move(update->segment);
            if (retainedDrain->seal())
              retainedSeal = retainedDrain->seal();
            retainedDrainDeadline = drainDeadline;
            reply.insert("live", retainedDrain->live());
            reply.insert("snapshot", snapshot(retainedDrain->snapshot()));
            reply.insert("cutoff", cutoff(retainedDrain->binding()));
            reply.insert("orderingLimitation", QString::fromUtf8(retainedDrain->orderingLimitation()));
            reply.insert(
                "seal", retainedDrain->seal()
                            ? QJsonValue(
                                  QJsonObject{
                                      {"live", retainedDrain->seal()->live()},
                                      {"origin", cutoff(retainedDrain->seal()->binding())},
                                      {"contract", QString::fromStdString(retainedDrain->seal()->contract())}
                                  }
                              )
                            : QJsonValue::Null
            );
            const auto &ordering = retainedDrain->senderOrdering();
            reply.insert(
                "senderOrdering", ordering ? QJsonValue(
                                                 QJsonObject{
                                                     {"live", ordering->live()},
                                                     {"contract", QString::fromStdString(ordering->contract())}
                                                 }
                                             )
                                           : QJsonValue::Null
            );
          } else {
            reply.insert("live", false);
          }
          output(std::move(reply));
          completedDrainRequest = drainRequest;
          drainCommandId = drainRequest = 0;
          drainDeadline.reset();
        }
      };
      drainUpdate(client.takeInputDrainUpdate(), false);
      drainUpdate(client.takeInputMapUpdate(), true);
      if (auto update = client.takeInputMapBatchUpdate()) {
        if (!drainCommandId) {
          require(
              completedDrainRequest == update->request && update->segments.empty() && !update->error.empty(),
              "Unexpected unsolicited batch update"
          );
        } else {
          require(mapCommand && update->request == drainRequest, "Wrong mapped batch reply");
          retainedBatch = std::move(update->segments);
          QJsonArray segments;
          for (const auto &segment : retainedBatch)
            segments.append(segmentReceipt(segment));
          if (!retainedBatch.empty()) {
            retainedDrain = retainedBatch.back();
            retainedDrainDeadline = drainDeadline;
          }
          output(
              {{"event", "batch-mapped"},
               {"id", number(drainCommandId)},
               {"error", QString::fromStdString(update->error)},
               {"segments", segments},
               {"requestedPings", number(requestedPings)},
               {"requestedDrains", number(requestedDrains)},
               {"requestedMaps", number(requestedMaps)},
               {"requestedBatches", number(requestedBatches)},
               {"originalDeliveryAuthority", false}}
          );
          completedDrainRequest = drainRequest;
          drainCommandId = drainRequest = 0;
          drainDeadline.reset();
        }
      }
      if (auto update = client.takeInputPreparationUpdate()) {
        require(update->request == preparationRequest, "Wrong preparation reply origin");
        if (!preparationCommandId) {
          require(
              !update->focus && !update->error.empty() && (!prepared || !prepared->live()),
              "Unexpected unsolicited preparation result"
          );
          std::fprintf(stderr, "Prepared focus revoked: %s\n", update->error.c_str());
        } else {
          QJsonObject reply{
              {"event", "prepared"},
              {"id", number(preparationCommandId)},
              {"clientRequest", QString::number(update->request)},
              {"error", QString::fromStdString(update->error)},
              {"outcome", preparationOutcome(update->outcome)},
              {"live", update->focus && update->focus->live()},
              {"originalDeliveryAuthority", false},
              {"idleStops", number(idleStops)},
              {"emulationRestarts", number(emulationRestarts)},
              {"requestedPings", number(requestedPings)}
          };
          if (update->focus) {
            prepared = std::move(update->focus);
            reply.insert("binding", preparationBinding(prepared->binding()));
            reply.insert("snapshot", snapshot(prepared->snapshot()));
            reply.insert("contract", QString::fromStdString(prepared->contract()));
          }
          output(std::move(reply));
          preparationCommandId = 0;
        }
      }
      if (auto update = client.takeInputPreparationCleanupUpdate()) {
        require(cleanupCommandId && update->request == preparationRequest, "Wrong preparation cleanup reply");
        output(
            {{"event", "preparation-cleaned"},
             {"id", number(cleanupCommandId)},
             {"clientRequest", QString::number(update->request)},
             {"error", QString::fromStdString(update->error)},
             {"released", update->released},
             {"preparedLive", prepared && prepared->live()},
             {"sealLive", retainedSeal && retainedSeal->live()},
             {"originalDeliveryAuthority", false}}
        );
        cleanupCommandId = 0;
        if (update->released) {
          prepared.reset();
          preparationDeadline.reset();
          preparationRequest = 0;
        }
      }
      if (adopted) {
        bool pong = false, disconnected = false, deviceInvalidated = false;
        ei_dispatch(input.context);
        while (auto *event = ei_get_event(input.context)) {
          auto *device = ei_event_get_device(event);
          switch (ei_event_get_type(event)) {
          case EI_EVENT_CONNECT:
            input.connected = true;
            break;
          case EI_EVENT_SEAT_ADDED:
            ei_seat_bind_capabilities(
                ei_event_get_seat(event), EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_SCROLL,
                EI_DEVICE_CAP_KEYBOARD, nullptr
            );
            break;
          case EI_EVENT_DEVICE_ADDED:
            if (!input.pointer && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
              input.pointer = ei_device_ref(device);
            if (!input.keyboard && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD))
              input.keyboard = ei_device_ref(device);
            break;
          case EI_EVENT_DEVICE_RESUMED:
            if (device == input.pointer || device == input.keyboard) {
              ei_device_start_emulating(device, emulationSequence);
              emulating = true;
              lastSubmission = Clock::now();
            }
            if (device == input.pointer)
              input.pointerReady = true;
            if (device == input.keyboard)
              input.keyboardReady = true;
            break;
          case EI_EVENT_DEVICE_PAUSED:
          case EI_EVENT_DEVICE_REMOVED:
            if (device == input.pointer || device == input.keyboard)
              deviceInvalidated = true;
            if (device == input.pointer)
              input.pointerReady = false;
            if (device == input.keyboard)
              input.keyboardReady = false;
            break;
          case EI_EVENT_PONG:
            if (input.pending && ei_event_pong_get_ping(event) == input.pending)
              pong = true;
            break;
          case EI_EVENT_DISCONNECT:
            disconnected = true;
            break;
          default:
            break;
          }
          ei_event_unref(event);
        }
        // A final synthetic PONG accompanying DISCONNECT must never pass.
        require(!disconnected && client.live(), "Owned production EIS session disconnected");
        if (pong) {
          require(!deviceInvalidated && input.pointerReady && input.keyboardReady, "EIS device revoked before PONG");
          input.pending = ei_ping_unref(input.pending);
          if (pendingPreparation) {
            const auto completedAt = Clock::now();
            require(
                preparationPingDeadline && completedAt < *preparationPingDeadline,
                "Preparation EI prefix PONG exceeded 250 ms"
            );
            require(
                completedAt < pendingPreparation->deadline, "Preparation deadline expired before complete PONG batch"
            );
            // The exact PONG and entire batch are now observed. The prefix
            // wait has ended; native focus and promotion retain the original
            // independent preparation deadline, never a renewed ping budget.
            preparationPingDeadline.reset();
            preparationRequest = client.requestInputPreparation(*pendingPreparation);
            if (!preparationRequest) {
              output(
                  {{"event", "prepared"},
                   {"id", number(preparationCommandId)},
                   {"error", "preparation-unavailable"},
                   {"outcome", "unsupported"},
                   {"live", false},
                   {"originalDeliveryAuthority", false}}
              );
              preparationCommandId = 0;
              preparationDeadline.reset();
            }
            pendingPreparation.reset();
          } else if (pendingDrain) {
            // No new EI submission is accepted until this command completes.
            // The synthetic fixture correlation proves only the lower native
            // segments; it is not a production Core cutoff or delivery permit.
            require(Clock::now() < pendingDrain->deadline, "Drain deadline expired before the complete PONG batch");
            drainRequest = client.requestInputDrain(*pendingDrain);
            require(drainRequest, "Production input drain request rejected as busy/unavailable");
            ++requestedDrains;
            pendingDrain.reset();
          } else {
            output({{"event", "synced"}, {"id", number(pingId)}});
          }
          pingId = 0;
        }
        if (!announced && input.connected && input.pointerReady && input.keyboardReady) {
          auto *region = ei_device_get_region(input.pointer, 0);
          require(region, "EIS pointer has no region");
          output(
              {{"event", "ready"},
               {"session", QString::fromStdString(identity.session)},
               {"viewer", QString::fromStdString(identity.viewer)},
               {"generation", static_cast<qint64>(identity.generation)},
               {"eisPeerPid", eisPeerPid},
               {"region",
                QJsonArray{
                    static_cast<qint64>(ei_region_get_x(region)), static_cast<qint64>(ei_region_get_y(region)),
                    static_cast<qint64>(ei_region_get_width(region)), static_cast<qint64>(ei_region_get_height(region))
                }}}
          );
          announced = true;
        }
      }
      // Mirror the production ordinary sender's four-second idle stop using
      // real libei calls. A retained prefix/preparation never idles or restarts.
      if (announced && emulating && !input.pending && !preparationRequest && !preparationCommandId &&
          !(retainedSeal && retainedSeal->live()) && Clock::now() - lastSubmission >= std::chrono::seconds(4)) {
        ei_device_stop_emulating(input.pointer);
        ei_device_stop_emulating(input.keyboard);
        emulating = false;
        ++idleStops;
      }
      pollfd descriptors[]{{STDIN_FILENO, POLLIN, 0}, {adopted ? ei_get_fd(input.context) : -1, POLLIN, 0}};
      const int polled = poll(descriptors, 2, 5);
      if (polled < 0 && errno == EINTR)
        continue;
      require(polled >= 0, "Fixture poll failed");
      if (!(descriptors[0].revents & (POLLIN | POLLHUP)))
        continue;
      char bytes[1024];
      const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
      if (count < 0 && (errno == EAGAIN || errno == EINTR))
        continue;
      if (count == 0) {
        client.stop();
        return 0;
      }
      require(count > 0 && incoming.size() + count <= 4096, "Fixture control EOF/error/overflow");
      incoming.append(bytes, count);
      for (;;) {
        const auto newline = incoming.indexOf('\n');
        if (newline < 0)
          break;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(incoming.first(newline), &error);
        incoming.remove(0, newline + 1);
        require(error.error == QJsonParseError::NoError && document.isObject(), "Malformed fixture command");
        const auto command = document.object();
        const auto id = integer(command, "id");
        require(
            id > lastId && !observationId && !input.pending && !drainCommandId && !preparationCommandId &&
                !cleanupCommandId,
            "Unordered/concurrent fixture command"
        );
        lastId = id;
        const auto operation = command.value("op").toString();
        const QSet<QString> allowed =
            operation == "move" || operation == "prepare" ? QSet<QString>{"id", "op", "x", "y"}
            : operation == "scroll"                       ? QSet<QString>{"id", "op", "dx", "dy"}
            : operation == "key"                          ? QSet<QString>{"id", "op", "code"}
            : operation == "map-batch" ? QSet<QString>{"id", "op", "generation", "connection", "count"}
            : operation == "drain" || operation == "seal" || operation == "map" || operation == "promote"
                ? QSet<QString>{"id", "op", "generation", "connection"}
                : QSet<QString>{"id", "op"};
        for (auto it = command.begin(); it != command.end(); ++it)
          require(allowed.contains(it.key()), "Unknown fixture command field");
        if (operation == "stop") {
          client.stop();
          output({{"event", "stopped"}, {"id", number(id)}, {"live", client.live()}});
          return 0;
        }
        require(announced && client.live(), "Command before production provider/EIS readiness");
        if (operation == "idle-state") {
          output(
              {{"event", "idle-state"},
               {"id", number(id)},
               {"emulating", emulating},
               {"idleStops", number(idleStops)},
               {"emulationRestarts", number(emulationRestarts)},
               {"idleMs",
                static_cast<qint64>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastSubmission).count()
                )}}
          );
          continue;
        }
        if (operation == "preparation-cleanup") {
          require(
              preparationRequest &&
                  client.cancelInputPreparation(preparationRequest, Clock::now() + inputCutoffMaximumLifetime),
              "Exact preparation cleanup was refused"
          );
          cleanupCommandId = id;
          continue;
        }
        if (operation == "preparation-retained") {
          require(prepared && preparationDeadline && preparationRequest, "No prepared focus lifetime was retained");
          const auto now = Clock::now();
          output(
              {{"event", "preparation-retained"},
               {"id", number(id)},
               {"clientRequest", QString::number(preparationRequest)},
               {"live", prepared->live()},
               {"prefixPending", preparationPingDeadline.has_value()},
               {"ageMs", static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 now - (preparationDeadline.value() - inputPreparationMaximumLifetime)
                         )
                                                 .count())},
               {"remainingMs",
                static_cast<qint64>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(preparationDeadline.value() - now).count()
                )},
               {"originalDeliveryAuthority", false}}
          );
          continue;
        }
        if (operation == "promote") {
          require(prepared && prepared->live() && preparationDeadline, "No live prepared focus for promotion");
          const auto &observed = prepared->snapshot();
          const auto &paint = observed.paint;
          InputCutoffBinding binding{
              id,
              prepared->binding().coreConnectionGeneration,
              prepared->binding().capture,
              {{paint.session, paint.epoch},
               integer(command, "generation"),
               paint.focusRequest,
               paint.focusRevision,
               paint.geometry,
               ForwardingScope::All},
              integer(command, "connection")
          };
          const auto pointDeadline = std::min(preparationDeadline.value(), Clock::now() + inputCutoffMaximumLifetime);
          require(observed.routedPoint.has_value(), "Prepared focus lost its exact routed point");
          drainRequest =
              client.promoteInputPreparation(*prepared, {binding, *observed.routedPoint, pointDeadline, true});
          require(drainRequest, "Exact prepared All promotion rejected");
          drainDeadline = pointDeadline;
          drainCommandId = id;
          mapCommand = false;
          ++requestedDrains;
          continue;
        }
        if (operation == "observe") {
          observationRequest = client.requestViewportObservation();
          require(observationRequest, "Production viewport request rejected as busy/unavailable");
          observationId = id;
          continue;
        }
        if (operation == "retained") {
          output({{"event", "retained"}, {"id", number(id)}, {"live", retained && retained->live()}});
          continue;
        }
        if (operation == "drain-retained") {
          require(retainedDrain && retainedDrainDeadline, "No native drain segment was retained");
          QJsonArray batchLive;
          for (const auto &segment : retainedBatch)
            batchLive.append(segment.live());
          output(
              {{"event", "drain-retained"},
               {"id", number(id)},
               {"live", retainedDrain->live()},
               {"senderOrderingLive", retainedDrain->senderOrdering() && retainedDrain->senderOrdering()->live()},
               {"sealLive", retainedSeal && retainedSeal->live()},
               {"batchLive", batchLive},
               {"remainingMs",
                static_cast<qint64>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(*retainedDrainDeadline - Clock::now()).count()
                )}}
          );
          continue;
        }
        require(input.pointerReady && input.keyboardReady, "EIS devices are not ready");
        if (operation == "prepare") {
          require(
              !preparationRequest && !(retainedSeal && retainedSeal->live()), "Preparation requires an ordinary sender"
          );
          const auto x = command.value("x"), y = command.value("y");
          require(
              x.isDouble() && y.isDouble() && std::isfinite(x.toDouble()) && std::isfinite(y.toDouble()) &&
                  ei_device_get_region_at(input.pointer, x.toDouble(), y.toDouble()),
              "Invalid preparation point"
          );
          pendingPreparation = GnomeProviderClient::InputPreparationRequest{
              {id, 1, {1, id, 1}}, {1, id, x.toDouble(), y.toDouble()}, Clock::now() + inputPreparationMaximumLifetime
          };
          preparationDeadline = pendingPreparation->deadline;
          preparationPingDeadline = Clock::now() + inputCutoffMaximumLifetime;
          preparationCommandId = id;
          ensureEmulating();
        } else if (operation == "drain" || operation == "seal" || operation == "map" || operation == "map-batch") {
          require(retained.has_value(), "Input drain requires a retained painted snapshot");
          const auto &observed = retained->snapshot();
          const auto &paint = observed.paint;
          InputCutoffBinding binding{
              id,
              1,
              {1, id, 1},
              {{paint.session, paint.epoch},
               integer(command, "generation"),
               paint.focusRequest,
               paint.focusRevision,
               paint.geometry,
               ForwardingScope::All},
              integer(command, "connection")
          };
          require(validInputCutoffBinding(binding), "Invalid fixture cutoff binding");
          pendingDrain = GnomeProviderClient::InputDrainRequest{
              binding,
              {binding.capture.epoch, binding.capture.sequence, observed.desktopX, observed.desktopY},
              Clock::now() + inputCutoffMaximumLifetime,
              operation == "seal"
          };
          drainDeadline = pendingDrain->deadline;
          drainCommandId = id;
          mapCommand = operation == "map" || operation == "map-batch";
          if (mapCommand) {
            require(retainedSeal && retainedSeal->live(), "Map requires a live retained prefix seal");
            if (operation == "map-batch") {
              const auto count = integer(command, "count");
              require(count && count <= inputCutoffMaximumBatch, "Invalid batch count");
              std::vector<GnomeProviderClient::InputDrainRequest> points;
              for (std::uint64_t i = 0; i < count; ++i) {
                auto point = *pendingDrain;
                point.cutoff.request += i;
                point.cutoff.capture.sequence += i;
                point.point.sequence += i;
                point.point.desktopX += i % 4;
                point.point.desktopY += i / 4;
                points.push_back(point);
              }
              lastId = id + count - 1;
              drainRequest = client.requestInputMapBatch(*retainedSeal, std::move(points));
              ++requestedBatches;
            } else {
              drainRequest = client.requestInputMap(*retainedSeal, *pendingDrain);
              ++requestedMaps;
            }
            require(drainRequest, "Production input map request rejected as busy/unavailable");
            pendingDrain.reset();
            continue; // A fresh point does not submit another EI event or ping.
          }
          ensureEmulating();
        } else if (operation == "move") {
          ensureEmulating();
          const auto x = command.value("x"), y = command.value("y");
          require(
              x.isDouble() && y.isDouble() && std::isfinite(x.toDouble()) && std::isfinite(y.toDouble()) &&
                  ei_device_get_region_at(input.pointer, x.toDouble(), y.toDouble()),
              "Invalid EIS move"
          );
          ei_device_pointer_motion_absolute(input.pointer, x.toDouble(), y.toDouble());
          ei_device_frame(input.pointer, ei_now(input.context));
        } else if (operation == "key" || operation == "escape") {
          ensureEmulating();
          const auto code = operation == "escape" ? 1 : integer(command, "code");
          require(code <= 767, "Key stroke is outside the Linux evdev key range");
          // A complete synthetic stroke only; this fixture never transfers a
          // held-key owner or grants original-input delivery authority.
          ei_device_keyboard_key(input.keyboard, static_cast<std::uint32_t>(code), true);
          ei_device_frame(input.keyboard, ei_now(input.context));
          ei_device_keyboard_key(input.keyboard, static_cast<std::uint32_t>(code), false);
          ei_device_frame(input.keyboard, ei_now(input.context));
        } else if (operation == "scroll") {
          ensureEmulating();
          require(ei_device_has_capability(input.pointer, EI_DEVICE_CAP_SCROLL), "Pointer has no scroll capability");
          const auto dx = scrollDelta(command, "dx"), dy = scrollDelta(command, "dy");
          require(dx || dy, "Scroll fixture requires a nonzero delta");
          ei_device_scroll_discrete(input.pointer, dx, dy);
          ei_device_frame(input.pointer, ei_now(input.context));
        } else if (operation == "click") {
          ensureEmulating();
          require(ei_device_has_capability(input.pointer, EI_DEVICE_CAP_BUTTON), "Pointer has no button capability");
          ei_device_button_button(input.pointer, 272, true);
          ei_device_frame(input.pointer, ei_now(input.context));
          ei_device_button_button(input.pointer, 272, false);
          ei_device_frame(input.pointer, ei_now(input.context));
        } else {
          require(operation == "sync", "Unknown fixture operation");
        }
        pingId = id;
        ++requestedPings;
        input.pending = ei_new_ping(input.context);
        require(input.pending, "Cannot create real EI sync fence");
        ei_ping(input.pending);
      }
    }
    throw std::runtime_error("Whole Qt viewport fixture exceeded 150 seconds");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
