<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# H07: ordinary titlebar dragging in both directions

Status: **NOT_RUN**. The user deferred physical testing. This procedure is for
the next attended session; builds, fixtures and cleanup alone cannot pass it.

## Freeze and configure

Record the implementation commit, source hashes, Mac Core/Host/helper/source
binary hashes, Linux Core/Viewer hashes, GNOME image identity, Qt versions,
provider ordering support and both physical monitor layouts. Use separately
packaged experimental applications and private test settings, trust, identities,
IPC paths and recovery journal. Follow the preparation and admission requirements
in [original-input-physical.md](original-input-physical.md).

H04's existing runner automatically exports its source and deliberately validates
a configuration with no edge links. Do not use its exit status as an H07 result.
For H07, prepare a distinct attended session with these actual runtime settings:

- Mac Core uses `--windowshare-host-config=/absolute/private/host.json` in server
  mode. The v1 Host configuration enables `holdingDisplay:true`, names the
  exact approved WindowShare peer and explicit Core-pin/WindowShare-pin pair.
  Core starts its sealed companion through the inherited private v9 channel.
- Linux Core uses `--windowshare-gnome-config=/absolute/private/gnome.json` in
  client mode. The v1 provider configuration names the actual Viewer executable,
  connection, identity and Mac Host pin. **Omit `exportWindow`**: the ordinary
  titlebar drag must be what creates the first export. Keep the Viewer owned by
  the real GNOME provider, with its actual private Wayland/viewport descriptors.
- Add reciprocal links to the private Deskflow server configuration, using the
  real test screen names, for example Mac `right = Ubuntu` and Ubuntu
  `left = Mac`. Preserve the explicit Control+Option+Shift+Left emergency return
  hotkey and the separate local Stop control. Start with default edge delays
  and no corner locks, then test configured policy separately.
- Use one independently identified disposable ordinary AppKit source and a
  distinct local control window. Keep the source fully on a physical display
  initially. Only the source is dragged. The source and controls must remain
  associated with their original process birth and native IDs throughout.

The verifier must confirm the visible control window first, then obtain fresh
readiness before starting capture/routing. Retain a bounded session deadline
and cleanup supervision outside the source window, since that source will move.
Do not reuse an old physical preparation or readiness reply.

## Physical cases

For every row retain human observations separately from native frames, Core
route/sequence evidence, source/Viewer identities and cleanup results. Native
grab metadata does not identify the originating device. Do not infer button
delivery or a successful focus change solely from a preview acknowledgement.

| Case | Action | Required result |
| --- | --- | --- |
| Outbound | Hold the source's normal top titlebar, cross the configured edge, continue moving, then release once. | A live provisional Viewer follows the same drag without selection/thumbnail steps. The original Mac down/up owner is retained. Only after native placement and paint, real up and journaled source relocation does Core select Ubuntu. The native Viewer frame/association must survive commit. |
| Return | Grab that Viewer's normal titlebar, cross the reciprocal edge, continue moving, then release once. | A non-activating Mac preview follows the original grip. Ubuntu gets its original up exactly once; GNOME ends the retained move grab. The exact original Mac source appears at the final preview pose, gains focus and retains its application state. Its remote Viewer retires and the cursor returns to Mac. |
| Cancel while held | Cross an edge, then move back across that entry edge before releasing. Repeat in both directions. | The provisional preview disappears, movement resumes with the original recipient and the eventual real release is delivered once. No export/restore is committed. |
| Cancel by key/extra press | Press Escape, or another control, during a held preview. | The movement gate retires; the original recipient still receives the actual button release. No invented release, stuck button or later commit occurs. |
| Missing first frame | Delay the initial preview frame long enough to exceed the preparation budget, then allow it to arrive. | The source remains recoverable under its original ownership; late paint cannot revive the attempt. |
| Peer loss | Disconnect only the recorded test connection during preparation, held movement and release/drop. Use a fresh run for each point/direction. | Pending transfer retires. Attempted native mutation is restored or retained in the exact recovery journal. Reconnection cannot revive the old press or grab. |
| Topology/source change | Change a configured edge/output, resize/minimize/close the exact source, or revoke an applicable native prerequisite during the attempt. | Identity/layout/geometry changes cancel the old attempt. No unrelated window is moved and no stale destination is committed. Record a recovery requirement as a failure, not a pass. |
| New input after up | Begin another interaction while the previous drop is still pending. | The producer fence prevents an old drop from transferring that newer interaction. If restoration already committed, the source remains locally recoverable and no duplicate button is posted. |
| Repeat | Export and return the same source repeatedly; then repeat with more than 16 successive source-window incarnations. | Window identity remains exact; retired watchers do not exhaust the 16-concurrent-window bound or retain old movement. |

While dragging, assess cursor visibility, grip continuity, preview placement,
duplicate/stale imagery and perceived interruption explicitly. A correct final
window location does not by itself satisfy the seamless interaction requirement.
Repeat with negative coordinates and mixed scales only when those real displays
are present. Native/browser/Electron titlebar variants remain separate H08 cases.

## Finish and record

Release all controls, return to the local controls, then stop the owned test Core
through its private IPC. Verify the exact source is visible, the recovery journal
is empty while Host is still alive, owned processes and holding display are gone,
no buttons/keys remain held and ordinary settings/processes are preserved. If
restoration failed, retain the journal and use the documented exact-package
recovery path; never delete the evidence to make cleanup appear successful.

Update H07 in `TESTING.md` only with the specific passing cases and source/binary
binding. Keep unrun monitor/application variants explicit. Existing automated
coverage and private GNOME tests are described in
[window-drag-preparation.md](window-drag-preparation.md).
