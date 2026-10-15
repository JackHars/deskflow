<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Experimental Linux archive: staging and verification

This is a **reproducible, inert source/runtime staging archive**, not an
installer. It does not modify an existing Deskflow installation, grant input
access, change system Qt, or install/enable the experimental GNOME extension.
Only explicitly selected source-build binaries are packaged. The GNOME provider
is included as disabled source under `share/deskflow-seamless-experimental`.

## Generate and verify

Build `deskflow-windowshare` (standalone `WINDOWSHARE_APP=ON`) and the real
opt-in `deskflow-core` (`BUILD_WINDOWSHARE=ON`) from the **same checked-out
revision**. Do not use objects from earlier incompatible builds.

The experimental Core requires **libei 1.5.0 or later**, both when building and
at runtime. Older unpatched releases can dereference a missing connection when
an EI context is destroyed before its EIS handshake finishes. The actual
startup/shutdown regression remains enabled; the opt-in configuration rejects
older versions. Ordinary Deskflow's build dependency policy is unchanged.
See [upstream fix 12237f19](https://gitlab.freedesktop.org/libinput/libei/-/commit/12237f19).

```sh
python3 deploy/windowshare/package-linux-tests.py
python3 deploy/windowshare/package-linux.py \
  --viewer build/app/deskflow-windowshare \
  --core build/integrated/bin/deskflow-core \
  --source-sha "$(git rev-parse HEAD)" \
  --output build/DeskflowSeamless-Linux-Experimental.tar.gz
python3 deploy/windowshare/package-linux.py \
  --verify build/DeskflowSeamless-Linux-Experimental.tar.gz
sha256sum build/DeskflowSeamless-Linux-Experimental.tar.gz
```

Successful `windowshare-foundation.yml` Linux jobs retain this verified archive
for seven days as `windowshare-linux-<commit>`, together with `SOURCE_COMMIT`
and `SHA256SUMS`. Download it from that exact Actions run and check the sums
before staging. The sibling `windowshare-macos-adhoc-<commit>` artifact contains
the two verified Mac ZIPs, signed ad hoc by CI. Those Mac artifacts do not carry
the local Apple-issued identity or its exact-library admission policy; rebuild
and sign locally for the attended production-input tests. Neither upload grants
permissions or installs a package on either machine.

The output parent directory must already exist. The destination archive must
**not** exist: the writer uses exclusive creation and refuses to overwrite
existing files/symlinks. A failed archive write removes only its own partially
created artifact. Archive entry order, metadata, gzip timestamp and filename
are fixed, so the same binary bytes, provider bytes and exact source SHA produce
the same archive bytes.

The internal `MANIFEST.json` records a SHA-256, size and POSIX mode for every
regular member plus the source commit and inert capabilities. The verifier
rejects duplicate/unlisted members, links, traversal, oversized members and
hash/mode mismatches. It does not cryptographically authenticate the manifest:
record and independently compare the archive's published SHA-256 and source Git
commit. A bundle built from stale local objects can still claim a newer Git
commit if its operator supplies the wrong `--source-sha`; the CI source checkout
and build logs remain the authority.

## Installation boundaries

Extract **only into an intentionally selected new staging directory** for
inspection. Two wrapper scripts, `bin/deskflow-seamless-viewer` and
`bin/deskflow-seamless-core`, invoke binaries retained under the
`libexec/deskflow-seamless-experimental` directory with their original basenames.
Neither wrapper executes at archive creation or extraction. No ordinary
`/usr/bin/deskflow-core` or application settings are changed.

System runtime libraries are not bundled. Verify compatible ABI, Qt/QtWayland
private headers/runtime, FFmpeg, XCB/Wayland, libei/GIO and the target desktop
before launch. The GNOME observation extension is **not enabled** by extracting
the archive; its version-pinned, user-initiated setup is described in
[`gnome-provider/README.md`](gnome-provider/README.md). It does not provide
original input delivery or queue-drain authority.

This package does not substitute for an installation/uninstallation smoke on
the actual Ubuntu host, runtime SONAME verification, two-monitor Wayland
testing, or hardware acceptance H01-H12. Those remain required in
[`TESTING.md`](../../TESTING.md).
