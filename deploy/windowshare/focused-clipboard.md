<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Focused exported-window clipboard

Text clipboard sharing is an independent, explicit WindowShare policy. It is
off by default and does not modify ordinary Deskflow clipboard configuration.
Both peers must opt in and negotiate `clipboard-text-v1` on their authenticated
WindowShare connection.

For standalone host and Viewer launches, add `--share-clipboard-text` on both
sides. For inherited launches, add the strict JSON boolean
`"shareClipboardText": true` to both the Mac `--windowshare-host-config` file
and Linux `--windowshare-gnome-config` file. Missing or false leaves sharing
disabled. Strings, numbers, duplicate fields and arbitrary injected arguments
are rejected. An option on only one peer does not enable clipboard access.

Only new plain-text clipboard changes of at most 8,192 UTF-8 bytes are sent.
Files, images, HTML, embedded NULs and malformed Unicode are not shared. There
is no clipboard-history transfer, read on enable, read on focus acquisition,
or simulated copy/paste keystroke. The user copies and pastes normally.

Every update names the exact export session/epoch, acknowledged focus request
and revision, painted geometry and increasing connection-local sequence.
Immediately before reading or writing, each side rechecks its retained native
or painted Viewer owner. Hidden/minimized/unpainted, unfocused, stale, returned,
disconnected or replaced windows cannot share. Native sheet/drawer return and
lifecycle revocation also invalidate the focus observation. Clipboard focus
does not grant original-input or compositor authority.

The controller suppresses synchronous and delayed clipboard echoes by digest;
it retains no unbounded message queue. A stale-focus update consumes its
sequence without writing, so it cannot later be replayed into a replacement
focus. Mac monitoring uses pasteboard change-count metadata and only enables
content reads through the same guarded controller after policy negotiation.
The sampled focus must stay live across the change-count read and the later
text read. Known focus/release/reconnect transitions establish a new metadata
baseline without reading text. These are observed-focus checks, not proof of
which application wrote the pasteboard: a background writer or an external
away-and-back entirely between observations cannot be excluded. See the
[application compatibility scenarios](app-compatibility.md).

`windowshare-clipboard` tests protocol limits, policy, stale focus, sequences,
echoes, reentry and destruction with private in-memory strings. Bridge tests
exercise negotiation and messages over disposable real TLS connections; Viewer
tests exercise painted/focus invalidation. These tests never access the user's
clipboard. H08/H09 must separately verify actual copy/paste on the final
packaged candidate, with disposable text and both policies explicitly enabled.
