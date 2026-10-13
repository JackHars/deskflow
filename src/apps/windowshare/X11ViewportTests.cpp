/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Viewer.h"
#include "X11DisplayObserver.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xinput.h>

using namespace deskflow::windowshare;
namespace {
void check(bool condition, const std::string &message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> using Owned = std::unique_ptr<T, decltype(&free)>;
template <class T> Owned<T> own(T *value)
{
  return Owned<T>(value, &free);
}
void checked(xcb_connection_t *connection, xcb_void_cookie_t cookie, const char *what)
{
  const auto error = own(xcb_request_check(connection, cookie));
  check(!error, std::string(what) + (error ? ": X error " + std::to_string(error->error_code) : ""));
}
bool waitUntil(const std::function<bool()> &condition, int timeout = 4000)
{
  QElapsedTimer elapsed;
  elapsed.start();
  while (!condition() && elapsed.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(2);
  }
  return condition();
}
struct Connection
{
  xcb_connection_t *value;
  explicit Connection(const char *display = nullptr) : value(xcb_connect(display, nullptr))
  {
    check(value && !xcb_connection_has_error(value), "Cannot connect to the private authenticated Xvfb");
  }
  ~Connection()
  {
    xcb_disconnect(value);
  }
  operator xcb_connection_t *() const
  {
    return value;
  }
  std::uint32_t root(int index = 0) const
  {
    auto screens = xcb_setup_roots_iterator(xcb_get_setup(value));
    for (int i = 0; i < index && screens.rem; ++i)
      xcb_screen_next(&screens);
    check(screens.rem, "Private Xvfb needs two native screens");
    return screens.data->root;
  }
};
std::uint16_t selectFixtureMaster(xcb_connection_t *connection)
{
  // Test setup only: choose an actual enabled native master, never assume ID2.
  // The production observer itself must never set a client's pointer.
  auto devices = own(xcb_input_xi_query_device_reply(
      connection, xcb_input_xi_query_device(connection, XCB_INPUT_DEVICE_ALL_MASTER), nullptr
  ));
  check(bool(devices), "Private Xvfb has no XI2 master-device query");
  for (auto i = xcb_input_xi_query_device_infos_iterator(devices.get()); i.rem; xcb_input_xi_device_info_next(&i))
    if (i.data->enabled && i.data->type == XCB_INPUT_DEVICE_TYPE_MASTER_POINTER) {
      checked(
          connection, xcb_input_xi_set_client_pointer_checked(connection, XCB_NONE, i.data->deviceid),
          "Selecting fixture master pointer"
      );
      return i.data->deviceid;
    }
  throw std::runtime_error("Private Xvfb has no enabled master pointer");
}
void pump(Connection &connection, X11DisplayObserver &observer, std::uint32_t surface = 0)
{
  while (auto *event = xcb_poll_for_event(connection)) {
    observer.handleEvent(*event);
    free(event);
  }
  observer.poll();
  if (!observer.pending())
    observer.refresh(surface);
}
X11DisplayObservation observe(Connection &connection, X11DisplayObserver &observer, std::uint32_t surface = 0)
{
  check(
      waitUntil([&] {
        pump(connection, observer, surface);
        return observer.observe() && observer.observe()->snapshot().surface == surface;
      }),
      "Native observation unavailable: " + observer.unavailableReason()
  );
  return *observer.observe();
}
void pumpCoordinates(Connection &connection, X11DisplayObserver &observer, X11CoordinateEndpoint &endpoint)
{
  while (auto *event = xcb_poll_for_event(connection)) {
    observer.handleEvent(*event);
    endpoint.handleEvent(*event);
    free(event);
  }
  observer.poll();
  endpoint.poll();
}
X11CoordinateEndpoint::Update
coordinateUpdate(Connection &connection, X11DisplayObserver &observer, X11CoordinateEndpoint &endpoint)
{
  std::optional<X11CoordinateEndpoint::Update> result;
  check(
      waitUntil([&] {
        pumpCoordinates(connection, observer, endpoint);
        if (!result)
          result = endpoint.takeUpdate();
        return result.has_value();
      }),
      "Coordinate endpoint did not settle its bounded request"
  );
  return std::move(*result);
}
auto coordinateDeadline()
{
  return X11CoordinateClock::now() + std::chrono::milliseconds(250);
}
std::uint32_t window(Connection &connection, int x, int y, unsigned width = 400, unsigned height = 300)
{
  const auto id = xcb_generate_id(connection);
  const std::uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE;
  checked(
      connection,
      xcb_create_window_checked(
          connection, XCB_COPY_FROM_PARENT, id, connection.root(), static_cast<std::int16_t>(x),
          static_cast<std::int16_t>(y), static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), 0,
          XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask
      ),
      "Creating fixture window"
  );
  checked(connection, xcb_map_window_checked(connection, id), "Mapping fixture window");
  return id;
}

void nativeObservations(const char *otherDisplay)
{
  check(!X11DisplayObservation{}.live(), "Default native observation must deny");
  X11DisplayObserver missing(nullptr, 0);
  check(!missing.observe() && !missing.unavailableReason().empty(), "Missing native connection must deny");
  Connection connection;
  const auto pointerBefore = own(
      xcb_input_xi_get_client_pointer_reply(connection, xcb_input_xi_get_client_pointer(connection, XCB_NONE), nullptr)
  );
  check(bool(pointerBefore), "Cannot inspect private connection client pointer");
  if (!pointerBefore->set) {
    X11DisplayObserver unset(connection, 0);
    const bool denied = waitUntil([&] {
      pump(connection, unset);
      return unset.unavailableReason().find("no explicit XI") != std::string::npos;
    });
    check(denied, "Unset XI client pointer was not explicitly unavailable: " + unset.unavailableReason());
    const auto after = own(xcb_input_xi_get_client_pointer_reply(
        connection, xcb_input_xi_get_client_pointer(connection, XCB_NONE), nullptr
    ));
    check(after && !after->set, "Observer silently chose an XI client pointer");
  }
  const auto master = selectFixtureMaster(connection);
  {
    // Force only the two initialization phases to complete. These geometry
    // round trips belong to the fixture and do not change client-pointer state.
    // The next phase has issued checked watcher creation/subscription requests
    // but has not consumed their verification barrier yet.
    X11DisplayObserver barrier(connection, 0);
    barrier.refresh();
    auto sync = [&] {
      auto reply = own(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, connection.root()), nullptr));
      check(bool(reply), "Cannot synchronize private watcher fixture");
    };
    sync();
    barrier.poll();
    sync();
    barrier.poll();
    check(barrier.pending() && !barrier.observe(), "Watcher barrier must precede any observation");
    barrier.invalidate(); // Xlib owners invalidate initial native RandR notifications this way.
    check(barrier.pending() && !barrier.observe(), "Invalidation discarded the unchecked watcher barrier");
    check(
        waitUntil([&] {
          barrier.poll();
          return barrier.observe().has_value();
        }),
        "Invalidated watcher barrier did not complete its verified native reads"
    );
    const auto retained = *barrier.observe();
    barrier.close();
    check(!retained.live(), "Watcher fixture close retained live evidence");
  }
  const auto rootAttributes =
      own(xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, connection.root()), nullptr)
      );
  check(bool(rootAttributes), "Cannot read root event mask");
  X11DisplayObserver observer(connection, 0);
  auto initial = observe(connection, observer);
  check(
      initial.snapshot().socketPeer.pid && initial.snapshot().socketPeer.processStartTicks &&
          initial.snapshot().root == connection.root() && initial.snapshot().clientPointer == master &&
          !initial.snapshot().topology.empty() && !initial.snapshot().outputs.empty(),
      "Missing actual native identity/topology"
  );
  const auto afterAttributes =
      own(xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, connection.root()), nullptr)
      );
  check(
      afterAttributes && rootAttributes->your_event_mask == afterAttributes->your_event_mask,
      "Observer replaced the owner's root event mask"
  );
  observer.refresh();
  check(initial.live(), "Starting an unchanged refresh immediately revoked the previous observation");
  check(
      waitUntil([&] {
        pump(connection, observer);
        return observer.observe() && observer.observe()->snapshot().revision == initial.snapshot().revision;
      }),
      "Unchanged observation failed"
  );

  Connection sameServer;
  selectFixtureMaster(sameServer);
  X11DisplayObserver independent(sameServer, 0);
  auto same = observe(sameServer, independent);
  check(
      same.snapshot().socketPeer == initial.snapshot().socketPeer && same.snapshot().root == initial.snapshot().root &&
          same.snapshot().connectionIncarnation != initial.snapshot().connectionIncarnation,
      "Independent connections must retain distinct local lifetimes and actual peer provenance"
  );
  X11DisplayObserver otherScreen(sameServer, 1);
  auto secondScreen = observe(sameServer, otherScreen);
  check(
      secondScreen.snapshot().desktop == initial.snapshot().desktop &&
          secondScreen.snapshot().root != initial.snapshot().root && secondScreen.snapshot().screen == 1,
      "Equal native screen bounds must not collapse distinct X roots"
  );
  Connection elsewhere(otherDisplay);
  selectFixtureMaster(elsewhere);
  X11DisplayObserver otherServer(elsewhere, 0);
  const auto other = observe(elsewhere, otherServer);
  check(
      other.snapshot().desktop == initial.snapshot().desktop &&
          other.snapshot().socketPeer != initial.snapshot().socketPeer,
      "Equal private server geometry must not manufacture matching socket provenance"
  );

  // An observer must not consume the owner's event queue, even while reading replies.
  const auto surface = window(connection, 80, 100);
  xcb_client_message_event_t sentinel{};
  sentinel.response_type = XCB_CLIENT_MESSAGE;
  sentinel.window = surface;
  sentinel.format = 32;
  sentinel.data.data32[0] = 0x44534b46;
  checked(
      connection,
      xcb_send_event_checked(
          connection, false, surface, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&sentinel)
      ),
      "Sending private sentinel"
  );
  observer.invalidate();
  observer.refresh();
  check(
      waitUntil([&] {
        observer.poll();
        return !observer.pending();
      }),
      "Observation did not finish without event dequeue"
  );
  bool found = false;
  while (auto *event = xcb_poll_for_event(connection)) {
    if ((event->response_type & 0x7f) == XCB_CLIENT_MESSAGE)
      found |= reinterpret_cast<xcb_client_message_event_t *>(event)->data.data32[0] == sentinel.data.data32[0];
    observer.handleEvent(*event);
    free(event);
  }
  check(found, "Observer consumed an unrelated owner event");
  checked(
      connection, xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_POINTER_ROOT, surface, XCB_CURRENT_TIME),
      "Focusing private fixture"
  );
  auto visible = observe(connection, observer, surface);
  check(
      visible.snapshot().client == X11NativeRect{80, 100, 400, 300} && visible.snapshot().viewable &&
          visible.snapshot().focusWithinSurface,
      "Actual translated surface rectangle/focus is wrong"
  );
  const std::uint32_t moved[] = {113, 157};
  checked(
      connection, xcb_configure_window_checked(connection, surface, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, moved),
      "Moving private fixture"
  );
  check(
      waitUntil([&] {
        pump(connection, observer, surface);
        return !visible.live();
      }),
      "Configure did not revoke retained observation"
  );
  const auto movedObservation = observe(connection, observer, surface);
  check(
      movedObservation.snapshot().client.x == 113 && movedObservation.snapshot().client.y == 157,
      "Native surface origin did not update"
  );

  // Same-size RandR change: primary output and custom monitor arrangement.
  auto beforeLayout = movedObservation;
  const auto output = beforeLayout.snapshot().outputs.front().output;
  const auto newPrimary = beforeLayout.snapshot().primaryOutput == output ? XCB_NONE : output;
  checked(
      connection, xcb_randr_set_output_primary_checked(connection, connection.root(), newPrimary),
      "Changing private primary output"
  );
  check(
      waitUntil([&] {
        pump(connection, observer, surface);
        return !beforeLayout.live() && observer.observe() && observer.observe()->snapshot().primaryOutput == newPrimary;
      }),
      "Same-size RandR output change did not invalidate and resample"
  );
  auto layout = *observer.observe();
  check(
      layout.snapshot().desktop == beforeLayout.snapshot().desktop &&
          layout.snapshot().topology != beforeLayout.snapshot().topology,
      "Layout identity incorrectly reduced to root dimensions"
  );

  const auto crtc = layout.snapshot().outputs.front().crtc;
  xcb_render_transform_t transform{65536, 0, 65536, 0, 65536, 0, 0, 0, 65536};
  auto transformError = own(xcb_request_check(
      connection, xcb_randr_set_crtc_transform_checked(connection, crtc, transform, 0, "", 0, nullptr)
  ));
  if (!transformError) {
    check(
        waitUntil([&] {
          pump(connection, observer, surface);
          return observer.observe() && observer.observe()->snapshot().topology != layout.snapshot().topology;
        }),
        "Pending RandR transform change was omitted from canonical topology"
    );
    check(
        !observer.observe()->snapshot().outputs.front().simpleMapping && !layout.live(),
        "A pending transformed output remained a simple mapping"
    );
    transform.matrix13 = 0;
    checked(
        connection, xcb_randr_set_crtc_transform_checked(connection, crtc, transform, 0, "", 0, nullptr),
        "Restoring private transform"
    );
  } else {
    std::cout << "LIMITATION: Xvfb does not implement SetCrtcTransform (X error "
              << unsigned(transformError->error_code)
              << "); current/pending transform fields still read on every pass\n";
  }
  auto fresh = observe(connection, observer, surface);
  check(
      waitUntil(
          [&] {
            pump(connection, observer, surface);
            return !fresh.live();
          },
          1000
      ),
      "Retained observation expiry was extended by subsequent refreshes"
  );
  auto retained = observe(connection, observer, surface);
  observer.close();
  check(!retained.live() && !observer.observe(), "Closing observer did not revoke retained observations");
  observer.refresh(surface);
  observer.poll();
  check(!observer.observe(), "Closed observer revived");
  checked(connection, xcb_destroy_window_checked(connection, surface), "Destroying private fixture");
  std::cout << "PASS native connection/root identity, event ownership, geometry, RandR, expiry and closure\n";
}

void namespaceWitnesses(const char *otherDisplay)
{
  check(!X11NamespaceWitness{}.live() && !X11PointValidation{}.live(), "Empty coordinate receipts must deny");
  Connection coreConnection, viewerConnection, foreignConnection(otherDisplay);
  selectFixtureMaster(coreConnection);
  selectFixtureMaster(viewerConnection);
  selectFixtureMaster(foreignConnection);
  X11DisplayObserver core(coreConnection, 0), viewer(viewerConnection, 0), foreign(foreignConnection, 0);
  X11DisplayObserver otherRoot(viewerConnection, 1);
  observe(coreConnection, core);
  observe(viewerConnection, viewer);
  observe(foreignConnection, foreign);
  observe(viewerConnection, otherRoot);
  X11CoordinateEndpoint publisher(core), reader(viewer), foreignReader(foreign), otherRootReader(otherRoot);
  check(!publisher.create(0, coordinateDeadline()), "Zero witness request accepted");
  check(!publisher.create(1, X11CoordinateClock::now()), "Expired witness request accepted");
  check(publisher.create(1, coordinateDeadline()), "Could not create actual-connection witness");
  const auto created = coordinateUpdate(coreConnection, core, publisher);
  check(created.witness && created.witness->live() && created.error.empty(), "Create failed: " + created.error);
  const auto published = *created.witness;
  const auto locator = published.snapshot().locator;
  check(!publisher.create(1, coordinateDeadline()), "Witness request ID was reused");
  check(reader.read(locator, coordinateDeadline()), "Locator-only read did not start");
  const auto read = coordinateUpdate(viewerConnection, viewer, reader);
  check(
      read.witness && read.witness->live() && read.witness->snapshot().secret == published.snapshot().secret &&
          read.witness->snapshot().native.snapshot().root == published.snapshot().native.snapshot().root &&
          read.witness->snapshot().native.snapshot().topology == published.snapshot().native.snapshot().topology &&
          read.witness->snapshot().native.snapshot().connectionIncarnation !=
              published.snapshot().native.snapshot().connectionIncarnation,
      "Same-server independent native connections did not rendezvous: " + read.error
  );
  check(!reader.confirm(published), "Reader accepted another endpoint's publisher receipt");
  check(publisher.confirm(published), "Exact live publisher confirmation refused");
  const auto confirmed = coordinateUpdate(coreConnection, core, publisher);
  check(
      confirmed.witness && confirmed.witness->live() &&
          confirmed.witness->snapshot().secret == published.snapshot().secret,
      "Exact witness confirmation failed: " + confirmed.error
  );
  check(foreignReader.read(locator, coordinateDeadline()), "Foreign-server negative fixture did not start");
  const auto foreignResult = coordinateUpdate(foreignConnection, foreign, foreignReader);
  check(!foreignResult.witness && !foreignResult.error.empty(), "Foreign native server acquired namespace witness");
  check(otherRootReader.read(locator, coordinateDeadline()), "Wrong-root negative fixture did not start");
  const auto wrongRoot = coordinateUpdate(viewerConnection, otherRoot, otherRootReader);
  check(!wrongRoot.witness && !wrongRoot.error.empty(), "Same-size other root acquired namespace witness");
  check(!publisher.cancel(999) && published.live(), "Wrong request canceled a live publisher");
  check(publisher.cancel(1) && !published.live() && !confirmed.witness->live(), "Cancellation did not revoke copies");
  check(!publisher.confirm(published), "Canceled publisher revived through confirmation");

  auto publish = [&](std::uint64_t request) {
    // Refresh an independently bounded observation for each new transaction.
    core.invalidate();
    observe(coreConnection, core);
    check(publisher.create(request, coordinateDeadline()), "Fresh witness admission failed");
    auto result = coordinateUpdate(coreConnection, core, publisher);
    check(result.witness && result.witness->live(), "Fresh witness publication failed: " + result.error);
    return *result.witness;
  };
  auto malformed = publish(2);
  const auto badLocator = malformed.snapshot().locator;
  const std::uint8_t shortValue[] = {1, 2, 3};
  checked(
      coreConnection,
      xcb_change_property_checked(
          coreConnection, XCB_PROP_MODE_REPLACE, badLocator.window, badLocator.property, badLocator.type, 8,
          sizeof(shortValue), shortValue
      ),
      "Replacing private witness with malformed bytes"
  );
  viewer.invalidate();
  observe(viewerConnection, viewer);
  check(reader.read(badLocator, coordinateDeadline()), "Malformed-property read did not start");
  const auto shortRead = coordinateUpdate(viewerConnection, viewer, reader);
  check(!shortRead.witness && !shortRead.error.empty(), "Short witness property was accepted");
  pumpCoordinates(coreConnection, core, publisher);
  check(!malformed.live(), "Owner-delivered PropertyNotify did not revoke the published witness");
  auto destroyed = publish(3);
  checked(
      coreConnection, xcb_destroy_window_checked(coreConnection, destroyed.snapshot().locator.window),
      "Destroying private witness"
  );
  pumpCoordinates(coreConnection, core, publisher);
  check(!destroyed.live(), "Owner-delivered DestroyNotify did not revoke the witness");
  auto afterDestroy = publish(4);
  check(
      afterDestroy.snapshot().locator.window != destroyed.snapshot().locator.window,
      "Destroyed resource was not replaced with a fresh private XID"
  );
  publisher.close();
  check(!afterDestroy.live(), "Endpoint close retained live namespace evidence");
  check(!publisher.create(5, coordinateDeadline()), "Closed namespace endpoint revived");
  std::cout
      << "PASS native namespace create/read/confirm, foreign server/root denial, overwrite, destroy and cancellation\n";
}

void nativePointValidation()
{
  Connection connection;
  selectFixtureMaster(connection);
  const auto surface = window(connection, 100, 120, 400, 300);
  checked(
      connection, xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_POINTER_ROOT, surface, XCB_CURRENT_TIME),
      "Focusing point fixture"
  );
  X11DisplayObserver observer(connection, 0);
  observe(connection, observer, surface);
  X11CoordinateEndpoint endpoint(observer);
  std::uint64_t request = 0;
  auto point = [&](int x, int y) {
    observer.invalidate();
    observe(connection, observer, surface);
    X11PointRequest value{++request, {8, request, 1}, x, y, coordinateDeadline()};
    check(endpoint.validatePoint(value), "Frozen point request did not start");
    auto result = coordinateUpdate(connection, observer, endpoint);
    check(result.request == request, "Point response lost exact request correlation");
    if (result.point)
      check(
          result.point->snapshot().capture == value.capture && result.point->snapshot().rootX == x &&
              result.point->snapshot().rootY == y,
          "Point response substituted capture or routed coordinates"
      );
    return result;
  };
  auto hit = point(200, 220);
  check(
      hit.point && hit.point->live() && hit.point->snapshot().clientX == 100 && hit.point->snapshot().clientY == 100,
      "Native point did not map to the actual client: " + hit.error
  );
  const auto copy = *hit.point;
  check(!endpoint.cancel(999) && copy.live(), "Wrong point cancellation revoked evidence");
  check(endpoint.cancel(request) && !copy.live(), "Exact point cancellation did not revoke retained copy");
  check(!point(50, 50).point, "Outside point acquired native validation");
  const auto occluder = window(connection, 150, 170, 200, 200);
  check(!point(200, 220).point, "Topmost mapped input window did not occlude the Viewer");
  const xcb_rectangle_t hole[] = {{0, 0, 20, 20}};
  checked(
      connection,
      xcb_shape_rectangles_checked(
          connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED, occluder, 0, 0, 1, hole
      ),
      "Shaping private input occluder"
  );
  auto shaped = point(200, 220);
  check(shaped.point && shaped.point->live(), "Native input-shape hole failed to expose Viewer: " + shaped.error);
  check(!point(160, 180).point, "Native shaped input region did not occlude Viewer");
  checked(connection, xcb_destroy_window_checked(connection, occluder), "Removing private occluder");
  const auto child = xcb_generate_id(connection);
  checked(
      connection,
      xcb_create_window_checked(
          connection, 0, child, surface, 70, 70, 80, 80, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0,
          nullptr
      ),
      "Creating private native child"
  );
  checked(connection, xcb_map_window_checked(connection, child), "Mapping private native child");
  check(!point(200, 220).point, "Unsupported native child inside Viewer acquired validation");
  checked(connection, xcb_destroy_window_checked(connection, child), "Removing private native child");
  observer.invalidate();
  observe(connection, observer, surface);
  check(
      !endpoint.validatePoint({++request, {8, request, 1}, -1, 220, coordinateDeadline()}),
      "Negative native root coordinate was accepted"
  );
  check(!endpoint.validatePoint({++request, {}, 200, 220, coordinateDeadline()}), "Missing capture was accepted");
  const auto expiring = ++request;
  check(
      endpoint.validatePoint(
          {expiring, {8, expiring, 1}, 200, 220, X11CoordinateClock::now() + std::chrono::milliseconds(1)}
      ),
      "Deadline fixture did not start"
  );
  QThread::msleep(3);
  const auto expired = coordinateUpdate(connection, observer, endpoint);
  check(!expired.point && !expired.error.empty(), "Expired native point produced a receipt");
  auto final = point(200, 220);
  check(final.point && final.point->live(), "Fresh point failed after expiry: " + final.error);
  endpoint.close();
  check(!final.point->live(), "Endpoint close retained point evidence");
  checked(connection, xcb_destroy_window_checked(connection, surface), "Removing point fixture");
  std::cout
      << "PASS exact stamped native point, occlusion, input shapes, native-child denial, deadline and revocation\n";
}

QJsonObject metadata(std::uint64_t geometry = 1)
{
  return {
      {"title", "Private Xvfb viewport"},
      {"geometry", static_cast<qint64>(geometry)},
      {"width", 640},
      {"height", 480},
      {"pixelWidth", 64},
      {"pixelHeight", 48},
      {"codec", "h264"}
  };
}
QByteArray frame()
{
  return QByteArray::fromBase64(
      "AAAAAWdCwAraEewEQAAAAwBAAAAPI8SJqAAAAAFozg/IAAAAAWWIhDoRigACGPHAAED2OAAIeUnJyddddddddeA="
  );
}
void viewerObservation()
{
  auto *native = qApp->nativeInterface<QNativeInterface::QX11Application>();
  check(native && native->connection(), "Qt fixture must use its real xcb connection");
  auto *connection = native->connection();
  selectFixtureMaster(connection);
  Viewer viewer(42, 3, metadata());
  viewer.resize(400, 400);
  viewer.move(100, 120);
  std::uint64_t request = 0;
  viewer.onSend = [&](MessageType type, const QJsonObject &message, std::uint64_t, std::uint64_t) {
    if (type == MessageType::Input && message.value("kind") == "focus") {
      request = static_cast<std::uint64_t>(message.value("request").toInteger());
      viewer.focusAcknowledged(7, request);
    }
    return true;
  };
  check(!viewer.viewportObservation(), "Unpainted viewer fabricated a viewport observation");
  viewer.show();
  checked(
      connection,
      xcb_set_input_focus_checked(
          connection, XCB_INPUT_FOCUS_POINTER_ROOT, static_cast<std::uint32_t>(viewer.winId()), XCB_CURRENT_TIME
      ),
      "Focusing private viewer"
  );
  viewer.committed();
  viewer.receiveFrame(frame(), 1);
  check(
      waitUntil([&] { return viewer.viewportObservation().has_value(); }),
      "Real viewer observation unavailable: " + viewer.viewportUnavailableReason().toStdString()
  );
  auto observed = *viewer.viewportObservation();
  const auto &state = observed.snapshot();
  check(
      state.native.snapshot().surface == viewer.winId() && state.session == 42 && state.epoch == 3 &&
          state.sourceGeometry == 1 && state.focusRequest == request && state.focusRevision == 7 && state.output,
      "Viewer observation lost its source/focus/native bindings"
  );
  check(
      state.clientLogical == QSizeF(400, 400) && state.imagePixels == QSize(64, 48) &&
          state.videoLogical == QRectF(0, 50, 400, 300) && state.sourceLogical == QSizeF(640, 480),
      "Viewer did not retain the actual last-painted letterbox rectangle"
  );
  check(
      std::abs(state.devicePixelRatio - viewer.devicePixelRatioF()) < 1e-9 &&
          std::abs(state.native.snapshot().client.width - 400 * state.devicePixelRatio) < 1e-9,
      "Fractional DPR does not match actual native client extent"
  );
  const auto nativeCenterX = state.native.snapshot().client.x + 200 * state.devicePixelRatio;
  const auto localX = (nativeCenterX - state.native.snapshot().client.x) / state.devicePixelRatio;
  const auto mapped = viewer.mapToSource(QPointF(localX, 200));
  check(
      mapped && std::abs(mapped->x() - 320) < 1e-9 && std::abs(mapped->y() - 240) < 1e-9 &&
          !viewer.mapToSource(QPointF(200, 10)),
      "Native pixel-to-painted source mapping/letterbox rejection failed"
  );
  Connection coreConnection;
  selectFixtureMaster(coreConnection);
  X11DisplayObserver core(coreConnection, 0);
  observe(coreConnection, core);
  X11CoordinateEndpoint publisher(core);
  check(publisher.create(100, coordinateDeadline()), "Actual Viewer witness fixture could not publish");
  const auto published = coordinateUpdate(coreConnection, core, publisher);
  check(published.witness && published.witness->live(), "Actual Viewer witness fixture failed: " + published.error);
  check(
      viewer.readX11Witness(published.witness->snapshot().locator, coordinateDeadline()),
      "Actual Qt-connection witness read refused"
  );
  auto takeViewerUpdate = [&] {
    std::optional<ViewerCoordinateUpdate> result;
    check(
        waitUntil([&] {
          if (!result)
            result = viewer.takeX11CoordinateUpdate();
          return result.has_value();
        }),
        "Actual Viewer coordinate request did not settle"
    );
    return std::move(*result);
  };
  const auto read = takeViewerUpdate();
  check(
      read.native.witness && read.viewport && read.viewport->live() &&
          read.native.witness->snapshot().secret == published.witness->snapshot().secret,
      "Actual Qt connection failed native namespace readback: " + read.native.error
  );
  viewer.onKeyboardRouteSend = [](const KeyboardRouteControl &) { return true; };
  KeyboardRouteBinding fence{{42, 3}, 1, request, 7, 1, ForwardingScope::All};
  std::uint64_t pointRequest = 100;
  auto routedPoint = [&](double localY = 200) {
    const auto current = viewer.viewportObservation();
    check(bool(current), "Actual Viewer lost its painted/native viewport before point request");
    const auto &painted = current->snapshot();
    return X11PointRequest{
        ++pointRequest,
        {9, pointRequest, 1},
        static_cast<std::int32_t>(painted.native.snapshot().client.x + 200 * painted.devicePixelRatio),
        static_cast<std::int32_t>(painted.native.snapshot().client.y + localY * painted.devicePixelRatio),
        coordinateDeadline()
    };
  };
  check(!viewer.validateX11Point(routedPoint(), fence), "Unfenced Viewer accepted native point validation");
  viewer.keyboardRoute({fence});
  check(viewer.allForwardingFenced(fence), "Actual Viewer did not install exact All fence");
  auto wrongFence = fence;
  ++wrongFence.generation;
  check(!viewer.validateX11Point(routedPoint(), wrongFence), "Wrong full fence accepted native point validation");
  const auto exactPoint = routedPoint();
  check(viewer.validateX11Point(exactPoint, fence), "Fenced native point request did not start");
  const auto actualPoint = takeViewerUpdate();
  check(
      actualPoint.native.point && actualPoint.native.point->live() && actualPoint.viewport &&
          actualPoint.viewport->live() && actualPoint.source &&
          actualPoint.native.point->snapshot().capture == exactPoint.capture &&
          std::abs(actualPoint.source->x() - 320) < 1e-9 && std::abs(actualPoint.source->y() - 240) < 1e-9,
      "Actual native point did not retain exact capture and painted fractional mapping: " + actualPoint.native.error
  );
  bool revokedBeforeResumeAck = false;
  viewer.onKeyboardRouteSend = [&](const KeyboardRouteControl &control) {
    if (control.mode == KeyboardRouteMode::Viewer)
      revokedBeforeResumeAck = !actualPoint.native.point->live();
    return true;
  };
  viewer.keyboardRoute({fence, KeyboardRouteAction::Request, KeyboardRouteMode::Viewer});
  check(
      revokedBeforeResumeAck && !actualPoint.native.point->live(),
      "Native point receipt survived forwarding resume acknowledgement"
  );
  check(!viewer.validateX11Point(routedPoint(), fence), "Resumed Viewer accepted stale fence");
  ++fence.generation;
  viewer.keyboardRoute({fence});
  check(viewer.validateX11Point(routedPoint(10), fence), "Letterbox native point fixture did not start");
  const auto letterbox = takeViewerUpdate();
  check(
      !letterbox.native.point && !letterbox.source && !letterbox.native.error.empty(),
      "Native point inside client letterbox acquired source mapping"
  );
  check(viewer.validateX11Point(routedPoint(), fence), "Fresh native point failed after denied letterbox");
  const auto beforeMove = takeViewerUpdate();
  check(beforeMove.native.point && beforeMove.native.point->live(), "Fresh native point unavailable");
  viewer.move(180, 180);
  check(!beforeMove.native.point->live(), "Qt move did not revoke retained native point receipt");
  check(!observed.live(), "Qt move did not immediately revoke retained viewport");
  check(
      waitUntil([&] { return viewer.viewportObservation().has_value(); }),
      "Moved viewer failed to resample native origin"
  );
  observed = *viewer.viewportObservation();
  viewer.focusAcknowledged(0, request);
  check(!observed.live() && !viewer.viewportObservation(), "Source focus revocation retained viewport evidence");
  viewer.hide();
  check(!viewer.viewportObservation(), "Hidden viewer retained viewport evidence");
  viewer.disconnected();
  check(!viewer.viewportObservation(), "Disconnected viewer retained viewport evidence");
  std::cout
      << "PASS native Qt connection witness, exact All point fence, fractional painted mapping, resume and lifecycle\n";
}
} // namespace

int main(int argc, char **argv)
{
  if (qEnvironmentVariable("DESKFLOW_X11_TEST_PRIVATE") != "1") {
    std::cerr << "Run this test through X11ViewportTests.py; it must never use the user's display\n";
    return 78;
  }
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    const QStringList arguments = app.arguments();
    QString other;
    for (const auto &argument : arguments)
      if (argument.startsWith("--other-display="))
        other = argument.mid(16);
    check(!other.isEmpty(), "Missing second private Xvfb display");
    nativeObservations(other.toLocal8Bit().constData());
    namespaceWitnesses(other.toLocal8Bit().constData());
    nativePointValidation();
    viewerObservation();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
