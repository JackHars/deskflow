# Authenticated coordinate integration plan

This is a proposed integration following `bc88d6e83`, not an enabled input path.
The smallest useful X11 slice is a native namespace witness carried over the two
authenticated connections, followed by sequence-bound pointer validation and an
ordered forwarding fence. Matching observations alone must not enable original
input. GNOME Wayland needs a different compositor-backed provider.

## Supported boundary

Trust the configured Linux endpoints and their local X server/desktop clients,
as ordinary X11 input sharing already does. Protect against network substitution,
stale sessions, accidental different displays, reconnects, wrong roots/devices,
layout changes and incorrect event association. This is not protection against
an authorized malicious X client relaying properties or a malicious X server.
X11 resources are shared within the server's namespace; access to that namespace
is not process attestation. See the [X.Org communication guide](https://www.x.org/guide/communication/).

Mutual pinned TLS authenticates the configured key holders and their current
connections. It does not sign or attest the Linux executable or X server.
Retain the actual `AuthenticatedPeerTicket`/`AuthenticatedPeerObservation` at
each local boundary; a wire fingerprint or copied connection number is not a
replacement for those objects.

`SO_PEERCRED`, boot ID and process birth remain useful provenance and mismatch
checks. They must not be promoted to an X server incarnation: socket credentials
can describe an inherited listener's creator. Local observer incarnation and
revision counters also cannot be compared across processes. See
[Linux Unix-domain socket credentials](https://man7.org/linux/man-pages/man7/unix.7.html).

## X11 namespace witness and publication

Use the existing injection connection in `XWindowsComputer` and the existing
`QNativeInterface::QX11Application::connection()` in `Viewer`. Do not open a
second display, replace event masks, consume another owner's event queue, change
its XI protocol version or silently select a master pointer.

1. The Mac coordinator captures the exact selected-client ticket/selection and
   WindowShare connection/focus/session binding. It starts one monotonic request
   deadline and sends `CoordinateWitnessCreate(request)` over that Deskflow
   client connection. Never locate this recipient by name, address or dimensions.
2. The Linux core generates a random 256-bit witness, writes it to a property on
   its own private unmapped InputOnly window through the injection connection,
   and completes a checked request/reply barrier. It replies with the witness,
   window XID, protocol property/type, local connection generation, actual
   root/screen and XI master pair, plus a fresh native observation. A single
   private window and fixed atom names avoid unbounded server resource creation.
3. The Mac sends only the request and witness locator to the current Viewer over
   WindowShare TLS. **Do not send the expected witness value.** Viewer reads the
   property through Qt's actual connection, checks its type/format/exact length
   and the witness window's root, and returns the bytes with its current native
   and painted viewport observation. An old or other-server XID is insufficient.
4. Accept only an exact outstanding request, matching secret, matching real
   root/screen/master pair and matching complete canonical topology digest, with
   both local connection/selection tokens still live. Core confirms the witness
   and its observation have not changed before acceptance. The host then assigns
   a random coordinate-space identity and its own layout revision to this
   correlated pair. Equal local revision counters are neither required nor proof.
5. Renew with a fresh witness and deadline. Overwriting/destroying the witness,
   disconnect, backend disable, device/root/layout change or Viewer binding
   change withdraws the binding. A renewal never extends retained observations.
   Start the existing 250 ms geometry budget at the host's request, not receipt;
   a slow exchange simply fails. No cross-machine monotonic-clock comparison.

This is a live namespace rendezvous under the stated desktop trust boundary.
It differentiates independent same-sized X servers even when XIDs and listener
credentials coincide. It is not a cryptographic claim that arbitrary X clients
could not bridge two namespaces.

The payload should be bounded, versioned and typed. Include request/connection
generation, server incarnation/recipient/selection generation, session/epoch,
source geometry, focus request/revision, surface incarnation/XID, viewport
revision, XI pair, topology digest, chosen output, native root/output/client
rectangles, painted client/video/image sizes and exact reduced rational DPR.
Keep each endpoint's observation revision separately. Canonical topology stays
bounded locally; transmit its SHA-256 plus the mapping fields, rather than
base64-expanding its possible 64 KiB into a 64 KiB control envelope.

For Deskflow 1.8 compatibility, advertise an opt-in coordinate-extension version
in a new `DSOP` option; existing option readers ignore unknown IDs. A new client
sends a capability acknowledgement only after that advertisement. Only then may
either side use a new length-bounded extension envelope with create/readback/
withdraw/fence/point subtypes. Test this negotiation against legacy parsers:
unknown four-byte message codes currently discard available input and are not a
safe capability probe. Reset-options and reconnect revoke the negotiation.
WindowShare similarly negotiates a coordinate capability in Hello/Accepted;
adding a message enum alone is insufficient. Proposed names are not frozen wire
numbers. Normal non-TLS clients and unsupported peers retain existing behavior
without acquiring coordinate authority.

## Original sequence and pointer ownership

The current path loses the association that delivery needs. Capture records have
epoch/sequence, but `IPrimaryComputer::{Motion,Button,Wheel}Info` do not.
`Server::onMouseMoveSecondary` applies adjustment, switching and clamping, while
`ServerProxy` may compress motion before `XTest`/`XWarpPointer`. Qt mouse events do
not carry the source record token. Timestamp proximity, current `Server::m_x/y`,
X request serials or a latest cached cursor position cannot join these streams.

For transparent overlapping windows, use `SequenceValidatedViewer` semantics:

1. Carry capture epoch/sequence through the existing core event path to the
   actual Server routing/absolute-coordinate decision. Publish the point and
   selected recipient with that exact record. Explicitly account for ignored,
   zero-motion, filtered and switch events; do not assume one captured record
   always produces one legacy event. Reject relative/locked-pointer mode initially.
2. Enter a generation-bound pointer fence on both the Linux injection path and
   Viewer. Release existing owners, require no held buttons, stop new legacy
   button/wheel injection, and suppress queued/new Qt forwarding. Retain
   tombstones through physical ups. This must complement the existing keyboard
   fence, which intentionally leaves pointers live today. A core socket flush
   alone does not prove that old native events have been consumed by Qt: the
   endpoint barrier and its native-queue ordering need an actual regression.
3. After the ordered cutover, original records travel once to the Mac host.
   Associate each with its exact routed root point and send a bounded
   `ValidatePoint(route, captureEpoch, sequence, kind, point, binding)` request
   to Viewer. Preserve order; overflow, missing association or timeout cancels
   instead of guessing. This sideband carries the token directly and does not
   try to recover it from an injected Qt event.
4. On Qt's owner thread, validate the fresh binding and painted geometry, then
   ask the native X server whether the point belongs to that viewer's client
   surface through the current stacking/input-shape hierarchy. Root-to-client
   rectangle containment alone misses occluders and decorations. Use a bounded
   native traversal with consistency checks; unsupported hierarchy or an
   intervening invalidation denies. The X server's coordinate translation
   considers mapped children and bounding/input shapes; see
   [Xlib coordinate translation](https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html)
   and the reference server's `ProcTranslateCoords` in
   [Xorg server 21.1.12](https://www.x.org/releases/individual/xserver/xorg-server-21.1.12.tar.xz).
5. A reply validates one exact sequence, point and binding, with the host's
   original deadline still live. Only then may the provider construct
   `ViewportDeliveryValidation` and map/deliver the original event. New presses
   and wheel events in letterboxing or behind another native input surface deny.
   Already-owned drag/up use their exact press owner and stored endpoint; after
   revocation cleanup must not retarget through a new mapping.
6. On leave, focus/geometry/layout loss, return or disconnect: revoke first,
   drain the exact native owner, then acknowledge Viewer/legacy resume. Never
   fall back by injecting a denied press into whichever Linux app is now under
   the cursor. Motion for displaying the Linux cursor may continue through an
   explicitly ordered motion-only path; those Qt events remain non-authorizing.

The native validation is a bounded observation at a named input sequence, not
an atomic transaction extending across the network and compositor. Recheck
local lifetime/deadline at use, process invalidation immediately, and state the
250 ms maximum stale-observation window. X11 input targeting, not arbitrary
compositor visual effects, defines this first supported hit-test model.

`FixedNativeOwner` is deployable for an explicit captured-window interaction,
but is not a shortcut for normal overlapping-window behavior. An acknowledged
`XIGrabDevice` with `owner_events=false` directs the named master's events to the
grab window and prevents those events reaching unrelated local apps. It also
continues through occlusion: viewable/focused/grabbed does not mean unobscured.
A new press/wheel still needs native hit validation if occluded viewers must not
receive it. Only an existing owned drag may continue outside. Never use a
synchronous device freeze as a long network wait, or replay rejected events
after ungrab. The [XI2 grab contract](https://www.x.org/releases/current/doc/inputproto/XI2proto.txt)
also requires handling failed grabs, device changes and automatic ungrab when
the window becomes unviewable. Prefer the explicit sequence-validation path
above for the first normal-window implementation.

## Production paths and acceptance tests

| Boundary | Required production change |
| --- | --- |
| Native witness | Add bounded witness operations beside `X11DisplayObserver`; wire the actual `XWindowsComputer` and `Viewer` connections. Preserve their event owners. |
| Deskflow publication | `ProtocolTypes`, `OptionTypes`, `Client`/`ServerProxy`, `ClientProxy1_0` and `Server`: negotiated messages, bounded decoding, exact TLS-ticket association and revoke-on-disconnect. |
| Core to Qt owner | Extend the selected-peer publication/coordinator boundary with retained coordinate evidence; no borrowed proxy or cross-thread native X calls. |
| WindowShare | `Core` message validation, `Transport`/Bridge negotiation, Viewer witness/viewport responses and exact connection-generation guards. |
| Ordered pointer route | `OriginalInputCaptureSource`, `OSXComputer`, `IPrimaryComputer` event metadata, Server routing, Session/Preflight and Viewer pointer fence. No extra SPSC consumer. |
| Actual delivery | Reviewed native point validator plus `ViewportLease` production factory, exact sequence/press ledger and NativeHost delivery. A successful witness cannot call this factory alone. |

Keep the next implementation connected: actual TLS/core proxy -> actual XCB
witness -> actual Viewer reply -> host acceptance/withdrawal. Then add the
sequence/fence path before enabling original pointer delivery. Do not label
diagnostic coordinate publication as a working original-input route.

Private authenticated Xvfb tests should cover two same-sized servers/root-XID
aliases, two connections on one server, wrong root/master, server restart,
destroyed/replaced witness, wrong property type/length, stale/replayed replies,
disconnect and same-size RandR changes. Test normal and fractional Qt scaling,
letterboxing, reparenting, an overlapping input window and shaped input regions.
Core/real-TLS tests must cover legacy negotiation, pointer sequences split across
queues, compressed/ignored motion, selection switch, delayed validation, resume
before old Qt events drain, held drag cleanup and no duplicate Linux/host click.
No new press may reach either the occluded source or an unrelated Linux window.
Xvfb does not validate real compositor transforms or GNOME Wayland behavior.

## GNOME Wayland is a separate provider

The actual Ubuntu target runs GNOME Wayland. Its Deskflow injection backend uses
EIS, whose regions may deliberately differ from desktop pixel geometry.
`ei_region_get_mapping_id()` can pair a region with a ScreenCast stream, but does
not identify a Qt toplevel's global placement or input hit target. Current
`PortalRemoteDesktop` requests `XDP_OUTPUT_NONE`, so even stream/region pairing
is not present. See [libei regions](https://libinput.pages.freedesktop.org/libei/api/structei__region.html),
[portal mapping IDs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
and [EI region mapping](https://libinput.pages.freedesktop.org/libei/interfaces/ei_device/index.html).

For ordinary movable, overlapping windows, implement a versioned GNOME/Mutter
provider (potentially a narrowly scoped Shell extension) that binds the actual
EIS session/device/region, viewer native surface and compositor input target,
and returns ordered validation/invalidation to the authenticated coordinator.
Its concrete supported API and permission model still require investigation;
no such provider exists in this tree. A combined RemoteDesktop/ScreenCast portal
session can establish region-to-stream mapping, but cannot alone supply that
surface/hit-test contract. Standard [Wayland pointer events](https://wayland.freedesktop.org/docs/html/apa.html)
provide surface-local coordinates and serials, not our original capture token.

An explicit fixed-owner relative-control mode is another possible product scope,
with compositor-supported pointer constraints and deliberate capture/release;
it must not be presented as transparent free-moving windows. Qt global positions,
Xwayland root bounds, monitor names and equal EIS rectangles are not fallbacks.
Until the compositor provider or that explicit interaction is implemented, the
GNOME Wayland original-pointer route remains unavailable even when X11 tests pass.
