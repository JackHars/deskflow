/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "X11DisplayObserver.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <xcb/randr.h>
#include <xcb/xcbext.h>
#include <xcb/xinput.h>

namespace deskflow::windowshare {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto freshness = std::chrono::milliseconds(250);
constexpr std::size_t maxReply = 64 * 1024, maxTopology = 64 * 1024;
constexpr unsigned maxOutputs = 32, maxCrtcs = 32, maxModes = 256, maxProviders = 16;
std::atomic_uint64_t nextIncarnation{1};

std::uint64_t incarnation()
{
  auto value = nextIncarnation.load();
  while (value != std::numeric_limits<std::uint64_t>::max())
    if (nextIncarnation.compare_exchange_weak(value, value + 1))
      return value;
  return 0;
}

[[maybe_unused]] std::optional<std::string> smallFile(const std::string &path)
{
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return {};
  std::array<char, 4096> buffer{};
  const auto count = read(fd, buffer.data(), buffer.size());
  close(fd);
  if (count <= 0 || count == static_cast<ssize_t>(buffer.size()))
    return {};
  return std::string(buffer.data(), static_cast<std::size_t>(count));
}

std::optional<X11SocketPeer> socketPeer(xcb_connection_t *connection)
{
#ifdef __linux__
  const int fd = xcb_get_file_descriptor(connection);
  sockaddr_storage address{};
  socklen_t length = sizeof(address);
  if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0 || address.ss_family != AF_UNIX)
    return {};
  ucred credentials{};
  length = sizeof(credentials);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 || length != sizeof(credentials) ||
      credentials.pid <= 0)
    return {};
  const auto boot = smallFile("/proc/sys/kernel/random/boot_id");
  const auto stat = smallFile("/proc/" + std::to_string(credentials.pid) + "/stat");
  if (!boot || !stat || boot->size() != 37 || boot->back() != '\n')
    return {};
  X11SocketPeer result;
  std::size_t byte = 0;
  for (std::size_t i = 0; i < 36;) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if ((*boot)[i++] != '-')
        return {};
      continue;
    }
    unsigned value = 0;
    const auto parsed = std::from_chars(boot->data() + i, boot->data() + i + 2, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != boot->data() + i + 2 || byte == result.bootId.size())
      return {};
    result.bootId[byte++] = static_cast<std::uint8_t>(value);
    i += 2;
  }
  const auto endName = stat->rfind(')');
  if (endName == std::string::npos || endName + 2 >= stat->size())
    return {};
  std::string_view fields(*stat);
  fields.remove_prefix(endName + 2);
  // /proc/PID/stat field 22, after the parenthesized comm and state (field 3).
  for (int field = 3; field < 22; ++field) {
    const auto space = fields.find(' ');
    if (space == std::string_view::npos)
      return {};
    fields.remove_prefix(space + 1);
  }
  const auto space = fields.find(' ');
  if (space == std::string_view::npos)
    return {};
  const auto parsed = std::from_chars(fields.data(), fields.data() + space, result.processStartTicks);
  if (parsed.ec != std::errc{} || parsed.ptr != fields.data() + space || result.processStartTicks == 0)
    return {};
  result.pid = static_cast<std::uint32_t>(credentials.pid);
  result.uid = credentials.uid;
  result.gid = credentials.gid;
  return result;
#else
  (void)connection;
  return {};
#endif
}

void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

struct Encoding
{
  std::vector<std::uint8_t> bytes;
  void number(std::uint32_t value)
  {
    require(bytes.size() <= maxTopology - 4, "X11 topology exceeds the bounded encoding");
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  void text(const void *data, std::size_t size)
  {
    require(size <= 4096 && bytes.size() + size + 4 <= maxTopology, "X11 topology text exceeds the bound");
    number(static_cast<std::uint32_t>(size));
    const auto *begin = static_cast<const std::uint8_t *>(data);
    bytes.insert(bytes.end(), begin, begin + size);
  }
  void ids(const std::uint32_t *data, std::size_t count)
  {
    require(count <= maxModes, "X11 topology resource list exceeds the bound");
    std::vector<std::uint32_t> sorted(data, data + count);
    std::sort(sorted.begin(), sorted.end());
    require(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "Duplicate X11 resource identity");
    number(static_cast<std::uint32_t>(count));
    for (auto id : sorted)
      number(id);
  }
};

template <class T> const T &reply(const std::shared_ptr<void> &value)
{
  require(bool(value), "Missing X11 reply");
  const auto *header = static_cast<const xcb_generic_reply_t *>(value.get());
  const std::size_t size = 32 + std::size_t(header->length) * 4;
  require(size >= sizeof(T) && size <= maxReply, "X11 reply size exceeds the bound");
  return *static_cast<const T *>(value.get());
}

template <class T> void range(const T &value, const void *data, std::size_t size)
{
  const auto begin = reinterpret_cast<std::uintptr_t>(&value);
  const auto item = reinterpret_cast<std::uintptr_t>(data);
  const auto length = 32 + std::size_t(value.length) * 4;
  require(item >= begin && item - begin <= length && size <= length - (item - begin), "Malformed X11 reply range");
}

bool equalSnapshot(const X11DisplaySnapshot &a, const X11DisplaySnapshot &b)
{
  return a.socketPeer == b.socketPeer && a.connectionIncarnation == b.connectionIncarnation && a.root == b.root &&
         a.screen == b.screen && a.clientPointer == b.clientPointer && a.clientKeyboard == b.clientKeyboard &&
         a.desktop == b.desktop && a.primaryOutput == b.primaryOutput && a.outputs == b.outputs &&
         a.topology == b.topology && a.surface == b.surface && a.inputFocus == b.inputFocus && a.client == b.client &&
         a.viewable == b.viewable && a.focusWithinSurface == b.focusWithinSurface;
}
} // namespace

struct X11DisplayObservation::State
{
  X11DisplaySnapshot snapshot;
  Clock::time_point expires;
  std::shared_ptr<std::atomic_bool> revoked;
};

const X11DisplaySnapshot &X11DisplayObservation::snapshot() const noexcept
{
  static const X11DisplaySnapshot empty;
  return m_state ? m_state->snapshot : empty;
}
bool X11DisplayObservation::live() const noexcept
{
  return m_state && m_state->revoked && !m_state->revoked->load(std::memory_order_acquire) &&
         Clock::now() < m_state->expires;
}

struct X11DisplayObserver::Impl
{
  enum class Phase
  {
    Idle,
    Extensions,
    Versions,
    Watcher,
    Base,
    Details,
    Ancestors
  };
  struct Query
  {
    unsigned sequence;
    std::shared_ptr<void> result;
  };
  xcb_connection_t *connection;
  int requestedScreen;
  Phase phase = Phase::Idle;
  std::map<std::pair<int, std::uint32_t>, Query> queries;
  std::vector<xcb_void_cookie_t> checked;
  std::optional<X11DisplayObservation> current;
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  std::optional<X11DisplaySnapshot> first;
  X11DisplaySnapshot sample;
  X11SocketPeer peer;
  Clock::time_point started;
  std::uint64_t connectionId = incarnation(), revision = 1;
  std::uint32_t root = 0, screen = 0, watcher = 0, watcherRoot = 0, surface = 0, configTimestamp = 0;
  std::uint8_t randrEvent = 0, xiOpcode = 0;
  std::uint32_t randrMinor = 0;
  unsigned ancestorDepth = 0;
  bool initialized = false, closed = false;
  std::string reason = "No native X11 observation has completed";
  enum Key
  {
    ExtRandr,
    ExtXi,
    ExtWayland,
    Geometry,
    Barrier,
    RandrVersion,
    Resources,
    Primary,
    Focus,
    Pointer,
    Devices,
    Attributes,
    Translate,
    Providers,
    Monitors,
    Output,
    Crtc,
    Transform,
    Panning,
    Provider,
    XiFocus,
    Tree
  };

  Impl(xcb_connection_t *value, int index) : connection(value), requestedScreen(index)
  {
    if (!connection || xcb_connection_has_error(connection) || connectionId == 0) {
      closed = true;
      reason = "Missing or failed native X11 connection";
    } else if (const auto identity = socketPeer(connection)) {
      peer = *identity;
    } else {
      closed = true;
      reason = "X11 observation requires Linux Unix-socket peer credentials and readable /proc birth identity";
    }
  }

  void add(Key key, unsigned sequence, std::uint32_t id = 0)
  {
    queries.emplace(std::pair{int(key), id}, Query{sequence, {}});
  }
  template <class T> const T &get(Key key, std::uint32_t id = 0) const
  {
    return reply<T>(queries.at({int(key), id}).result);
  }
  void discard()
  {
    if (connection)
      for (const auto &[key, query] : queries) {
        (void)key;
        if (!query.result)
          xcb_discard_reply(connection, query.sequence);
      }
    queries.clear();
    // Checked void requests preceding a barrier can be discarded without
    // waiting. They only create/select this observer's private watcher.
    if (connection)
      for (auto cookie : checked)
        xcb_discard_reply(connection, cookie.sequence);
    checked.clear();
  }
  void revoke()
  {
    revoked->store(true, std::memory_order_release);
    current.reset();
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
      closed = true;
      reason = "X11 observation revision exhausted";
    } else {
      ++revision;
      revoked = std::make_shared<std::atomic_bool>(false);
    }
  }
  void fail(std::string message)
  {
    discard();
    first.reset();
    phase = Phase::Idle;
    revoke();
    reason = std::move(message);
  }
  void chooseRoot(std::uint32_t observedRoot)
  {
    auto roots = xcb_setup_roots_iterator(xcb_get_setup(connection));
    int index = 0;
    for (; roots.rem; xcb_screen_next(&roots), ++index)
      if ((observedRoot && roots.data->root == observedRoot) || (!observedRoot && index == requestedScreen)) {
        require(requestedScreen < 0 || index == requestedScreen, "X11 surface is on a different native screen");
        root = roots.data->root;
        screen = static_cast<std::uint32_t>(index);
        return;
      }
    throw std::runtime_error("Cannot resolve the surface's actual X11 root/screen");
  }
  void refresh(std::uint32_t value)
  {
    if (closed || phase != Phase::Idle)
      return;
    if (surface != value) {
      revoke();
      surface = value;
    }
    started = Clock::now();
    first.reset();
    if (!initialized) {
      xcb_prefetch_extension_data(connection, &xcb_randr_id);
      xcb_prefetch_extension_data(connection, &xcb_input_id);
      add(ExtRandr, xcb_query_extension(connection, 5, "RANDR").sequence);
      add(ExtXi, xcb_query_extension(connection, 15, "XInputExtension").sequence);
      add(ExtWayland, xcb_query_extension(connection, 8, "XWAYLAND").sequence);
      if (surface)
        add(Geometry, xcb_get_geometry(connection, surface).sequence);
      else
        chooseRoot(0);
      phase = Phase::Extensions;
    } else {
      if (surface) {
        add(Geometry, xcb_get_geometry(connection, surface).sequence);
        phase = Phase::Versions; // Resolve this surface before each pair of reads.
      } else {
        chooseRoot(0);
        startWatcherOrPass();
      }
    }
    xcb_flush(connection);
  }
  void startWatcherOrPass()
  {
    if (watcherRoot != root) {
      revoke();
      if (watcher)
        xcb_destroy_window(connection, watcher);
      watcher = xcb_generate_id(connection);
      require(watcher != std::numeric_limits<std::uint32_t>::max(), "Cannot allocate X11 observer watcher");
      watcherRoot = root;
      checked.push_back(xcb_create_window_checked(
          connection, 0, watcher, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, nullptr
      ));
      const std::uint16_t mask = XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE | XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                                 XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY |
                                 XCB_RANDR_NOTIFY_MASK_PROVIDER_CHANGE | XCB_RANDR_NOTIFY_MASK_PROVIDER_PROPERTY |
                                 XCB_RANDR_NOTIFY_MASK_RESOURCE_CHANGE;
      checked.push_back(xcb_randr_select_input_checked(connection, watcher, mask));
      // A core GetInputFocus/QueryPointer barrier can make the server choose a
      // previously unset client pointer. Use a geometry-only round trip.
      add(Barrier, xcb_get_geometry(connection, root).sequence);
      phase = Phase::Watcher;
    } else {
      startPass();
    }
  }
  void startPass()
  {
    sample = {};
    sample.socketPeer = peer;
    sample.connectionIncarnation = connectionId;
    sample.root = root;
    sample.screen = screen;
    sample.surface = surface;
    add(Geometry, xcb_get_geometry(connection, root).sequence);
    add(Resources, xcb_randr_get_screen_resources_current(connection, root).sequence);
    add(Primary, xcb_randr_get_output_primary(connection, root).sequence);
    add(Providers, xcb_randr_get_providers(connection, root).sequence);
    if (randrMinor >= 5)
      add(Monitors, xcb_randr_get_monitors(connection, root, false).sequence);
    add(Pointer, xcb_input_xi_get_client_pointer(connection, XCB_NONE).sequence);
    add(Devices, xcb_input_xi_query_device(connection, XCB_INPUT_DEVICE_ALL_MASTER).sequence);
    if (surface) {
      add(Geometry, xcb_get_geometry(connection, surface).sequence, surface);
      add(Attributes, xcb_get_window_attributes(connection, surface).sequence);
      add(Translate, xcb_translate_coordinates(connection, surface, root, 0, 0).sequence);
    }
    phase = Phase::Base;
  }

  void base()
  {
    const auto &geometry = get<xcb_get_geometry_reply_t>(Geometry);
    require(geometry.root == root && geometry.width && geometry.height, "Invalid X11 root geometry");
    sample.desktop = {0, 0, geometry.width, geometry.height};
    sample.primaryOutput = get<xcb_randr_get_output_primary_reply_t>(Primary).output;
    const auto &pointer = get<xcb_input_xi_get_client_pointer_reply_t>(Pointer);
    require(pointer.set && pointer.deviceid != 0, "X11 connection has no explicit XI client pointer");
    sample.clientPointer = pointer.deviceid;
    const auto &devices = get<xcb_input_xi_query_device_reply_t>(Devices);
    require(devices.num_infos <= 64, "Too many XI master devices");
    std::map<std::uint16_t, std::pair<std::uint16_t, std::uint16_t>> enabled;
    for (auto i = xcb_input_xi_query_device_infos_iterator(&devices); i.rem; xcb_input_xi_device_info_next(&i)) {
      range(devices, i.data, sizeof(*i.data));
      require(i.data->name_len <= 256 && i.data->num_classes <= 64, "Oversized XI device description");
      // Generated sizeof/next helpers walk the variable class records. Check
      // every nested length before allowing that traversal.
      const auto nameBytes = (std::size_t(i.data->name_len) + 3) & ~std::size_t{3};
      range(devices, i.data, sizeof(*i.data) + nameBytes);
      const auto *position = reinterpret_cast<const std::uint8_t *>(i.data) + sizeof(*i.data) + nameBytes;
      for (unsigned n = 0; n < i.data->num_classes; ++n) {
        range(devices, position, sizeof(xcb_input_device_class_t));
        const auto *item = reinterpret_cast<const xcb_input_device_class_t *>(position);
        require(item->len >= 2, "Malformed XI device class length");
        const auto bytes = std::size_t(item->len) * 4;
        range(devices, position, bytes);
        position += bytes;
      }
      if (i.data->enabled)
        enabled.emplace(i.data->deviceid, std::pair{i.data->type, i.data->attachment});
    }
    const auto master = enabled.find(pointer.deviceid);
    require(
        master != enabled.end() && master->second.first == XCB_INPUT_DEVICE_TYPE_MASTER_POINTER,
        "XI client pointer is not an enabled master pointer"
    );
    const auto keyboard = enabled.find(master->second.second);
    require(
        keyboard != enabled.end() && keyboard->second.first == XCB_INPUT_DEVICE_TYPE_MASTER_KEYBOARD &&
            keyboard->second.second == pointer.deviceid,
        "XI client pointer has no paired enabled master keyboard"
    );
    sample.clientKeyboard = keyboard->first;
    if (surface) {
      const auto &window = get<xcb_get_geometry_reply_t>(Geometry, surface);
      const auto &translated = get<xcb_translate_coordinates_reply_t>(Translate);
      require(window.root == root && translated.same_screen, "X11 surface/root changed while observing");
      sample.client = {translated.dst_x, translated.dst_y, window.width, window.height};
      sample.viewable = get<xcb_get_window_attributes_reply_t>(Attributes).map_state == XCB_MAP_STATE_VIEWABLE;
    }
    Encoding topology;
    topology.number(1); // Encoding schema.
    topology.number(root);
    topology.number(screen);
    topology.number(geometry.width);
    topology.number(geometry.height);
    topology.number(sample.primaryOutput);
    const auto &resources = get<xcb_randr_get_screen_resources_current_reply_t>(Resources);
    require(
        resources.num_outputs <= maxOutputs && resources.num_crtcs <= maxCrtcs && resources.num_modes <= maxModes &&
            resources.names_len <= 4096,
        "RandR resource count exceeds the observation bound"
    );
    range(resources, xcb_randr_get_screen_resources_current_names(&resources), resources.names_len);
    topology.number(resources.timestamp);
    topology.number(resources.config_timestamp);
    configTimestamp = resources.config_timestamp;
    topology.ids(xcb_randr_get_screen_resources_current_outputs(&resources), resources.num_outputs);
    topology.ids(xcb_randr_get_screen_resources_current_crtcs(&resources), resources.num_crtcs);
    std::map<std::uint32_t, std::pair<xcb_randr_mode_info_t, std::string>> modes;
    auto *mode = xcb_randr_get_screen_resources_current_modes(&resources);
    const char *names = reinterpret_cast<const char *>(xcb_randr_get_screen_resources_current_names(&resources));
    std::size_t offset = 0;
    for (unsigned i = 0; i < resources.num_modes; ++i) {
      require(mode[i].name_len <= resources.names_len - offset, "Invalid RandR mode name");
      require(
          modes.emplace(mode[i].id, std::pair{mode[i], std::string(names + offset, mode[i].name_len)}).second,
          "Duplicate RandR mode"
      );
      offset += mode[i].name_len;
    }
    require(offset == resources.names_len, "Trailing RandR mode names");
    topology.number(static_cast<std::uint32_t>(modes.size()));
    for (const auto &[id, value] : modes) {
      const auto &m = value.first;
      topology.number(id);
      topology.number(m.width);
      topology.number(m.height);
      topology.number(m.dot_clock);
      topology.number(m.hsync_start);
      topology.number(m.hsync_end);
      topology.number(m.htotal);
      topology.number(m.hskew);
      topology.number(m.vsync_start);
      topology.number(m.vsync_end);
      topology.number(m.vtotal);
      topology.number(m.mode_flags);
      topology.text(value.second.data(), value.second.size());
    }
    const auto &providers = get<xcb_randr_get_providers_reply_t>(Providers);
    require(providers.num_providers <= maxProviders, "Too many RandR providers");
    range(providers, xcb_randr_get_providers_providers(&providers), providers.num_providers * sizeof(std::uint32_t));
    topology.number(providers.timestamp);
    topology.ids(xcb_randr_get_providers_providers(&providers), providers.num_providers);
    if (randrMinor >= 5) {
      const auto &monitors = get<xcb_randr_get_monitors_reply_t>(Monitors);
      require(monitors.nMonitors <= 32 && monitors.nOutputs <= 64, "Too many RandR monitors");
      std::map<std::uint32_t, Encoding> sorted;
      for (auto i = xcb_randr_get_monitors_monitors_iterator(&monitors); i.rem; xcb_randr_monitor_info_next(&i)) {
        range(monitors, i.data, sizeof(*i.data));
        const auto &m = *i.data;
        require(m.nOutput <= maxOutputs, "Too many outputs in a RandR monitor");
        range(monitors, xcb_randr_monitor_info_outputs(&m), m.nOutput * sizeof(std::uint32_t));
        Encoding encoded;
        encoded.number(m.name);
        encoded.number(m.primary);
        encoded.number(m.automatic);
        encoded.number(m.x);
        encoded.number(m.y);
        encoded.number(m.width);
        encoded.number(m.height);
        encoded.number(m.width_in_millimeters);
        encoded.number(m.height_in_millimeters);
        encoded.ids(xcb_randr_monitor_info_outputs(&m), m.nOutput);
        require(sorted.emplace(m.name, std::move(encoded)).second, "Duplicate RandR monitor identity");
      }
      topology.number(monitors.timestamp);
      topology.number(static_cast<std::uint32_t>(sorted.size()));
      for (const auto &[id, value] : sorted) {
        (void)id;
        topology.text(value.bytes.data(), value.bytes.size());
      }
    }
    sample.topology = std::move(topology.bytes);
    // Copy resource IDs before discarding this reply batch.
    std::vector<std::uint32_t> outputs(
        xcb_randr_get_screen_resources_current_outputs(&resources),
        xcb_randr_get_screen_resources_current_outputs(&resources) + resources.num_outputs
    );
    std::vector<std::uint32_t> crtcs(
        xcb_randr_get_screen_resources_current_crtcs(&resources),
        xcb_randr_get_screen_resources_current_crtcs(&resources) + resources.num_crtcs
    );
    std::vector<std::uint32_t> providerIds(
        xcb_randr_get_providers_providers(&providers),
        xcb_randr_get_providers_providers(&providers) + providers.num_providers
    );
    queries.clear();
    for (auto id : outputs)
      add(Output, xcb_randr_get_output_info(connection, id, configTimestamp).sequence, id);
    for (auto id : crtcs) {
      add(Crtc, xcb_randr_get_crtc_info(connection, id, configTimestamp).sequence, id);
      add(Transform, xcb_randr_get_crtc_transform(connection, id).sequence, id);
      add(Panning, xcb_randr_get_panning(connection, id).sequence, id);
    }
    for (auto id : providerIds)
      add(Provider, xcb_randr_get_provider_info(connection, id, configTimestamp).sequence, id);
    add(XiFocus, xcb_input_xi_get_focus(connection, static_cast<std::uint16_t>(sample.clientKeyboard)).sequence);
    phase = Phase::Details;
  }

  void details()
  {
    Encoding topology{std::move(sample.topology)};
    std::map<std::uint32_t, std::pair<X11NativeRect, bool>> crtcs;
    for (const auto &[key, query] : queries) {
      if (key.first != Crtc)
        continue;
      const auto &c = reply<xcb_randr_get_crtc_info_reply_t>(query.result);
      require(
          c.status == XCB_RANDR_SET_CONFIG_SUCCESS && c.num_outputs <= maxOutputs &&
              c.num_possible_outputs <= maxOutputs,
          "RandR CRTC changed or exceeds the bound"
      );
      range(c, xcb_randr_get_crtc_info_possible(&c), c.num_possible_outputs * sizeof(std::uint32_t));
      const auto &t = get<xcb_randr_get_crtc_transform_reply_t>(Transform, key.second);
      require(
          t.pending_len <= 256 && t.current_len <= 256 && t.pending_nparams <= 64 && t.current_nparams <= 64,
          "RandR transform exceeds the bound"
      );
      range(t, xcb_randr_get_crtc_transform_current_params(&t), t.current_nparams * sizeof(std::int32_t));
      const auto pending = std::bit_cast<std::array<std::int32_t, 9>>(t.pending_transform);
      const auto transform = std::bit_cast<std::array<std::int32_t, 9>>(t.current_transform);
      constexpr std::array<std::int32_t, 9> identity{65536, 0, 0, 0, 65536, 0, 0, 0, 65536};
      const auto &p = get<xcb_randr_get_panning_reply_t>(Panning, key.second);
      require(p.status == XCB_RANDR_SET_CONFIG_SUCCESS, "RandR panning changed during observation");
      const bool unpanned = !p.left && !p.top && !p.width && !p.height && !p.track_left && !p.track_top &&
                            !p.track_width && !p.track_height && !p.border_left && !p.border_top && !p.border_right &&
                            !p.border_bottom;
      crtcs.emplace(
          key.second,
          std::pair{
              X11NativeRect{c.x, c.y, c.width, c.height},
              c.mode != 0 && c.rotation == XCB_RANDR_ROTATION_ROTATE_0 && pending == identity &&
                  transform == identity && unpanned && t.pending_nparams == 0 && t.current_nparams == 0
          }
      );
      topology.number(Crtc);
      topology.number(key.second);
      topology.number(c.timestamp);
      topology.number(c.x);
      topology.number(c.y);
      topology.number(c.width);
      topology.number(c.height);
      topology.number(c.mode);
      topology.number(c.rotation);
      topology.number(c.rotations);
      topology.ids(xcb_randr_get_crtc_info_outputs(&c), c.num_outputs);
      topology.ids(xcb_randr_get_crtc_info_possible(&c), c.num_possible_outputs);
      topology.number(t.has_transforms);
      for (auto value : pending)
        topology.number(static_cast<std::uint32_t>(value));
      for (auto value : transform)
        topology.number(static_cast<std::uint32_t>(value));
      topology.text(xcb_randr_get_crtc_transform_pending_filter_name(&t), t.pending_len);
      topology.number(t.pending_nparams);
      for (unsigned i = 0; i < t.pending_nparams; ++i)
        topology.number(xcb_randr_get_crtc_transform_pending_params(&t)[i]);
      topology.text(xcb_randr_get_crtc_transform_current_filter_name(&t), t.current_len);
      topology.number(t.current_nparams);
      for (unsigned i = 0; i < t.current_nparams; ++i)
        topology.number(xcb_randr_get_crtc_transform_current_params(&t)[i]);
      topology.number(p.timestamp);
      topology.number(p.left);
      topology.number(p.top);
      topology.number(p.width);
      topology.number(p.height);
      topology.number(p.track_left);
      topology.number(p.track_top);
      topology.number(p.track_width);
      topology.number(p.track_height);
      topology.number(p.border_left);
      topology.number(p.border_top);
      topology.number(p.border_right);
      topology.number(p.border_bottom);
    }
    for (const auto &[key, query] : queries) {
      if (key.first == Output) {
        const auto &o = reply<xcb_randr_get_output_info_reply_t>(query.result);
        require(
            o.status == XCB_RANDR_SET_CONFIG_SUCCESS && o.name_len <= 256 && o.num_crtcs <= maxCrtcs &&
                o.num_modes <= maxModes && o.num_clones <= maxOutputs,
            "RandR output changed or exceeds the bound"
        );
        range(o, xcb_randr_get_output_info_name(&o), o.name_len);
        const std::string_view name(reinterpret_cast<const char *>(xcb_randr_get_output_info_name(&o)), o.name_len);
        require(!name.starts_with("XWAYLAND"), "Xwayland output is unsupported for native viewport observation");
        topology.number(Output);
        topology.number(key.second);
        topology.number(o.timestamp);
        topology.number(o.crtc);
        topology.number(o.connection);
        topology.number(o.subpixel_order);
        topology.number(o.mm_width);
        topology.number(o.mm_height);
        topology.text(name.data(), name.size());
        topology.number(o.num_preferred);
        topology.ids(xcb_randr_get_output_info_crtcs(&o), o.num_crtcs);
        // Mode ordering identifies the preferred prefix; preserve that order.
        topology.number(o.num_modes);
        for (unsigned i = 0; i < o.num_modes; ++i)
          topology.number(xcb_randr_get_output_info_modes(&o)[i]);
        topology.ids(xcb_randr_get_output_info_clones(&o), o.num_clones);
        if (o.connection == XCB_RANDR_CONNECTION_CONNECTED && o.crtc) {
          const auto found = crtcs.find(o.crtc);
          require(found != crtcs.end(), "RandR output refers to an unknown CRTC");
          sample.outputs.push_back({key.second, o.crtc, found->second.first, found->second.second});
        }
      } else if (key.first == Provider) {
        const auto &p = reply<xcb_randr_get_provider_info_reply_t>(query.result);
        require(
            p.status == XCB_RANDR_SET_CONFIG_SUCCESS && p.num_crtcs <= maxCrtcs && p.num_outputs <= maxOutputs &&
                p.num_associated_providers <= maxProviders && p.name_len <= 256,
            "RandR provider changed or exceeds the bound"
        );
        range(p, xcb_randr_get_provider_info_name(&p), p.name_len);
        topology.number(Provider);
        topology.number(key.second);
        topology.number(p.timestamp);
        topology.number(p.capabilities);
        topology.text(xcb_randr_get_provider_info_name(&p), p.name_len);
        topology.ids(xcb_randr_get_provider_info_crtcs(&p), p.num_crtcs);
        topology.ids(xcb_randr_get_provider_info_outputs(&p), p.num_outputs);
        std::map<std::uint32_t, std::uint32_t> associated;
        for (unsigned i = 0; i < p.num_associated_providers; ++i)
          require(
              associated
                  .emplace(
                      xcb_randr_get_provider_info_associated_providers(&p)[i],
                      xcb_randr_get_provider_info_associated_capability(&p)[i]
                  )
                  .second,
              "Duplicate RandR associated provider"
          );
        topology.number(static_cast<std::uint32_t>(associated.size()));
        for (auto [id, capabilities] : associated) {
          topology.number(id);
          topology.number(capabilities);
        }
      }
    }
    sample.topology = std::move(topology.bytes);
    sample.inputFocus = get<xcb_input_xi_get_focus_reply_t>(XiFocus).focus;
    sample.focusWithinSurface = surface && sample.inputFocus == surface;
    queries.clear();
    ancestorDepth = 0;
    if (surface && sample.inputFocus > XCB_INPUT_FOCUS_POINTER_ROOT && sample.inputFocus != surface &&
        sample.inputFocus != root) {
      add(Tree, xcb_query_tree(connection, sample.inputFocus).sequence);
      phase = Phase::Ancestors;
    } else {
      finishPass();
    }
  }
  void finishPass()
  {
    const auto identity = socketPeer(connection);
    require(identity && *identity == peer, "X11 socket peer birth identity changed or became unreadable");
    require(Clock::now() < started + freshness, "X11 observation expired while reading native state");
    if (!first) {
      first = sample;
      startPass();
      return;
    }
    require(equalSnapshot(*first, sample), "X11 native state changed between complete observation passes");
    if (current && !equalSnapshot(current->snapshot(), sample))
      revoke();
    require(!closed, "X11 observation is closed");
    sample.revision = revision;
    X11DisplayObservation result;
    result.m_state = std::make_shared<X11DisplayObservation::State>(
        X11DisplayObservation::State{sample, started + freshness, revoked}
    );
    current = std::move(result);
    first.reset();
    phase = Phase::Idle;
    reason.clear();
  }
  void advance()
  {
    switch (phase) {
    case Phase::Extensions: {
      const auto &rr = get<xcb_query_extension_reply_t>(ExtRandr);
      const auto &xi = get<xcb_query_extension_reply_t>(ExtXi);
      require(rr.present && xi.present, "Native X11 observation requires RandR and XI2");
      require(
          !get<xcb_query_extension_reply_t>(ExtWayland).present,
          "Xwayland is unsupported for native viewport observation"
      );
      randrEvent = rr.first_event;
      xiOpcode = xi.major_opcode;
      if (surface)
        chooseRoot(get<xcb_get_geometry_reply_t>(Geometry).root);
      queries.clear();
      // Prefetches precede the completed extension replies on this connection.
      // Generated extension requests therefore use already received metadata.
      require(
          xcb_get_extension_data(connection, &xcb_randr_id)->present &&
              xcb_get_extension_data(connection, &xcb_input_id)->present,
          "X11 extension metadata unavailable"
      );
      add(RandrVersion, xcb_randr_query_version(connection, 1, 5).sequence);
      // Do not negotiate XIQueryVersion on a borrowed connection. Its owner
      // (notably Qt) may already have negotiated a higher version; a lower
      // repeat can fail and changing it can alter the owner's event semantics.
      // Only stable XI2.0 read requests are used; unsupported requests deny.
      phase = Phase::Versions;
      break;
    }
    case Phase::Versions:
      if (!initialized) {
        const auto &rr = get<xcb_randr_query_version_reply_t>(RandrVersion);
        require(rr.major_version == 1 && rr.minor_version >= 4, "Native X11 observation requires RandR 1.4");
        randrMinor = rr.minor_version;
        initialized = true;
      } else {
        chooseRoot(get<xcb_get_geometry_reply_t>(Geometry).root);
      }
      queries.clear();
      startWatcherOrPass();
      break;
    case Phase::Watcher:
      for (auto cookie : checked) {
        std::unique_ptr<xcb_generic_error_t, decltype(&free)> error(xcb_request_check(connection, cookie), &free);
        require(!error, "Cannot create or subscribe the private X11 observer watcher");
      }
      checked.clear();
      queries.clear();
      startPass();
      break;
    case Phase::Base:
      base();
      break;
    case Phase::Details:
      details();
      break;
    case Phase::Ancestors: {
      const auto &tree = get<xcb_query_tree_reply_t>(Tree);
      require(tree.root == root && ++ancestorDepth <= 32, "X11 focus ancestry is unavailable or exceeds the bound");
      const auto parent = tree.parent;
      sample.focusWithinSurface = parent == surface;
      queries.clear();
      if (parent && parent != root && parent != surface)
        add(Tree, xcb_query_tree(connection, parent).sequence);
      else
        finishPass();
      break;
    }
    case Phase::Idle:
      break;
    }
  }
  void poll()
  {
    if (closed)
      return;
    if (xcb_connection_has_error(connection)) {
      fail("Native X11 connection closed");
      closed = true;
      return;
    }
    if (phase == Phase::Idle)
      return;
    require(Clock::now() < started + freshness, "X11 observation timed out");
    bool complete = true;
    unsigned budget = 16;
    for (auto &[key, query] : queries) {
      (void)key;
      if (query.result)
        continue;
      if (!budget--) {
        complete = false;
        break;
      }
      void *value = nullptr;
      xcb_generic_error_t *error = nullptr;
      if (!xcb_poll_for_reply(connection, query.sequence, &value, &error)) {
        complete = false;
        continue;
      }
      std::unique_ptr<xcb_generic_error_t, decltype(&free)> failure(error, &free);
      query.result = std::shared_ptr<void>(value, &free);
      require(!failure && value, "Native X11 query failed (surface/resource may have been destroyed)");
      (void)reply<xcb_generic_reply_t>(query.result);
    }
    if (complete)
      advance();
    xcb_flush(connection);
  }
};

X11DisplayObserver::X11DisplayObserver(xcb_connection_t *borrowed, int screenIndex)
    : m_impl(std::make_unique<Impl>(borrowed, screenIndex))
{
}
X11DisplayObserver::~X11DisplayObserver()
{
  close();
}
void X11DisplayObserver::refresh(std::uint32_t surface)
{
  try {
    m_impl->refresh(surface);
  } catch (const std::exception &error) {
    m_impl->fail(error.what());
  }
}
void X11DisplayObserver::poll()
{
  try {
    m_impl->poll();
  } catch (const std::exception &error) {
    m_impl->fail(error.what());
  }
}
bool X11DisplayObserver::pending() const noexcept
{
  return m_impl->phase != Impl::Phase::Idle;
}
void X11DisplayObserver::invalidate()
{
  if (m_impl->closed)
    return;
  if (m_impl->phase == Impl::Phase::Watcher) {
    // Xlib owners may invalidate on the initial RandR notification before
    // receiving our barrier. Preserve the checked CreateWindow/SelectInput
    // replies: dropping them could publish later reads without ever verifying
    // that notification subscription succeeded. No native sample has started
    // yet, so the reads after this barrier will observe the changed state.
    m_impl->revoke();
    m_impl->first.reset();
    m_impl->reason = "Native X11 state was invalidated while verifying the watcher";
    if (m_impl->closed) {
      m_impl->discard();
      m_impl->phase = Impl::Phase::Idle;
    }
    return;
  }
  m_impl->fail("Native X11 state was invalidated");
}
void X11DisplayObserver::close()
{
  if (!m_impl)
    return;
  m_impl->fail("Native X11 observer closed");
  m_impl->closed = true;
  if (m_impl->watcher && m_impl->connection && !xcb_connection_has_error(m_impl->connection)) {
    xcb_destroy_window(m_impl->connection, m_impl->watcher);
    xcb_flush(m_impl->connection);
  }
  m_impl->watcher = 0;
}
void X11DisplayObserver::handleEvent(const xcb_generic_event_t &event)
{
  if (m_impl->closed)
    return;
  const auto type = event.response_type & 0x7f;
  bool relevant =
      type == m_impl->randrEvent + XCB_RANDR_SCREEN_CHANGE_NOTIFY || type == m_impl->randrEvent + XCB_RANDR_NOTIFY;
  if (type == XCB_GE_GENERIC) {
    const auto &generic = reinterpret_cast<const xcb_ge_generic_event_t &>(event);
    relevant = generic.extension == m_impl->xiOpcode &&
               (generic.event_type == XCB_INPUT_HIERARCHY || generic.event_type == XCB_INPUT_DEVICE_CHANGED ||
                generic.event_type == XCB_INPUT_FOCUS_IN || generic.event_type == XCB_INPUT_FOCUS_OUT);
  }
  // Changes to unrelated windows are ignored. Qt/core retain event ownership.
  std::uint32_t window = 0;
  switch (type) {
  case XCB_CONFIGURE_NOTIFY:
    window = reinterpret_cast<const xcb_configure_notify_event_t &>(event).window;
    break;
  case XCB_REPARENT_NOTIFY:
    window = reinterpret_cast<const xcb_reparent_notify_event_t &>(event).window;
    break;
  case XCB_DESTROY_NOTIFY:
    window = reinterpret_cast<const xcb_destroy_notify_event_t &>(event).window;
    break;
  case XCB_MAP_NOTIFY:
    window = reinterpret_cast<const xcb_map_notify_event_t &>(event).window;
    break;
  case XCB_UNMAP_NOTIFY:
    window = reinterpret_cast<const xcb_unmap_notify_event_t &>(event).window;
    break;
  case XCB_FOCUS_IN:
  case XCB_FOCUS_OUT:
    window = reinterpret_cast<const xcb_focus_in_event_t &>(event).event;
    break;
  default:
    break;
  }
  relevant |= window && (window == m_impl->surface || window == m_impl->root);
  if (relevant)
    invalidate();
}
std::optional<X11DisplayObservation> X11DisplayObserver::observe() const
{
  if (m_impl->current && m_impl->current->live())
    return m_impl->current;
  return {};
}
std::string X11DisplayObserver::unavailableReason() const
{
  if (observe())
    return {};
  return m_impl->reason.empty() ? "Native X11 observation expired; a fresh read is required" : m_impl->reason;
}

} // namespace deskflow::windowshare
