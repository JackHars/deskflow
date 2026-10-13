/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "X11CoordinateEndpoint.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <xcb/xcbext.h>
#ifdef __linux__
#include <sys/random.h>
#endif

namespace deskflow::windowshare {
namespace {
using Clock = X11CoordinateClock;
constexpr auto lifetime = std::chrono::milliseconds(250);
constexpr char propertyName[] = "_DESKFLOW_COORDINATE_WITNESS_V1";
constexpr char typeName[] = "_DESKFLOW_COORDINATE_WITNESS_BYTES_V1";
void require(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
bool validLocator(const X11WitnessLocator &value)
{
  return value.request && value.window && value.property && value.type && value.property != value.type;
}
void randomSecret(std::array<std::uint8_t, 32> &secret)
{
#ifdef __linux__
  require(
      getrandom(secret.data(), secret.size(), GRND_NONBLOCK) == static_cast<ssize_t>(secret.size()),
      "X11 witness entropy unavailable"
  );
#else
  (void)secret;
  throw std::runtime_error("X11 namespace witnesses require Linux entropy");
#endif
}
} // namespace

struct X11NamespaceWitness::State
{
  X11WitnessSnapshot snapshot;
  std::shared_ptr<std::atomic_bool> revoked;
  Clock::time_point deadline;
  const void *publisher = nullptr;
};
bool X11NamespaceWitness::live() const noexcept
{
  return m_state && m_state->revoked && !m_state->revoked->load(std::memory_order_acquire) &&
         m_state->snapshot.native.live() && Clock::now() < m_state->deadline;
}
const X11WitnessSnapshot &X11NamespaceWitness::snapshot() const noexcept
{
  static const X11WitnessSnapshot empty;
  return m_state ? m_state->snapshot : empty;
}
struct X11PointValidation::State
{
  X11PointSnapshot snapshot;
  std::shared_ptr<std::atomic_bool> revoked;
  Clock::time_point deadline;
};
bool X11PointValidation::live() const noexcept
{
  return m_state && m_state->revoked && !m_state->revoked->load(std::memory_order_acquire) &&
         m_state->snapshot.native.live() && Clock::now() < m_state->deadline;
}
const X11PointSnapshot &X11PointValidation::snapshot() const noexcept
{
  static const X11PointSnapshot empty;
  return m_state ? m_state->snapshot : empty;
}

struct X11CoordinateEndpoint::Impl
{
  enum class Operation
  {
    None,
    Create,
    Read,
    Confirm,
    Point
  } operation = Operation::None;
  enum class Phase
  {
    Atoms,
    Window,
    Property,
    Point
  } phase = Phase::Atoms;
  enum QueryKind
  {
    PropertyAtom,
    TypeAtom,
    Geometry,
    Attributes,
    Property,
    Translate
  };
  struct Query
  {
    unsigned sequence;
    std::shared_ptr<void> reply;
  };
  X11DisplayObserver &observer;
  xcb_connection_t *connection;
  bool closed = false, ownWriteSeen = false;
  std::uint64_t highWater = 0, namespaceRequest = 0, pointRequest = 0, currentRequest = 0;
  std::uint32_t window = 0, windowRoot = 0, property = 0, type = 0, ownWriteSequence = 0;
  X11WitnessLocator locator;
  std::array<std::uint8_t, 32> expected{};
  X11DisplayObservation native;
  Clock::time_point deadline;
  std::shared_ptr<std::atomic_bool> namespaceRevoked = std::make_shared<std::atomic_bool>(true);
  std::shared_ptr<std::atomic_bool> pointRevoked = std::make_shared<std::atomic_bool>(true);
  std::shared_ptr<std::atomic_bool> pendingRevoked;
  std::optional<Update> update;
  std::map<QueryKind, Query> queries;
  std::vector<xcb_void_cookie_t> checked;
  X11PointRequest point;
  std::vector<std::uint32_t> traversal, firstTraversal;
  std::int16_t firstX = 0, firstY = 0;

  Impl(X11DisplayObserver &value, xcb_connection_t *borrowed) : observer(value), connection(borrowed)
  {
    closed = !connection;
  }
  void add(QueryKind key, unsigned sequence)
  {
    queries.emplace(key, Query{sequence, {}});
  }
  template <class T> const T &get(QueryKind key) const
  {
    return *static_cast<const T *>(queries.at(key).reply.get());
  }
  void discard()
  {
    if (connection) {
      for (const auto &[key, query] : queries) {
        (void)key;
        if (!query.reply)
          xcb_discard_reply(connection, query.sequence);
      }
      for (auto cookie : checked)
        xcb_discard_reply(connection, cookie.sequence);
    }
    checked.clear();
    queries.clear();
  }
  void revokeNamespace()
  {
    namespaceRevoked->store(true, std::memory_order_release);
    pointRevoked->store(true, std::memory_order_release);
  }
  void fail(const char *reason)
  {
    if (pendingRevoked)
      pendingRevoked->store(true, std::memory_order_release);
    discard();
    if (operation != Operation::None)
      update = Update{currentRequest, reason, {}, {}};
    operation = Operation::None;
    pendingRevoked.reset();
  }
  bool admit(std::uint64_t request, Clock::time_point inherited, Operation next, bool confirming = false)
  {
    if (closed || !connection || xcb_connection_has_error(connection) || operation != Operation::None || update ||
        !request || (!confirming && request <= highWater) || inherited <= Clock::now())
      return false;
    const auto observed = observer.observe();
    if (!observed || !observed->live())
      return false;
    native = *observed;
    deadline = std::min(inherited, Clock::now() + lifetime);
    currentRequest = request;
    if (!confirming)
      highWater = request;
    operation = next;
    if (next == Operation::Point) {
      pointRevoked->store(true, std::memory_order_release);
      pointRevoked = std::make_shared<std::atomic_bool>(false);
      pointRequest = request;
      pendingRevoked = pointRevoked;
    } else {
      if (!confirming) {
        revokeNamespace();
        namespaceRevoked = std::make_shared<std::atomic_bool>(false);
      }
      namespaceRequest = request;
      pendingRevoked = namespaceRevoked;
    }
    return true;
  }
  void atoms()
  {
    const bool onlyExisting = operation != Operation::Create;
    add(PropertyAtom, xcb_intern_atom(connection, onlyExisting, sizeof(propertyName) - 1, propertyName).sequence);
    add(TypeAtom, xcb_intern_atom(connection, onlyExisting, sizeof(typeName) - 1, typeName).sequence);
    phase = Phase::Atoms;
    xcb_flush(connection);
  }
  void readProperty()
  {
    queries.clear();
    add(Geometry, xcb_get_geometry(connection, locator.window).sequence);
    add(Attributes, xcb_get_window_attributes(connection, locator.window).sequence);
    add(Property, xcb_get_property(connection, false, locator.window, locator.property, locator.type, 0, 8).sequence);
    phase = Phase::Property;
  }
  void publishWitness()
  {
    X11NamespaceWitness result;
    result.m_state = std::make_shared<X11NamespaceWitness::State>(X11NamespaceWitness::State{
        {locator, expected, native}, namespaceRevoked, deadline, operation == Operation::Read ? nullptr : this
    });
    update = Update{currentRequest, {}, std::move(result), {}};
    queries.clear();
    operation = Operation::None;
    pendingRevoked.reset();
  }
  void pointStep(std::uint32_t destination)
  {
    require(
        traversal.size() < 32 && std::find(traversal.begin(), traversal.end(), destination) == traversal.end(),
        "X11 input hierarchy is cyclic or exceeds the bound"
    );
    traversal.push_back(destination);
    queries.clear();
    add(Translate, xcb_translate_coordinates(
                       connection, native.snapshot().root, destination, static_cast<std::int16_t>(point.rootX),
                       static_cast<std::int16_t>(point.rootY)
                   )
                       .sequence);
    phase = Phase::Point;
  }
  void advance()
  {
    if (phase == Phase::Atoms) {
      property = get<xcb_intern_atom_reply_t>(PropertyAtom).atom;
      type = get<xcb_intern_atom_reply_t>(TypeAtom).atom;
      require(property && type && property != type, "X11 witness protocol atoms unavailable");
      queries.clear();
      if (operation == Operation::Create) {
        const auto root = native.snapshot().root;
        if (window && windowRoot != root) {
          xcb_discard_reply(connection, xcb_destroy_window_checked(connection, window).sequence);
          window = 0;
        }
        if (!window) {
          window = xcb_generate_id(connection);
          require(window && window != std::numeric_limits<std::uint32_t>::max(), "X11 witness XID unavailable");
          windowRoot = root;
          const std::uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
          checked.push_back(xcb_create_window_checked(
              connection, 0, window, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
              XCB_CW_EVENT_MASK, &mask
          ));
        }
        locator = {currentRequest, window, property, type};
        const auto changed = xcb_change_property_checked(
            connection, XCB_PROP_MODE_REPLACE, window, property, type, 8, expected.size(), expected.data()
        );
        checked.push_back(changed);
        ownWriteSequence = changed.sequence;
        ownWriteSeen = false;
        // Geometry-only reply orders checked writes without silently assigning
        // an XI master as a core GetInputFocus/QueryPointer request can do.
        add(Geometry, xcb_get_geometry(connection, windowRoot).sequence);
        phase = Phase::Window;
      } else {
        require(locator.property == property && locator.type == type, "X11 witness atom namespace mismatch");
        readProperty();
      }
    } else if (phase == Phase::Window) {
      for (const auto cookie : checked) {
        std::unique_ptr<xcb_generic_error_t, decltype(&free)> error(xcb_request_check(connection, cookie), &free);
        require(!error, "X11 witness window/property write failed");
      }
      checked.clear();
      readProperty();
    } else if (phase == Phase::Property) {
      const auto &geometry = get<xcb_get_geometry_reply_t>(Geometry);
      const auto &attributes = get<xcb_get_window_attributes_reply_t>(Attributes);
      const auto &value = get<xcb_get_property_reply_t>(Property);
      require(
          geometry.root == native.snapshot().root && geometry.depth == 0 && geometry.width == 1 &&
              geometry.height == 1 && attributes._class == XCB_WINDOW_CLASS_INPUT_ONLY &&
              attributes.map_state == XCB_MAP_STATE_UNMAPPED,
          "X11 witness is not the expected unmapped native-root resource"
      );
      require(
          value.type == locator.type && value.format == 8 && value.value_len == expected.size() &&
              value.bytes_after == 0 && value.length == 8 && xcb_get_property_value_length(&value) == 32,
          "X11 witness property type/format/length mismatch"
      );
      const auto *bytes = static_cast<const std::uint8_t *>(xcb_get_property_value(&value));
      if (operation == Operation::Read)
        std::copy_n(bytes, expected.size(), expected.begin());
      else
        require(std::equal(expected.begin(), expected.end(), bytes), "X11 witness was overwritten");
      // Observe our own write through the owner's event delivery before
      // publishing; otherwise its delayed PropertyNotify could look like a
      // subsequent replacement. We never consume that owner's event queue.
      if (operation == Operation::Create && !ownWriteSeen)
        return;
      publishWitness();
    } else {
      const auto &translated = get<xcb_translate_coordinates_reply_t>(Translate);
      require(translated.same_screen, "X11 routed point crossed native roots");
      const auto destination = traversal.back();
      if (destination != native.snapshot().surface) {
        require(translated.child != 0, "X11 routed point does not hit the retained Viewer surface");
        pointStep(translated.child);
        return;
      }
      require(!translated.child, "X11 routed point hits an unsupported native child of the Viewer");
      require(
          translated.dst_x >= 0 && translated.dst_y >= 0 &&
              static_cast<std::uint32_t>(translated.dst_x) < native.snapshot().client.width &&
              static_cast<std::uint32_t>(translated.dst_y) < native.snapshot().client.height,
          "X11 routed point hits a border or lies outside the client"
      );
      if (firstTraversal.empty()) {
        firstTraversal = traversal;
        firstX = translated.dst_x;
        firstY = translated.dst_y;
        traversal.clear();
        pointStep(native.snapshot().root);
        return;
      }
      require(
          firstTraversal == traversal && firstX == translated.dst_x && firstY == translated.dst_y,
          "X11 input hierarchy or transform changed between point reads"
      );
      X11PointValidation result;
      result.m_state = std::make_shared<X11PointValidation::State>(X11PointValidation::State{
          {point.request, point.capture, point.rootX, point.rootY, firstX, firstY, native}, pointRevoked, deadline
      });
      update = Update{currentRequest, {}, {}, std::move(result)};
      queries.clear();
      operation = Operation::None;
      pendingRevoked.reset();
    }
  }
  void poll()
  {
    if (closed || operation == Operation::None)
      return;
    require(connection && !xcb_connection_has_error(connection), "X11 coordinate connection closed");
    require(
        Clock::now() < deadline && native.live() && pendingRevoked && !pendingRevoked->load(),
        "X11 coordinate request expired or invalidated"
    );
    bool complete = true;
    for (auto &[key, query] : queries) {
      (void)key;
      if (query.reply)
        continue;
      void *value = nullptr;
      xcb_generic_error_t *error = nullptr;
      if (!xcb_poll_for_reply(connection, query.sequence, &value, &error)) {
        complete = false;
        continue;
      }
      std::unique_ptr<xcb_generic_error_t, decltype(&free)> failure(error, &free);
      query.reply = std::shared_ptr<void>(value, &free);
      require(!failure && value, "X11 coordinate resource query failed");
      const auto *header = static_cast<xcb_generic_reply_t *>(value);
      const std::uint32_t expectedLength = key == Attributes ? 3 : key == Property ? 8 : 0;
      require(
          header->response_type == 1 && header->length == expectedLength,
          "X11 coordinate reply has an unexpected size or type"
      );
    }
    if (complete)
      advance();
    xcb_flush(connection);
  }
};

X11CoordinateEndpoint::X11CoordinateEndpoint(X11DisplayObserver &observer)
    : m_impl(std::make_unique<Impl>(observer, observer.borrowedConnection()))
{
}
X11CoordinateEndpoint::~X11CoordinateEndpoint()
{
  close();
}
bool X11CoordinateEndpoint::create(std::uint64_t request, Clock::time_point deadline)
{
  if (!m_impl->admit(request, deadline, Impl::Operation::Create))
    return false;
  try {
    randomSecret(m_impl->expected);
    m_impl->atoms();
  } catch (const std::exception &error) {
    m_impl->fail(error.what());
  }
  return true;
}
bool X11CoordinateEndpoint::read(X11WitnessLocator locator, Clock::time_point deadline)
{
  if (!validLocator(locator) || !m_impl->admit(locator.request, deadline, Impl::Operation::Read))
    return false;
  m_impl->locator = locator;
  m_impl->atoms();
  return true;
}
bool X11CoordinateEndpoint::confirm(const X11NamespaceWitness &published)
{
  if (!published.live() || published.m_state->publisher != m_impl.get() ||
      published.snapshot().locator.request != m_impl->namespaceRequest ||
      published.m_state->revoked != m_impl->namespaceRevoked ||
      !m_impl->admit(published.snapshot().locator.request, published.m_state->deadline, Impl::Operation::Confirm, true))
    return false;
  m_impl->locator = published.snapshot().locator;
  m_impl->expected = published.snapshot().secret;
  m_impl->atoms();
  return true;
}
bool X11CoordinateEndpoint::validatePoint(X11PointRequest request)
{
  const auto within = [](std::int32_t n) { return n >= 0 && n <= std::numeric_limits<std::int16_t>::max(); };
  if (!request.capture || !request.capture.part || request.capture.part > maximumOriginalInputParts ||
      !within(request.rootX) || !within(request.rootY))
    return false;
  const auto native = m_impl->observer.observe();
  if (!native || !native->snapshot().surface || !native->snapshot().viewable ||
      !native->snapshot().focusWithinSurface ||
      request.rootX >= static_cast<std::int64_t>(native->snapshot().desktop.width) ||
      request.rootY >= static_cast<std::int64_t>(native->snapshot().desktop.height) ||
      !m_impl->admit(request.request, request.deadline, Impl::Operation::Point))
    return false;
  m_impl->point = request;
  m_impl->traversal.clear();
  m_impl->firstTraversal.clear();
  m_impl->pointStep(m_impl->native.snapshot().root);
  xcb_flush(m_impl->connection);
  return true;
}
void X11CoordinateEndpoint::poll()
{
  try {
    m_impl->poll();
  } catch (const std::exception &error) {
    m_impl->fail(error.what());
  }
}
bool X11CoordinateEndpoint::pending() const noexcept
{
  return m_impl->operation != Impl::Operation::None;
}
std::optional<X11CoordinateEndpoint::Update> X11CoordinateEndpoint::takeUpdate()
{
  return std::exchange(m_impl->update, {});
}
bool X11CoordinateEndpoint::cancel(std::uint64_t request)
{
  if (!request || (request != m_impl->namespaceRequest && request != m_impl->pointRequest))
    return false;
  if (request == m_impl->namespaceRequest) {
    m_impl->revokeNamespace();
    m_impl->namespaceRequest = 0;
  }
  if (request == m_impl->pointRequest) {
    m_impl->pointRevoked->store(true);
    m_impl->pointRequest = 0;
  }
  if (m_impl->currentRequest == request && pending()) {
    m_impl->fail("X11 coordinate request canceled");
  }
  if (m_impl->update && m_impl->update->request == request)
    m_impl->update.reset();
  return true;
}
void X11CoordinateEndpoint::invalidate()
{
  m_impl->revokeNamespace();
  m_impl->fail("X11 coordinate endpoint invalidated");
}
void X11CoordinateEndpoint::close()
{
  if (!m_impl || m_impl->closed)
    return;
  invalidate();
  m_impl->closed = true;
  if (m_impl->window && m_impl->connection && !xcb_connection_has_error(m_impl->connection)) {
    xcb_discard_reply(m_impl->connection, xcb_destroy_window_checked(m_impl->connection, m_impl->window).sequence);
    xcb_flush(m_impl->connection);
  }
  m_impl->window = 0;
}
void X11CoordinateEndpoint::propertyChanged(std::uint32_t window, std::uint32_t atom, std::uint32_t sequence)
{
  if (window != m_impl->window || atom != m_impl->property)
    return;
  if (m_impl->operation == Impl::Operation::Create && !m_impl->ownWriteSeen && sequence == m_impl->ownWriteSequence)
    m_impl->ownWriteSeen = true;
  else
    invalidate();
}
void X11CoordinateEndpoint::windowDestroyed(std::uint32_t window)
{
  if (window == m_impl->window) {
    invalidate();
    m_impl->window = 0;
  }
}
void X11CoordinateEndpoint::handleEvent(const xcb_generic_event_t &event)
{
  if ((event.response_type & 0x7f) == XCB_PROPERTY_NOTIFY) {
    const auto &property = reinterpret_cast<const xcb_property_notify_event_t &>(event);
    propertyChanged(property.window, property.atom, event.full_sequence);
  } else if ((event.response_type & 0x7f) == XCB_DESTROY_NOTIFY)
    windowDestroyed(reinterpret_cast<const xcb_destroy_notify_event_t &>(event).window);
}
} // namespace deskflow::windowshare
