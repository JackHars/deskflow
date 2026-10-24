#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Build/verify a reproducible *inert* experimental Linux source/runtime archive.

This never installs an executable, enables a GNOME extension, bundles unreviewed
system libraries, or alters an ordinary Deskflow installation. The controls offer explicit installation/enablement after a human selects the
supported Qt/GNOME desktop; extracting an archive performs neither action.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tarfile

PACKAGE_ROOT = "deskflow-seamless-linux-experimental"
RUNTIME = "libexec/deskflow-seamless-experimental"
SHARE = "share/deskflow-seamless-experimental"
PROVIDER = f"{SHARE}/gnome-provider"
PROVIDER_FILES = (
    "README.md", "extension.js", "interface.xml", "launcher.js", "logic.js", "ordering.js", "window-drag.js",
    "metadata.json", "metadata.json.license", "package.json", "package.json.license",
    # Offline provenance and explicit diagnostics are inert; the runtime uses
    # only the closed allowlist in logic.js, never this evidence manifest.
    "ordering-builds.json", "ordering-builds.json.license", "verify-ordering-build.py",
    "tests/ordering-native.js", "tests/logic.test.js", "tests/window-drag.test.js", "tests/launcher.gjs.js", "tests/launcher-native.py",
)
DOCUMENT_FILES = ("package-linux.md", "app-compatibility.md", "focused-clipboard.md", "seamless-profile.md")
SESSION_FILES = ("seamless-profile.py", "seamless-provider.py")
MAX_ARCHIVE_MEMBER = 128 * 1024 * 1024
MAX_ARCHIVE_TOTAL = 256 * 1024 * 1024


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def executable(path: Path) -> bytes:
    # No symlinks, even when their target happens to have the right ELF header.
    if path.is_symlink() or not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError(f"Binary must be a regular, non-symlink file: {path}")
    if not path.stat().st_mode & 0o111:
        raise ValueError(f"Binary is not executable: {path}")
    if path.stat().st_size > MAX_ARCHIVE_MEMBER:
        raise ValueError(f"Binary exceeds maximum package member size: {path}")
    data = path.read_bytes()
    if len(data) < 16 or not data.startswith(b"\x7fELF"):
        raise ValueError(f"Expected an ELF binary: {path}")
    return data


def launcher(binary: str) -> bytes:
    if binary not in ("deskflow-core", "deskflow-windowshare"):
        raise ValueError("Unexpected executable")
    return (
        "#!/bin/sh\n"
        "set -eu\n"
        'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        f'exec "$here/../{RUNTIME}/{binary}" "$@"\n'
    ).encode("utf-8")


def package_files(viewer: Path, core: Path, source: Path, source_sha: str) -> dict[str, tuple[bytes, int]]:
    if not re.fullmatch(r"[0-9a-fA-F]{40}", source_sha):
        raise ValueError("--source-sha must be the exact 40-digit Git commit hash")
    provider = source / "deploy/windowshare/gnome-provider"
    if provider.is_symlink() or not provider.is_dir():
        raise ValueError("Provider source directory missing or symlinked")
    files: dict[str, tuple[bytes, int]] = {
        f"{RUNTIME}/deskflow-windowshare": (executable(viewer), 0o755),
        f"{RUNTIME}/deskflow-core": (executable(core), 0o755),
        "bin/deskflow-seamless-viewer": (launcher("deskflow-windowshare"), 0o755),
        "bin/deskflow-seamless-core": (launcher("deskflow-core"), 0o755),
        "bin/deskflow-seamless-session": ((
            '#!/bin/sh\nset -eu\n'
            'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            f'exec python3 "$here/../{SHARE}/seamless-profile.py" "$@"\n'
        ).encode(), 0o755),
        "bin/deskflow-seamless-controls": ((
            '#!/bin/sh\nset -eu\n'
            'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            f'exec "$here/../{RUNTIME}/deskflow-windowshare" --session-controls "$@"\n'
        ).encode(), 0o755),
    }
    for name in PROVIDER_FILES:
        path = provider / name
        if path.is_symlink() or not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
            raise ValueError(f"Provider file missing or symlinked: {path}")
        if path.stat().st_size > MAX_ARCHIVE_MEMBER:
            raise ValueError(f"Provider file too large: {path}")
        files[f"{PROVIDER}/{name}"] = (path.read_bytes(), 0o644)
    metadata = json.loads(files[f"{PROVIDER}/metadata.json"][0])
    if metadata.get("uuid") != "seamless-windows@deskflow.org" or metadata.get("shell-version") != ["50"]:
        raise ValueError("GNOME extension identity or Shell 50 pin was changed")
    for name in DOCUMENT_FILES + SESSION_FILES:
        path = source / "deploy/windowshare" / name
        if path.is_symlink() or not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
            raise ValueError(f"Compatibility document missing or symlinked: {path}")
        if path.stat().st_size > MAX_ARCHIVE_MEMBER:
            raise ValueError(f"Compatibility document too large: {path}")
        files[f"{SHARE}/{name}"] = (path.read_bytes(), 0o644)
    manifest = {
        "schema": 1,
        "sourceCommit": source_sha.lower(),
        "kind": "inert-experimental-linux",
        "ordinaryDeskflowModified": False,
        "gnomeProviderEnabled": False,
        "bundledSystemLibraries": False,
        "requirements": [
            "Python 3.9 or newer for persistent session setup and launch",
            "Ubuntu system Python with python3-gi for GNOME support setup and readiness",
            "Linux x86_64 or matching build architecture and ABI",
            "runtime Qt version matching compiled Qt Wayland private APIs",
            "the build's FFmpeg, XCB/Wayland and GIO dependencies, and libei >= 1.5.0",
            "stable native GNOME Shell 50 for the optional observation provider; original input also requires "
            "the exact mapped Mutter/Clutter ordering build documented in gnome-provider/README.md",
            "mutually pinned private TLS identities and explicit user opt-in",
            "focused text clipboard defaults off and requires explicit policy on both peers",
            "capture-fps defaults to 30; 60 must be selected on the Mac host and is not measured delivery",
        ],
        "files": {
            name: {"sha256": digest(data), "bytes": len(data), "mode": f"{mode:04o}"}
            for name, (data, mode) in sorted(files.items())
        },
    }
    files["MANIFEST.json"] = ((json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode(), 0o644)
    if sum(len(data) for data, _ in files.values()) > MAX_ARCHIVE_TOTAL:
        raise ValueError("Archive exceeds bounded unpacked size")
    return files


def write_archive(output: Path, files: dict[str, tuple[bytes, int]]) -> None:
    # xb is exclusive and rejects both pre-existing archives and symlink targets.
    # Never create the parent directory or write into the ordinary installation.
    if output.parent.is_symlink() or not output.parent.is_dir():
        raise ValueError("Output parent must already be a non-symlink directory")
    created = False
    try:
        with output.open("xb") as stream:
            created = True
            with gzip.GzipFile(fileobj=stream, filename="", mode="wb", mtime=0, compresslevel=9) as zipstream:
                with tarfile.open(fileobj=zipstream, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    for path, (data, mode) in sorted(files.items()):
                        info = tarfile.TarInfo(f"{PACKAGE_ROOT}/{path}")
                        info.mode = mode
                        info.size = len(data)
                        info.uid = info.gid = info.mtime = 0
                        info.uname = info.gname = ""
                        archive.addfile(info, io.BytesIO(data))
    except Exception:
        # Only remove an archive that this process created with O_EXCL.
        # A failed open cannot delete another user's pre-existing path.
        if created:
            output.unlink(missing_ok=True)
        raise


def verify_archive(path: Path) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError("Package is missing or symlinked")
    contents: dict[str, tuple[bytes, int]] = {}
    total = 0
    with tarfile.open(path, "r:gz") as archive:
        for item in archive:
            if not item.isfile() or item.size < 0 or item.size > MAX_ARCHIVE_MEMBER:
                raise ValueError("Invalid or oversized package member")
            prefix = PACKAGE_ROOT + "/"
            if not item.name.startswith(prefix):
                raise ValueError("Unexpected package prefix")
            name = item.name[len(prefix):]
            if not name or name.startswith("/") or any(part in ("", ".", "..") for part in name.split("/")):
                raise ValueError("Unsafe archive member path")
            if name in contents:
                raise ValueError("Duplicated package member")
            if len(contents) >= 6 + len(PROVIDER_FILES) + len(DOCUMENT_FILES) + len(SESSION_FILES) + 1:
                raise ValueError("Too many package members")
            total += item.size
            if total > MAX_ARCHIVE_TOTAL:
                raise ValueError("Unpacked archive exceeds size limit")
            data = archive.extractfile(item).read()
            if len(data) != item.size:
                raise ValueError("Truncated package member")
            contents[name] = (data, item.mode)
    if "MANIFEST.json" not in contents:
        raise ValueError("Package manifest missing")
    manifest = json.loads(contents.pop("MANIFEST.json")[0])
    if (not re.fullmatch(r"[0-9a-f]{40}", manifest.get("sourceCommit", "")) or
            manifest.get("schema") != 1 or manifest.get("kind") != "inert-experimental-linux" or
            manifest.get("ordinaryDeskflowModified") is not False or
            manifest.get("gnomeProviderEnabled") is not False or
            manifest.get("bundledSystemLibraries") is not False):
        raise ValueError("Unexpected package policy")
    if set(manifest.get("files", {})) != set(contents):
        raise ValueError("Unlisted or missing package member")
    for name, info in manifest["files"].items():
        data, mode = contents[name]
        if (digest(data) != info.get("sha256") or len(data) != info.get("bytes") or
                f"{mode:04o}" != info.get("mode")):
            raise ValueError(f"Package member differs from manifest: {name}")
    # Checking hashes alone accepts an internally consistent but unlaunchable
    # archive whose manifest omits a runtime import or compatibility contract.
    required = {"bin/deskflow-seamless-viewer", "bin/deskflow-seamless-core",
                "bin/deskflow-seamless-session",
                "bin/deskflow-seamless-controls",
                f"{RUNTIME}/deskflow-windowshare", f"{RUNTIME}/deskflow-core"}
    required.update(f"{PROVIDER}/{name}" for name in PROVIDER_FILES)
    required.update(f"{SHARE}/{name}" for name in DOCUMENT_FILES + SESSION_FILES)
    if not required.issubset(contents):
        raise ValueError("Package missing an experimental runtime, provider, or compatibility diagnostic")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, help="Built Linux deskflow-windowshare ELF")
    parser.add_argument("--core", type=Path, help="Built opt-in deskflow-core ELF")
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2],
                        help="Repository root containing the inert GNOME provider")
    parser.add_argument("--source-sha", help="Exact tested source commit SHA")
    parser.add_argument("--output", type=Path, help="New, never-overwritten tar.gz path")
    parser.add_argument("--verify", type=Path, help="Read-only verify a staged archive")
    args = parser.parse_args()
    if args.verify:
        if any((args.viewer, args.core, args.source_sha, args.output)):
            parser.error("--verify cannot be combined with package creation")
        manifest = verify_archive(args.verify)
        print(f"VERIFIED {args.verify} source={manifest['sourceCommit']} sha256={digest(args.verify.read_bytes())}")
    else:
        if not all((args.viewer, args.core, args.output, args.source_sha)):
            parser.error("Building requires --viewer, --core, --output and --source-sha")
        files = package_files(args.viewer, args.core, args.source, args.source_sha)
        write_archive(args.output, files)
        verify_archive(args.output)
        print(f"PACKAGED {args.output} source={args.source_sha} sha256={digest(args.output.read_bytes())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
