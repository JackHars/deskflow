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
ID, process name, matching rectangle, or supplied PID alone establishes window
identity. The production Qt path additionally challenges each actual owned
`xdg_toplevel` with a fresh private random app ID, as described below.

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
  `trustedDesktopOnly:b`, `viewportChannel:b`, `perWindowAssociation:b`,
  `routedPointObservation:b`, `routedPointsObservation:b`,
  `stageUpdateSequence:b` (optional real two-frame read-only diagnostic),
  `inputDrainSegment:b`, `inputDrainOrderingContract:s`, `inputDrainOrderingLimit:s`,
  `viewportSocketValidated:b`,
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
- `GetViewerWithAssociation(s session, u generation, s viewer, s association)
  -> (a{sv} observation)` selects one normal owned toplevel by its exact private
  role challenge. Qt writes `org.deskflow.window.` followed by 64 lowercase hex
  characters to its retained actual `xdg_toplevel`, and includes the same token
  in its authenticated painted snapshot. The provider requires exactly one
  `Meta.WaylandClient.owns_window()` match whose `wm-class` and instance both
  equal that token. Mutter's processing of that exact role write supplies the
  association; a title, PID, user-supplied app ID, or coordinate does not.
  Other owned windows may coexist. Popups, transients, duplicate tokens and
  multiple native surface actors within the selected toplevel remain denied.
  Selecting another token invalidates the old observations and prefix seal.
  The production client requires this capability and never falls back to
  inferring a focused surface from the process's window count.
- `ValidateCurrentHit(s session, u generation, s viewer, s request, d x, d y,
  u expectedRevision) -> (a{sv} observation)` performs a fresh native scene pick.
  The token is a 1–64 character ASCII letter/digit/underscore/hyphen string,
  echoed for consumer association. It is not a compositor-generated sequence or
  an anti-replay capability. The caller must retain its own sequence, request
  start/deadline, route, focus, source geometry, and forwarding-fence bindings.
- `ValidateRoutedPoint(s session, u generation, s viewer, s request, d x, d y,
  u expectedRevision) -> (a{sv} observation)` is an **optional, read-only**
  native hit query, available only when `routedPointObservation=true`. The
  coordinate must be an exact, finite, float-representable compositor-stage
  coordinate bounded to +/-10,000,000; the native observation returns separate
  `routedX/routedY` echo fields. It performs a native reactive-stage surface
  pick and transform at the **supplied frozen point**, not the current EIS
  cursor location, then rechecks the surface, revision, state and occlusion.
  The C++ client retains its original `captureEpoch/sequence` outside D-Bus
  and checks this exact point echo before joining painted Qt geometry. An old
  provider or malformed/replayed reply cannot silently convert this into the
  current-cursor path. No native input injection, ordering fence, or delivery
  authority is established by a successful result.
- `ObserveStageFrames(s session, u generation, s viewer, s request, d x, d y,
  u expectedRevision) -> (a{sv} observation)` is an **optional, read-only**
  same-view stage-update observation. The provider accepts only the exact
  caller and owned viewer and requires the frozen point to be the reactive hit
  on its sole native surface. It listens to Mutter's actual
  `ClutterStage::before-update/after-update` signals and requires two distinct,
  correctly paired cycles on the **same** stage view whose *native layout*
  contains that point. Unsupported `StageView.get_layout`, layout drift,
  source focus/occlusion changes, reentry, invalidation, caller loss, or the
  **120 ms absolute** deadline refuse the result. A second stage update is
  scheduled on the GNOME main context only after the first completion. It
  never opens a second display, posts input, or copies a borrowed native
  pointer to the core worker. The strict reply has
  `session/generation/viewer/request/revision,stageX/stageY,stageUpdates=2,
  sameStageView=true,observationOnly=true,deliveryReady=false` with explicit
  `available/reason`; the core validates the complete tuple. The session
  permits only one outstanding check. This method is connected between the
  exact GNOME hit and Qt's queued observation when advertised.
  **Important:** `after-update` runs after that stage's event-queue pass,
  but queued input may be detached or reentrantly scheduled and no source EIS
  device/prefix is associated with these two frames. This is *not* a native
  old-input drain or a permission to enable original-input forwarding.
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
| `windowAssociation` | `s` | Exact selected private Qt role challenge, or empty for the legacy sole-window diagnostic |
| `frameRect`, `bufferRect` | `(dddd)` | Native x/y/width/height, when valid |
| `pointerX`, `pointerY`, `focused` | `d`, `d`, `b` | Native cursor readback and exact focused-window comparison, when a window exists |
| `request` | `s` | Native hit request echo (not standalone capture authority) |
| `routedX`, `routedY` | `d`, `d` | Exact requested compositor-stage coordinate echo, only for routed-point queries |
| `localX`, `localY` | `d`, `d` | Successful current hit in that actual Wayland surface's coordinates |

`GetViewer.available` means the native observation is usable, not that the
cursor hits the viewer. Both `ValidateCurrentHit` and the new read-only
`ValidateRoutedPoint` can pick a point; the first also requires an exact
current-cursor match. Neither grants original-input delivery. It requires
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
The production Qt 6.8/6.10 adapter retains the actual `QWaylandWindow` shell
role, requires the exact built-in `QWaylandXdgSurface` and a non-null
`xdg_toplevel*` role, and rotates the random challenge after role/surface
replacement. It uses Qt's existing Wayland connection. This capability check
assumes the same trusted in-process Qt runtime as the input drain; it does not
authenticate arbitrary hostile plugins loaded into that process. The token
does not grant delivery or focus authority and never extends a point deadline.

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
`DrainInputPrefix` separately checks an empty global Clutter queue outside
nested dispatch followed by a later completed update of the same stage view.
Its closed reply echoes the exact caller binding and has `orderingContract:s`
and `orderingLimit:s`. A supported mapped build has a `sha256:<digest>` contract
and an empty limitation; other builds have an empty contract and
`mutter-eis-sync-build-unverified`. The client requires an exact match to the
startup capability, then the exact All-fenced Qt queue operation and final
native point/focus recheck. This operation alone does not prove the caller's
sender PONG, event batch, authenticated Core connection or original capture.

## Native sender ordering build prerequisite

The closed runtime allowlist supports **Ubuntu resolute amd64** packages
`libmutter-18-0 50.1-0ubuntu2.2`, `50.1-0ubuntu2.4` and `50.1-0ubuntu2.5`, built with
`libeis-dev 1.5.0-3` and the native backend. It hashes both the actual loaded
`/usr/lib/x86_64-linux-gnu/libmutter-18.so.0.0.0` and the matching
`mutter-18/libmutter-clutter-18.so.0.0.0` at startup. The exact expected digests
are in `logic.js`; a mixed pair is unsupported. Main-module contract digests:

| Package | Main module SHA-256 |
| --- | --- |
| 50.1-0ubuntu2.2 | `fec9023139ac8eff93fcc1af90413613cdb526e7f02389dc9d8fe90f7abc81e5` |
| 50.1-0ubuntu2.4 | `4d7ece1588279dbd3b66de3a28d1bf1098e7905b705d8e44743192b925d66b83` |
| 50.1-0ubuntu2.5 | `72ffe9741f6258c838ab73c37df0d9aece3f5d394ead8b86b4b2a306ad16c192` |

`ordering.js` reads bounded `/proc/self/maps` data from the provider's own
Shell process. It requires one exact, nondeleted, private executable mapping
identity for each approved path, matches mapped device/inode to the **same
opened file descriptor** being hashed, and requires root ownership, regular
files, and no group/other write permission. File size and change metadata must
stay unchanged across hashing; mapping/file identity is rechecked before a
drain and at its completion. Hashing yields between 256 KiB reads, with a
separate two-second startup limit, 2 MiB maps limit and 8 MiB per-file ceiling.
No environment variable, user manifest, package version text, imported symbol,
or caller-provided contract can add support. A changed or unknown build loses
the sender prerequisite; it cannot silently substitute a new contract.

This is a **trusted local desktop build contract**, not process-memory
attestation. It trusts the Ubuntu builder/HTTPS provenance, root-owned system
libraries and loader, the running Shell and enabled extensions. It does not
defend against root rewriting a mapped library in place, code injection,
LD_PRELOAD/interposition, or hostile in-process extensions. It does not identify
an EIS device from a Clutter event. Updates/new architectures require another
reviewed source/build/package contract and native tests. Keeping an older
supported library installed is not a reason to postpone security updates; a new
unreviewed build simply denies this experimental sender prerequisite.

The source checkout includes `verify-ordering-build.py` and
`ordering-builds.json`. The verifier downloads only pinned primary Launchpad
artifacts when `--download` is supplied, checks source descriptor and builder
checksums, applies every Debian patch, checks the reviewed sender/queue source
hashes, checks native/libeis build configuration, and extracts both exact module
hashes. It makes no installation or desktop change. This reproduces the
published builder evidence; it does not claim an independent binary rebuild.
The runtime never reads that manifest or the verifier's output.

```sh
python3 deploy/windowshare/gnome-provider/verify-ordering-build.py \
  --cache build/mutter-ordering-evidence --download \
  --output build/mutter-ordering-evidence/verified.json
```

The separate `tests/ordering-native.js` GJS probe loads these packaged libraries
and checks actual process mappings without starting a desktop. Full support
also requires the private native Shell integration test, exact requested Core
sender cutoff and retained Qt/All binding. The typed C++ `SenderOrderingEvidence`
shares the segment's revocation and original absolute deadline. Exact
`cancelInputDrain(localRequestId)` immediately revokes pending/completed copies
and removes unread results; a bounded in-flight native call settles before the
slot is reused. Wrong IDs and late replies cannot revive a canceled result.

For the standalone probe, GJS needs Mutter's private typelib/library directory
(GNOME Shell normally supplies this itself):

```sh
GI_TYPELIB_PATH=/usr/lib/x86_64-linux-gnu/mutter-18 \
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/mutter-18 \
gjs -m deploy/windowshare/gnome-provider/tests/ordering-native.js
```

Primary build records: [Ubuntu 2.2 amd64 build](https://launchpad.net/ubuntu/+source/mutter/50.1-0ubuntu2.2/+build/32927878),
[Ubuntu 2.4 amd64 build](https://launchpad.net/ubuntu/+source/mutter/50.1-0ubuntu2.4/+build/33579950),
and [Ubuntu 2.5 amd64 build](https://launchpad.net/ubuntu/+source/mutter/50.1-0ubuntu2.5/+build/33824576).
All three patched sources retain `meta-eis.c:100–169`'s held SYNC reference,
input-thread task and main-thread return; `meson.build:590–591` enables that
path from libeis >=1.5. Exact source and artifact digests are recorded in the
checked-in evidence manifest, including the Debian patches affecting the
surrounding native input implementation.

The 2.5 review applies every Debian patch before comparison with 2.4. EIS,
native-seat, virtual-device, backend and stage-view files are unchanged. Clutter
adds tablet `PROXIMITY_OUT` to the existing immediate queue-drain branch and
clears that tablet sprite's area; it does not defer the EIS prefix. Additional
changes track sticky mouse focus, tablet implicit grabs and current maximized
window placement. Their affected implementation files are also hashed in the
2.5 source manifest. Existing focus/geometry invalidation still applies; the
build contract grants no new tablet support. The main module grew to 2,759,144
bytes, so module sizes are matched per exact build alongside both hashes.
Mixed-build modules and wrong sizes remain unsupported. The isolated Qt fixture
defaults to this latest reviewed contract; use its explicit
`--expected-ordering-contract` option for an older supported installation.

## First-click preparation

The separate IPP1 preparation path accepts only the original capture/connection
stamp and frozen desktop point; it cannot fabricate a focused All lease. The
inherited private Qt inventory supplies the actual Viewer connection, peer pin
and per-window role tokens. A connection-wide preparation gate closes before
native selection, including Viewers created while preparation is pending.

After the caller's requested EI PONG and complete event batch, the provider
requires its verified mapped Mutter contract, an exact owned surface hit,
empty Clutter queue outside nested dispatch, a later completed same-view update,
and the actual Qt default queue drain. It then activates that retained
MetaWindow without injecting a click. Activation completes only after two
completed updates of the same StageView, within the smaller of 120 ms and the
remaining preparation budget. The exact focused window, owned surface, frozen
hit, geometry and view layout must remain unchanged. One coalesced `restacked`
signal before the first completed update is permitted; all other invalidations,
a second restack, or a later restack refuse activation. Mutter's global signal
cannot attribute an individual raise, so this is a fresh activation-completion
boundary, not native event attribution. Every signal still revokes older
receipts. The supported patched Mutter sources show the synchronous raise/focus
in `src/core/window.c:4194–4206` and deferred `META_LATER_SYNC_STACK` plus
`display::restacked` in `src/core/stack-tracker.c:1112–1146`.

A focus-only intent then obtains a fresh Host acknowledgement. Any native
change after the completed activation reply through final publication refuses
the prepared focus; the client never adopts a later revision to recover it.
The original absolute preparation budget is at most
500 ms; mapped promotion still has its own at-most-250-ms point deadline and
cannot outlive the original budget.

The genuine Host All fence coexists with the preparation gate. Promotion records
the exact consumed association and creates the normal fresh mapped receipt and
historical prefix seal; it does not open preparation. Independent Host resume
revokes All evidence while preparation continues to suppress forwarded source
input. Exact cancellation performs another native and Qt queue drain under the
preparation gate, then removes that gate. Failure, expiry or socket EOF is never
reported as successful cleanup. The Core caller keeps its sender paused until
that cleanup result, or tears down its owned route on failure.

`gnome-qt-viewer-tests.py` exercises this native provider/Qt chain with disposable
mutually pinned TLS peers and synthetic Core capture correlation. Its idle case
uses the fixture-owned real libei sender, including an actual four-second idle
stop and restart; it is **not** runtime proof of production `EiComputer` idle
admission. The report states `productionCoreCutoff: false`,
`productionEiComputer: false` and `originalDeliveryAuthority: false`. Native
activation must emit no click, Host All resume must leave preparation closed,
and only completed cleanup permits a fresh positive input receipt. Native runs
are separate from pure/private-bus policy regressions and remain required.

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
