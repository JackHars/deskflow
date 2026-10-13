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
  require(bytes.size() <= 16384, "Fixture output exceeds bound");
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
    std::optional<Clock::time_point> drainDeadline;
    std::optional<Clock::time_point> retainedDrainDeadline;
    std::uint64_t drainCommandId = 0, drainRequest = 0, completedDrainRequest = 0;
    GnomeProviderClient::Update identity;
    pid_t eisPeerPid = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(150);
    while (Clock::now() < deadline) {
      require(!drainDeadline || Clock::now() < *drainDeadline, "Input drain exceeded its inherited deadline");
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
      if (auto update = client.takeInputDrainUpdate()) {
        if (!drainCommandId) {
          require(
              completedDrainRequest && update->request == completedDrainRequest && !update->segment &&
                  !update->error.empty() && (!retainedDrain || !retainedDrain->live()),
              "Unexpected unsolicited input drain update"
          );
        } else {
          require(update->request == drainRequest, "Wrong input drain reply");
          QJsonObject reply{
              {"event", "drained"},
              {"id", number(drainCommandId)},
              {"clientRequest", QString::number(update->request)},
              {"error", QString::fromStdString(update->error)},
              {"originalDeliveryAuthority", false}
          };
          if (update->segment) {
            retainedDrain = std::move(update->segment);
            retainedDrainDeadline = drainDeadline;
            reply.insert("live", retainedDrain->live());
            reply.insert("snapshot", snapshot(retainedDrain->snapshot()));
            reply.insert("cutoff", cutoff(retainedDrain->binding()));
            reply.insert("orderingLimitation", QString::fromUtf8(retainedDrain->orderingLimitation()));
          } else {
            reply.insert("live", false);
          }
          output(std::move(reply));
          completedDrainRequest = drainRequest;
          drainCommandId = drainRequest = 0;
          drainDeadline.reset();
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
            if (device == input.pointer || device == input.keyboard)
              ei_device_start_emulating(device, 1);
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
          if (pendingDrain) {
            // No new EI submission is accepted until this command completes.
            // The synthetic fixture correlation proves only the lower native
            // segments; it is not a production Core cutoff or delivery permit.
            require(Clock::now() < pendingDrain->deadline, "Drain deadline expired before the complete PONG batch");
            drainRequest = client.requestInputDrain(*pendingDrain);
            require(drainRequest, "Production input drain request rejected as busy/unavailable");
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
            id > lastId && !observationId && !input.pending && !drainCommandId, "Unordered/concurrent fixture command"
        );
        lastId = id;
        const auto operation = command.value("op").toString();
        const QSet<QString> allowed = operation == "move"     ? QSet<QString>{"id", "op", "x", "y"}
                                      : operation == "scroll" ? QSet<QString>{"id", "op", "dx", "dy"}
                                      : operation == "key"    ? QSet<QString>{"id", "op", "code"}
                                      : operation == "drain"  ? QSet<QString>{"id", "op", "generation", "connection"}
                                                              : QSet<QString>{"id", "op"};
        for (auto it = command.begin(); it != command.end(); ++it)
          require(allowed.contains(it.key()), "Unknown fixture command field");
        if (operation == "stop") {
          client.stop();
          output({{"event", "stopped"}, {"id", number(id)}, {"live", client.live()}});
          return 0;
        }
        require(announced && client.live(), "Command before production provider/EIS readiness");
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
          output(
              {{"event", "drain-retained"},
               {"id", number(id)},
               {"live", retainedDrain->live()},
               {"remainingMs",
                static_cast<qint64>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(*retainedDrainDeadline - Clock::now()).count()
                )}}
          );
          continue;
        }
        require(input.pointerReady && input.keyboardReady, "EIS devices are not ready");
        if (operation == "drain") {
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
              Clock::now() + inputCutoffMaximumLifetime
          };
          drainDeadline = pendingDrain->deadline;
          drainCommandId = id;
        } else if (operation == "move") {
          const auto x = command.value("x"), y = command.value("y");
          require(
              x.isDouble() && y.isDouble() && std::isfinite(x.toDouble()) && std::isfinite(y.toDouble()) &&
                  ei_device_get_region_at(input.pointer, x.toDouble(), y.toDouble()),
              "Invalid EIS move"
          );
          ei_device_pointer_motion_absolute(input.pointer, x.toDouble(), y.toDouble());
          ei_device_frame(input.pointer, ei_now(input.context));
        } else if (operation == "key" || operation == "escape") {
          const auto code = operation == "escape" ? 1 : integer(command, "code");
          require(code <= 767, "Key stroke is outside the Linux evdev key range");
          // A complete synthetic stroke only; this fixture never transfers a
          // held-key owner or grants original-input delivery authority.
          ei_device_keyboard_key(input.keyboard, static_cast<std::uint32_t>(code), true);
          ei_device_frame(input.keyboard, ei_now(input.context));
          ei_device_keyboard_key(input.keyboard, static_cast<std::uint32_t>(code), false);
          ei_device_frame(input.keyboard, ei_now(input.context));
        } else if (operation == "scroll") {
          require(ei_device_has_capability(input.pointer, EI_DEVICE_CAP_SCROLL), "Pointer has no scroll capability");
          const auto dx = scrollDelta(command, "dx"), dy = scrollDelta(command, "dy");
          require(dx || dy, "Scroll fixture requires a nonzero delta");
          ei_device_scroll_discrete(input.pointer, dx, dy);
          ei_device_frame(input.pointer, ei_now(input.context));
        } else if (operation == "click") {
          require(ei_device_has_capability(input.pointer, EI_DEVICE_CAP_BUTTON), "Pointer has no button capability");
          ei_device_button_button(input.pointer, 272, true);
          ei_device_frame(input.pointer, ei_now(input.context));
          ei_device_button_button(input.pointer, 272, false);
          ei_device_frame(input.pointer, ei_now(input.context));
        } else {
          require(operation == "sync", "Unknown fixture operation");
        }
        pingId = id;
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
