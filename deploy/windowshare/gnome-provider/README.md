# Experimental GNOME Shell 50 observation provider

This extension is **default off**. Nothing in the build installs or enables it.
It runs only in a stable GNOME Shell 50 native Wayland compositor, including a
private headless native test compositor. It rejects X11/nested-X11 backends.
It owns a real Mutter RemoteDesktop session and a real `Meta.WaylandClient`
subprocess. It reports observations, **not permission to deliver original input**.
`deliveryReady`, `syncFenceVerified`, `pointerDeviceAttribution`, and
`qtViewportMapping` are always false.

The declared boundary is a trusted local GNOME desktop: Shell, its enabled
extensions, the configured viewer executable, and the core caller are trusted.
This is not sandboxing of other same-user processes or process-memory
attestation. Any local caller may reserve the one available session; subsequent
operations require that exact caller's D-Bus unique name. No title, application
ID, process name, matching rectangle, or supplied PID establishes window identity.

## Interface

The bus and interface are `org.deskflow.Seamless.Gnome50`; the object is
`/org/deskflow/Seamless/Gnome50`. `interface.xml` is authoritative. Before use,
the consumer must resolve the provider and `org.gnome.Shell` and require the
**same unique owner**, then address that unique owner for every call. Restart or
name replacement retires all observations. The provider checks that Shell and
`org.gnome.Mutter.RemoteDesktop` belong to its own shared bus connection before
exporting the interface.

- `GetCapabilities() -> (u version, s shellVersion, a{sv} capabilities)`.
  Version is 1. Capability types are `providerApi:u`, `backend:s`,
  `nativeHitTesting:b`, `exactWaylandClientBinding:b`, `eisSessionOwned:b`,
  `trustedDesktopOnly:b`, `viewportChannel:b`, `viewportSocketValidated:b`,
  `syncFenceVerified:b`, `pointerDeviceAttribution:b`,
  `qtViewportMapping:b`, and `deliveryReady:b`.
- `CreateSession() -> (s session, u generation, h eisFd)`. One outstanding
  session is allowed. Creation, Start, and ConnectToEIS are asynchronous with
  an absolute ten-second startup deadline. `h` indexes the single genuine
  descriptor in the attached Unix FD list; it is not a numeric process FD.
  The EIS device-types request is keyboard plus pointer, with no screencast
  stream. The consumer still must complete the ordinary EI handshake and track
  its actual seat/device lifetime.
- `LaunchViewer(s session, u generation, s absoluteExecutable, as arguments)
  -> (s viewer)`. Exactly one child process may be launched per session. It
  receives Mutter's private Wayland socket. Qt/GDK are forced to native Wayland;
  Qt platform/plugin override arguments are rejected. The executable is a
  regular executable path, not a shell command. Arguments are limited to 64
  entries, 4096 characters each and 16384 characters total. The caller supplies
  the normal viewer configuration, such as `--headless`, `--connect`,
  `--identity`, and `--peer-pin`. The extension does not invent a remote peer.
- `LaunchViewerWithViewport(s session, u generation, s absoluteExecutable,
  as arguments, h viewportFd) -> (s viewer)` adds a private core-to-viewer
  observation channel to that same owned subprocess. It requires exactly one
  attached Unix FD at handle zero, representing a connected Unix stream socket.
  Missing/extra descriptors and other handles are rejected before launch. The
  trusted core creates the socketpair; the child must reject non-stream,
  non-Unix or unconnected endpoints before adoption. The extension maps it to child FD **4**
  and appends exactly `--gnome-viewport-fd=4`; Mutter reserves FD 3 for
  `WAYLAND_SOCKET`. Caller-supplied forms of the reserved option are rejected by
  both launch methods; the viewport method also rejects `--` argument termination.
  The same caller/session and one-viewer checks apply across both methods.
  `viewportChannel=true` advertises FD forwarding only, never viewport readiness
  or delivery authority; `viewportSocketValidated` remains false. Core and child still validate and own their endpoint
  lifetime and framed protocol. The provider does not read or write the channel.
- `GetViewer(s session, u generation, s viewer) -> (a{sv} observation)` reports
  the current owned window and native surface. Exactly one normal, nontransient
  toplevel and one native surface are supported. Multiple windows or native
  subsurfaces explicitly report unavailable. The first bound window cannot be
  replaced in the same session. Merely accepting a launch is not window readiness.
- `ValidateCurrentHit(s session, u generation, s viewer, s request, d x, d y,
  u expectedRevision) -> (a{sv} observation)` performs a fresh native scene pick.
  The token is a 1–64 character ASCII letter/digit/underscore/hyphen string,
  echoed for consumer association. It is not a compositor-generated sequence or
  an anti-replay capability. The caller must retain its own sequence, request
  start/deadline, route, focus, source geometry, and forwarding-fence bindings.
- `CloseSession(s session, u generation)` retires the session and owned child.
  The same teardown occurs on caller loss, Mutter closure, viewer loss, or
  extension disable. Only the owned child receives SIGTERM and, if necessary,
  SIGKILL after one second; wait/reaping remains asynchronous.

Signals are `Closed(s session, u generation, s reason)` and
`Invalidated(s session, u generation, u revision, s reason)`. Generations and
revisions are nonzero uint32 values and do not wrap. Consumers must retain the
provider unique owner as part of their identity; a new extension lifetime has a
new random session identifier even if its generation counter starts over.

Every observation contains:

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `available`, `reason` | `b`, `s` | Current operation's result and explicit unavailable reason |
| `observationOnly`, `deliveryReady` | `b`, `b` | Always true and false respectively |
| `session`, `generation`, `revision`, `viewer` | `s`, `u`, `u`, `s` | Exact provider session and sampled native state |
| `sampledAtUsec` | `s` | Decimal local monotonic timestamp; not a cross-machine clock or renewable lease |
| `window`, `surface` | `s`, `s` | Fresh opaque tokens for retained native object identities, when present |
| `frameRect`, `bufferRect` | `(dddd)` | Native x/y/width/height, when valid |
| `pointerX`, `pointerY`, `focused` | `d`, `d`, `b` | Native cursor readback and exact focused-window comparison, when a window exists |
| `request` | `s` | Current-hit request echo |
| `localX`, `localY` | `d`, `d` | Successful current hit in that actual Wayland surface's coordinates |

`GetViewer.available` means the native observation is usable, not that the
cursor hits the viewer. Only `ValidateCurrentHit` picks the point. It requires
the expected revision, exact Graphene float cursor readback, the exact retained
native surface actor, and successful native stage-to-surface conversion. It
rechecks native state and cursor after the pick. It rejects locked/greeter
sessions, overview, stage grabs, Shell modals, exposed button-held state,
unfocused/hidden/minimized windows, ambiguity, and unsupported geometry. It
never activates/raises a window, changes focus, acquires a grab, or injects an
event itself. It exposes no tests-only method to relax these checks.

Signals cover window creation/restacking/focus, monitor layout, workspace,
overview/session mode, grabs, and bound-window geometry/lifetime. Each query
also compares sampled state. Surface input regions are checked by the fresh
native pick, not cached rectangles. This does not promise notification of every
native state transition, complete physical-button neutrality, or the effective
Wayland focus target under arbitrary grabs. No result is a retained live proof.

## Lifecycle details

Each session uses a **separate private connection to the existing session bus**
to call Mutter. Closing that connection retires Mutter's caller-owned session
even if an asynchronous CreateSession reply was lost or arrives after timeout.
It does not change the public provider's Shell-owner identity. All native/D-Bus
work runs on the Shell main context. Retired callbacks compare exact session
objects and cannot erase a later session. The active session is detached before
external cleanup. File validation and Mutter's subprocess launcher use their
normal synchronous native calls; the ten-second deadline is not a promise to
preempt a stalled filesystem or native library call.

The viewer is matched using `Meta.WaylandClient.owns_window()` on actual native
objects. The private Wayland socket is retained by Mutter, and unrelated
same-title/PID claims do not match it. A genuine interpreter-launched GTK test
child is allowed; it must still create its surface on that exact connection.

Core supplies a CLOEXEC, nonblocking Unix stream endpoint; Shell preserves its
mode and the child validates its native type, family and connected state. Shell
does not use `Gio.Socket.new_from_fd()` to inspect an arbitrary descriptor:
the supported GLib runtime closes it on constructor failure, contrary to that
API's documented caller-close-on-failure ownership. Closing by its old numeric
value could close a reused, unrelated descriptor. `UnixFDList.get()` instead
creates a CLOEXEC duplicate with unambiguous ownership, transferred with `SubprocessLauncher.take_fd()`;
`SubprocessLauncher.close()` closes all parent-side mappings immediately after
spawn or failure. The invocation's FD list is explicitly emptied and closed on
every return, including wrong-caller and already-launched rejection. Endpoint
cleanup and peer EOF do not depend on GJS garbage collection. The provider never
retains a viewport endpoint after the launch method finishes.

## Validation

Pure tests require Node and no desktop:

```sh
node --test deploy/windowshare/gnome-provider/tests/*.test.js
node --check deploy/windowshare/gnome-provider/extension.js
node --check deploy/windowshare/gnome-provider/launcher.js
```

These test caller/generation ownership, absolute deadlines, late completion,
terminal disable, overflow, argument bounds, native-state policy, and fractional
coordinate comparison. They do not prove GJS or compositor behavior.

The headless GJS/GIO descriptor fixture requires Linux, GJS and Python, but no
bus, display or compositor:

```sh
python3 deploy/windowshare/gnome-provider/tests/launcher-native.py
```

It uses disposable socketpairs and real subprocesses to check FD 4 exchange,
the exact reserved argument, EOF while the launcher process remains alive,
legacy launch, malformed lists/handles, child-side socket validation, callback/spawn
failures, and repeated rejection without descriptor growth. The separate native
compositor harness must also exercise `LaunchViewerWithViewport` over D-Bus and
confirm FD 4 coexists with Mutter's actual FD 3 Wayland connection.

The isolated integration harness must copy this directory only into a temporary
`XDG_DATA_HOME/gnome-shell/extensions/seamless-windows@deskflow.org`, use private
HOME/settings/runtime/session and system buses, and start its own native
headless GNOME Shell 50. It must never reuse a user's bus, display, extension
configuration, or settings. Required real assertions include:

1. Provider and Shell share the owned compositor's unique bus owner; returned
   EIS FD completes an actual libei handshake against that compositor.
2. Only the launched native Wayland child is matched; foreign caller, stale
   generation/revision, another window, and an occluder cannot obtain a hit.
3. Genuine EIS motion followed by ping produces fresh cursor readback and
   surface-local results; fractional scaling and native input regions agree.
4. A stage grab/modal, focus loss, close, caller disconnect, and compositor
   restart invalidate observations; the owned viewer and EIS session terminate.
5. `deliveryReady` stays false on every positive observation.

Mutter 50's actual input-thread-to-main-context EIS sync requires a build with
`HAVE_EIS_EVENT_REF`, selected by **libeis >= 1.5**. A Shell version check alone
does not establish it. The client ping API requires libei >= 1.4; libei can
synthesize a pong before a disconnect, so a pong alone is not sufficient.
The provider reports this build-specific fence as unverified. A future delivery
consumer must separately establish ordering, full route fencing and painted Qt
content mapping, then bind each validation to the original sequence and its
host-started deadline. Until then the original input authorizer remains unset.

## Primary API sources

Implementation was checked against the official Mutter and Shell **50.0**
sources. The stable Shell 50 family is the only enabled API family; missing or
changed native APIs cause unavailability, not a geometry fallback.

- [Mutter private RemoteDesktop D-Bus interface](https://github.com/GNOME/mutter/blob/50.0/data/dbus-interfaces/org.gnome.Mutter.RemoteDesktop.xml)
- [RemoteDesktop logical-monitor viewports and ConnectToEIS](https://github.com/GNOME/mutter/blob/50.0/src/backends/meta-remote-desktop-session.c)
- [Meta.WaylandClient launch, private socket and exact owns_window](https://github.com/GNOME/mutter/blob/50.0/src/wayland/meta-wayland-client.c)
- [Native surface input-region picking](https://github.com/GNOME/mutter/blob/50.0/src/compositor/meta-surface-actor.c)
- [Wayland surface-local conversion uses transform_stage_point](https://github.com/GNOME/mutter/blob/50.0/src/wayland/meta-wayland-actor-surface.c#L367-L380)
- [Native cursor readback](https://github.com/GNOME/mutter/blob/50.0/src/backends/meta-cursor-tracker.c#L406-L425)
- [Mutter input-thread sync](https://github.com/GNOME/mutter/blob/50.0/src/backends/meta-eis.c#L100-L171) and [conditional build gate](https://github.com/GNOME/mutter/blob/50.0/meson.build#L581-L582)
- [Shell modal/session state](https://github.com/GNOME/gnome-shell/blob/50.0/js/ui/main.js)
- [GIO asynchronous Unix FD calls](https://docs.gtk.org/gio/method.DBusConnection.call_with_unix_fd_list.html) and [FD replies](https://docs.gtk.org/gio/method.DBusMethodInvocation.return_value_with_unix_fd_list.html)
- [GIO FD duplication](https://docs.gtk.org/gio/method.UnixFDList.get.html) and [FD list ownership transfer](https://docs.gtk.org/gio/method.UnixFDList.steal_fds.html)
- [GSocket constructor ownership documentation](https://docs.gtk.org/gio/ctor.Socket.new_from_fd.html) and [GLib constructor/finalizer implementation](https://github.com/GNOME/glib/blob/2.86.0/gio/gsocket.c)
- [Subprocess FD mapping](https://docs.gtk.org/gio/method.SubprocessLauncher.take_fd.html) and [deterministic launcher close](https://docs.gtk.org/gio/method.SubprocessLauncher.close.html)
- [libei ping/disconnect semantics](https://libinput.pages.freedesktop.org/libei/api/group__libei.html)
