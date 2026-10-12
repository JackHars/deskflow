# Seamless Windows work state

Base: `JackHars/deskflow`, `master`,
`3aec96d57eda8851e3ca534400962cd9d71357ac`.

## Committed milestone

Portable session/protocol/geometry/input-ownership foundation, mutually pinned
Qt TLS transport, standalone builds, core and transport tests, CI configuration,
and an accurate status document in `docs/seamless-windows.md`.

The original Deskflow runtime and default build remain unchanged. This is not a
finished window-sharing feature and no application was installed on either of
the user's computers.

## Authoring validation

Core tests passed under GCC 14 and Clang 17 with ASan/UBSan and strict warnings:
20,139 assertions per compiler. Qt/Apple SDKs were not present locally, so the
Qt socket suite and native compilation are not claimed as locally verified.
Check actual CI results separately.

## Publication boundary

The write adding the experimental Mac adapter and Ubuntu viewer was blocked by
a tool-level safety check. It was not published by retrying through another
encoding or write route. Those files, the native executable and its integration
hooks are NOT part of this commit. The NativeHost declaration/stub is only an
interface placeholder and is not linked by the foundation build.

## Remaining milestones

1. Review and validate a single-window capture/viewer implementation.
2. Integrate explicit focus/input ownership without changing default Deskflow.
3. Prove safe restore/disconnect behavior on the actual Intel Mac and Ubuntu PC.
4. Add compositor-specific placement and transactional drag out/back.
5. Complete menus/dialogs, reconnect, crash watchdog, media performance and UX.

Do not turn a successful core unit test into a claim that the native bridge,
actual desktop input, or seamless dragging works. No subagents were used.
