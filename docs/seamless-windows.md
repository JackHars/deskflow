# Seamless Windows: foundation milestone

Status: **foundation only, not an end-to-end window-sharing application**.
This commit does not change normal Deskflow input handling or its default build.
It does not yet let a user drag or display Mac windows on Ubuntu.

## Published components

- C++20 session lifecycle with journal-before-export and first-frame gates.
- Explicit session epochs and geometry revisions for stale-input rejection.
- Input ownership ledger: a held key/button retains its original owner.
- Focus request/acknowledgment primitives.
- Versioned, bounded, endian-independent packet framing and replay rejection.
- Logical-coordinate mapping, letterbox rejection and recovery bounds handling.
- Qt TLS transport requiring an explicitly approved SHA-256 leaf-certificate
  fingerprint in both directions. No trust-on-first-use and no plaintext fallback.
- Owner-only identity directory, private-key permission checks and an
  overwrite guard when generating local test identities.
- Portable core tests and real-socket Qt transport tests.

`NativeHost.h` is an interface for later platform work. The non-Mac stub is not
an implementation. Neither is linked into the foundation library.

## Build and test the core (no Qt required)

From the repository root, with CMake 3.24+ and a C++20 compiler:

```sh
cmake -S src/unittests/windowshare -B build/windowshare-core \
  -DWINDOWSHARE_SANITIZERS=ON
cmake --build build/windowshare-core --parallel
ctest --test-dir build/windowshare-core --output-on-failure
```

The test executable performs 20,139 assertions, including 10,000 seeded
geometry/parser mutation iterations. That count is not a claim of 20,139
independent integration tests.

## Build and test the transport

Requires Qt 6.4+ Core/Network, a working Qt TLS backend, and the `openssl` command
for generating temporary test certificates. Existing Deskflow builds may impose
a newer Qt requirement independently.

```sh
cmake -S src/apps/windowshare -B build/windowshare-transport
cmake --build build/windowshare-transport --parallel
ctest --test-dir build/windowshare-transport --output-on-failure
```

The socket tests exercise an approved pair, a wrong server pin, a wrong client
pin, malformed fingerprints, and refusing to overwrite an existing identity.
They do not capture a desktop, inject input, or connect to the user's machines.

## Validation recorded during authoring

- GCC 14: core tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- Clang 17: the same core checks passed with both sanitizers.
- Both builds enabled `-Wall -Wextra -Wpedantic -Werror`.
- Qt and Apple SDKs were unavailable locally. Qt socket tests, native Mac
  compilation and two-machine interactive behavior were not locally validated.
- Added CI configuration is not itself evidence that the jobs passed. Consult
  the results associated with the commit before relying on them.

## Unpublished and unfinished work

A separate experimental Mac capture host and Ubuntu viewer were authored, but
publication of the write containing those files was blocked by a tool-level
safety check. Those sources are not part of this committed milestone. Do not
assume a `deskflow-windowshare` executable exists here.

The intended next milestone is a reviewed and hardware-tested single-window
vertical slice. It must verify capture, explicit consent, focus ownership,
loop-free input handling and safe return before automatic edge dragging.

Still required for the complete feature: integration with Deskflow's input and
display topology, virtual-display lifecycle, native viewers, transactional drag
handoff, compositor-specific placement, robust dialogs/menus, crash recovery,
reconnection, hardware video encoding and packaged applications.

A synthetic-event marker is an input-loop bookkeeping aid, never authentication.
Peer authentication belongs at the transport boundary. Do not bypass macOS
capture or Accessibility permissions, protected input, or lock-screen controls.
