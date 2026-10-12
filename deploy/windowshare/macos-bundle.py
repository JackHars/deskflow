#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Repair or audit a copied macOS bundle without launching any bundled code.

Repair uses only the explicitly supplied library roots, in order. Every copied
dependency is processed recursively. Non-system dependencies become paths
relative to their loader, so the final audit never needs the build machine's
Homebrew/Qt installations. Verify is read-only and also checks the signature.
"""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys


MACH_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}
LOAD_COMMANDS = {
    "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB",
}
# Public Security.framework CodeDirectory flags. These inspect the signed file;
# the live peer policy separately checks dynamic process status. No file audit
# can attest the exact loaded-image set or authorize original input.
CS_ADHOC, CS_LINKER_SIGNED = 0x2, 0x20000
CS_LIBRARY_VALIDATION, CS_RUNTIME = 0x2000, 0x10000


@dataclass(frozen=True)
class SigningFacts:
    team: str
    flags: int


def signing_facts(metadata, entitlements):
    teams = re.findall(r"^TeamIdentifier=(.*)$", metadata, re.MULTILINE)
    directories = re.findall(r"^CodeDirectory (.*)$", metadata, re.MULTILINE)
    flags = re.search(r"^.* flags=(0x[0-9a-fA-F]+)(?:\([^\n]*\))? hashes=", directories[0]) if len(directories) == 1 else None
    if len(teams) != 1 or not re.fullmatch(r"[A-Z0-9]{10}", teams[0]) or flags is None:
        raise RuntimeError("Missing or malformed signed Team ID or CodeDirectory flags")
    value = int(flags.group(1), 16)
    if value > 0xffffffff or value & (CS_ADHOC | CS_LINKER_SIGNED):
        raise RuntimeError("Ad-hoc or linker-only signatures cannot restrict library admission")
    if entitlements:
        try:
            claims = plistlib.loads(entitlements)
        except (ValueError, plistlib.InvalidFileException) as error:
            raise RuntimeError("Cannot read signed entitlements") from error
        if not isinstance(claims, dict) or claims:
            raise RuntimeError("Restricted library admission requires absent or empty entitlements")
    return SigningFacts(teams[0], value)


def inspect_signing(path, architecture):
    result = subprocess.run([
        "codesign", "--display", "--verbose=4", "--architecture", architecture,
        "--entitlements", "-", "--xml", str(path),
    ], capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError("Cannot inspect the signed library-admission policy: " + result.stderr.decode(errors="replace"))
    return signing_facts(result.stderr.decode(errors="strict"), result.stdout)


def command(*arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError("{} failed: {}".format(arguments[0], result.stderr.strip() or result.stdout.strip()))
    return result.stdout


def inside(path, directory):
    try:
        path.resolve().relative_to(directory.resolve())
        return True
    except ValueError:
        return False


def system_path(name):
    return name.startswith("/") and os.path.normpath(name).startswith(("/System/Library/", "/usr/lib/"))


def version(text):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", text):
        raise RuntimeError("Invalid macOS version: " + text)
    parts = tuple(int(part) for part in text.split("."))
    return parts + (0,) * (3 - len(parts))


def version_text(value):
    return ".".join(str(part) for part in (value if value[2] else value[:2]))


@dataclass
class MachO:
    path: Path
    dependencies: list
    rpaths: list
    identities: list
    minimum: tuple
    architectures: set
    uuids: set


def inspect(path):
    dependencies, rpaths, identities, minimums, uuids = [], [], [], [], set()
    for block in re.split(r"Load command \d+", command("otool", "-l", str(path))):
        match = re.search(r"^\s*cmd (LC_\w+)\s*$", block, re.MULTILINE)
        if not match:
            continue
        kind = match.group(1)
        if kind in LOAD_COMMANDS or kind == "LC_ID_DYLIB":
            name = re.search(r"^\s*name (.+) \(offset \d+\)\s*$", block, re.MULTILINE)
            if not name:
                raise RuntimeError("Cannot parse library command in " + str(path))
            (identities if kind == "LC_ID_DYLIB" else dependencies).append(name.group(1))
        elif kind == "LC_RPATH":
            name = re.search(r"^\s*path (.+) \(offset \d+\)\s*$", block, re.MULTILINE)
            if not name:
                raise RuntimeError("Cannot parse run path in " + str(path))
            rpaths.append(name.group(1))
        elif kind in {"LC_BUILD_VERSION", "LC_VERSION_MIN_MACOSX"}:
            value = re.search(r"^\s*(?:minos|version) ([0-9.]+)\s*$", block, re.MULTILINE)
            if value:
                minimums.append(version(value.group(1)))
        elif kind == "LC_UUID":
            value = re.search(r"^\s*uuid ([0-9A-Fa-f-]+)\s*$", block, re.MULTILINE)
            if value:
                uuids.add(value.group(1).upper())
    if not minimums:
        raise RuntimeError("Cannot determine the minimum macOS version of " + str(path))
    return MachO(
        path, list(dict.fromkeys(dependencies)), list(dict.fromkeys(rpaths)), list(dict.fromkeys(identities)),
        max(minimums), set(command("lipo", "-archs", str(path)).split()), uuids,
    )


def mach_files(bundle):
    result = set()
    for directory, _, names in os.walk(bundle):
        for name in names:
            path = Path(directory) / name
            if not path.is_file():
                continue
            with path.open("rb") as stream:
                if stream.read(4) not in MACH_MAGICS:
                    continue
            if not inside(path, bundle):
                raise RuntimeError("Mach-O symlink escapes the bundle: " + str(path))
            result.add(path.resolve())
    return sorted(result)


class Bundle:
    def __init__(self, path, search_roots=()):
        self.path = path.resolve()
        self.frameworks = self.path / "Contents/Frameworks"
        self.executables = self.path / "Contents/MacOS"
        self.plist_path = self.path / "Contents/Info.plist"
        if not inside(self.plist_path, self.path) or not inside(self.frameworks, self.path):
            raise RuntimeError("Bundle metadata or Frameworks directory escapes the bundle")
        with self.plist_path.open("rb") as stream:
            self.plist = plistlib.load(stream)
        name = self.plist.get("CFBundleExecutable", "")
        if not name or Path(name).name != name:
            raise RuntimeError("Bundle executable name is invalid")
        self.main = self.executables / name
        self.helper = self.executables / "deskflow-holding-display-helper"
        for executable in (self.main, self.helper):
            if not executable.is_file() or not inside(executable, self.path) or not os.access(executable, os.X_OK):
                raise RuntimeError("Missing or external bundle executable: " + str(executable))
        self.roots = list(dict.fromkeys(Path(root).resolve() for root in search_roots))
        for root in self.roots:
            if not root.is_dir():
                raise RuntimeError("Library search root is not a directory: " + str(root))
        self.main_info = inspect(self.main)
        self.origins = {}
        self.copied = []
        self.rewritten = 0

    def expand(self, name, loader):
        if name.startswith("@loader_path/"):
            return loader.parent / name[len("@loader_path/"):]
        if name.startswith("@executable_path/"):
            return self.executables / name[len("@executable_path/"):]
        if name.startswith("/"):
            return Path(name)
        return None

    def internal(self, name, info):
        if name.startswith("@rpath/"):
            candidates = []
            for loader, paths in ((info.path, info.rpaths), (self.main, self.main_info.rpaths)):
                for entry in paths:
                    base = self.expand(entry, loader)
                    if base is not None:
                        candidates.append(base / name[len("@rpath/"):])
        else:
            candidate = self.expand(name, info.path)
            candidates = [] if candidate is None else [candidate]
        for candidate in candidates:
            if candidate.is_file() and inside(candidate, self.path):
                return candidate.resolve()
        return None

    def source(self, name, info):
        candidates = []
        origin = self.origins.get(info.path, info.path)
        direct = self.expand(name, origin)
        if direct is not None:
            candidates.append(direct)
        suffix = name[len("@rpath/"):] if name.startswith("@rpath/") else Path(name).name
        if name.startswith("@rpath/"):
            for entry in info.rpaths:
                base = self.expand(entry, origin)
                if base is not None:
                    candidates.append(base / suffix)
        framework = re.search(r"([^/]+\.framework/.+)$", name)
        if framework:
            suffix = framework.group(1)
        for root in self.roots:
            candidates.append(root / suffix)
        for candidate in candidates:
            if candidate.is_file() and any(inside(candidate, root) for root in self.roots):
                return candidate.resolve()
        raise RuntimeError("{}: cannot resolve {} in the explicit library search roots".format(
            info.path.relative_to(self.path), name,
        ))

    def copy_library(self, source, name):
        # Fail before copying if a search result is not a usable Mach-O library.
        source_info = inspect(source)

        def deployed_copy(destination):
            if not destination.is_file() or not inside(destination, self.path):
                return False
            actual = destination.resolve()
            if self.origins.get(actual) == source:
                return True
            existing = inspect(actual)
            # macdeployqt changes install names and signatures, but preserves
            # the linker UUID of every slice. Reuse that same deployed build;
            # never silently substitute another library sharing its filename.
            if (source_info.uuids and source_info.uuids == existing.uuids and
                    source_info.architectures == existing.architectures):
                self.origins[actual] = source
                return True
            return False

        framework = next((parent for parent in source.parents if parent.name.endswith(".framework")), None)
        self.frameworks.mkdir(parents=True, exist_ok=True)
        if framework is not None:
            destination_root = self.frameworks / framework.name
            destination = destination_root / source.relative_to(framework)
            if destination_root.exists():
                if deployed_copy(destination):
                    return destination.resolve()
                raise RuntimeError("Conflicting framework destination: " + str(destination_root))
            shutil.copytree(framework, destination_root, symlinks=True)
            for copied in mach_files(destination_root):
                self.origins[copied] = framework / copied.relative_to(destination_root)
            self.copied.append(destination_root.relative_to(self.path).as_posix())
        else:
            destination = self.frameworks / Path(name).name
            if destination.exists():
                if deployed_copy(destination):
                    return destination.resolve()
                raise RuntimeError("Conflicting library destination: " + str(destination))
            shutil.copy2(source, destination)
            destination.chmod(destination.stat().st_mode | 0o200)
            self.origins[destination.resolve()] = source
            self.copied.append(destination.relative_to(self.path).as_posix())
        return destination.resolve()

    def repair(self):
        if not self.roots:
            raise RuntimeError("Repair requires at least one explicit --search-root")
        pending = mach_files(self.path)
        processed = set()
        while pending:
            path = pending.pop(0)
            if path in processed:
                continue
            processed.add(path)
            info = inspect(path)
            edits = []
            for dependency in info.dependencies:
                if system_path(dependency):
                    continue
                target = self.internal(dependency, info)
                if target is None:
                    target = self.copy_library(self.source(dependency, info), dependency)
                    pending.extend(file for file in mach_files(self.path) if file not in processed)
                relative = "@loader_path/" + os.path.relpath(target, path.parent)
                if dependency != relative:
                    edits.extend(("-change", dependency, relative))
                    self.rewritten += 1
            for entry in info.rpaths:
                expanded = self.expand(entry, path)
                # No current-directory or build-machine search paths survive.
                if entry.startswith("/") or expanded is None or not inside(expanded, self.path) or not expanded.is_dir():
                    edits.extend(("-delete_rpath", entry))
            if info.identities:
                identity = "@rpath/" + os.path.relpath(path, self.frameworks)
                if any(old != identity for old in info.identities):
                    edits.extend(("-id", identity))
            if edits:
                path.chmod(path.stat().st_mode | 0o200)
                command("install_name_tool", *edits, str(path))
        records = [inspect(path) for path in mach_files(self.path)]
        effective = max([version(str(self.plist.get("LSMinimumSystemVersion", "0.0")))] + [info.minimum for info in records])
        self.plist["LSMinimumSystemVersion"] = version_text(effective)
        with self.plist_path.open("wb") as stream:
            plistlib.dump(self.plist, stream, sort_keys=False)
        return self.verify(signature=False)

    def verify_restricted_library_admission(self, records):
        # This is a file/package audit, not live process evidence. The two
        # roles remain pinned separately at runtime, and original input stays
        # disabled until its independent admission/ordering providers exist.
        if not self.main_info.architectures or not self.main_info.architectures.issubset({"x86_64", "arm64"}):
            raise RuntimeError("Restricted library admission supports only x86_64 and arm64 executables")
        if version(str(self.plist.get("LSMinimumSystemVersion", "0.0"))) < (10, 14, 0):
            raise RuntimeError("Restricted library admission requires macOS 10.14 or newer")
        fixed_paths = {self.main, self.helper}
        for name in ("deskflow-core", "deskflow-windowshare"):
            candidate = self.executables / name
            if candidate.exists():
                fixed_paths.add(candidate)
        for path in fixed_paths:
            if path.is_symlink() or not path.is_file() or not inside(path, self.path):
                raise RuntimeError("A fixed executable must be a contained regular file: " + str(path))
        executables = {path.resolve() for path in fixed_paths}
        missing = executables - {info.path for info in records}
        if missing:
            raise RuntimeError("A fixed executable is not a contained Mach-O binary: " +
                               ", ".join(str(path) for path in sorted(missing)))
        main_facts = inspect_signing(self.main, sorted(self.main_info.architectures)[0])
        requirement = '=anchor apple generic and certificate leaf[subject.OU] = "{}"'.format(main_facts.team)
        for info in records:
            for architecture in sorted(info.architectures):
                facts = inspect_signing(info.path, architecture)
                if facts.team != main_facts.team:
                    raise RuntimeError("Bundled code has a different Team ID: " + str(info.path.relative_to(self.path)))
                required = CS_RUNTIME | CS_LIBRARY_VALIDATION
                if info.path in executables and facts.flags & required != required:
                    raise RuntimeError("Executable lacks hardened runtime or explicit library validation: " +
                                       str(info.path.relative_to(self.path)))
            command("codesign", "--verify", "--strict", "--all-architectures", "-R", requirement, str(info.path))
        return {
            "scope": "Apple-or-same-Team library admission", "team_id": main_facts.team,
            "executables": sorted(path.relative_to(self.path).as_posix() for path in executables),
            "exact_loaded_images_verified": False, "original_input_authorized": False,
        }

    def verify(self, signature=True, restricted_libraries=False):
        if restricted_libraries and not signature:
            raise RuntimeError("Restricted library admission cannot be audited before signing")
        records = [inspect(path) for path in mach_files(self.path)]
        binary_paths = {info.path for info in records}
        errors = []
        effective = max(info.minimum for info in records)
        declared = version(str(self.plist.get("LSMinimumSystemVersion", "0.0")))
        main_architectures = self.main_info.architectures
        for info in records:
            relative = info.path.relative_to(self.path)
            if not main_architectures.issubset(info.architectures):
                errors.append("{} lacks application architectures {}".format(relative, sorted(main_architectures - info.architectures)))
            for dependency in info.dependencies:
                if system_path(dependency):
                    continue
                target = self.internal(dependency, info)
                if dependency.startswith("/") or target is None:
                    errors.append("{}: dependency is missing or external: {}".format(relative, dependency))
                elif target not in binary_paths:
                    errors.append("{}: dependency is not a Mach-O binary: {}".format(relative, dependency))
            for entry in info.rpaths:
                expanded = self.expand(entry, info.path)
                if entry.startswith("/") or expanded is None or not inside(expanded, self.path):
                    errors.append("{}: run path escapes the bundle: {}".format(relative, entry))
            for identity in info.identities:
                if identity.startswith("/") and not system_path(identity):
                    errors.append("{}: library ID contains a machine-local path: {}".format(relative, identity))
        if declared < effective:
            errors.append("Info.plist declares macOS {}, but bundled Mach-O files require {}".format(
                version_text(declared), version_text(effective),
            ))
        signature_state = "not checked before signing"
        if signature:
            result = subprocess.run(["codesign", "--verify", "--deep", "--strict", str(self.path)], capture_output=True, text=True)
            signature_state = "valid" if result.returncode == 0 else "invalid"
            if result.returncode:
                errors.append("Signature verification failed: " + result.stderr.strip())
        admission = None
        if restricted_libraries:
            try:
                admission = self.verify_restricted_library_admission(records)
            except (RuntimeError, UnicodeError, ValueError) as error:
                errors.append("Restricted library admission failed: " + str(error))
        return {
            "bundle": str(self.path), "mach_o_files": len(records), "architectures": sorted(main_architectures),
            "minimum_macos": version_text(effective), "declared_minimum_macos": version_text(declared),
            "copied_libraries": self.copied, "rewritten_dependencies": self.rewritten,
            "signature": signature_state, "restricted_library_admission": admission, "errors": errors,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("repair", "verify"))
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--search-root", action="append", default=[], metavar="DIRECTORY")
    parser.add_argument("--restricted-libraries", action="store_true",
                        help="audit Apple-issued same-Team signatures, executable runtime/library flags and no entitlements")
    arguments = parser.parse_args()
    if arguments.mode == "verify" and arguments.search_root:
        parser.error("verify never uses external library search roots")
    if arguments.mode == "repair" and arguments.restricted_libraries:
        parser.error("restricted library admission must be verified after signing")
    try:
        bundle = Bundle(arguments.bundle, arguments.search_root)
        report = bundle.repair() if arguments.mode == "repair" else bundle.verify(
            restricted_libraries=arguments.restricted_libraries,
        )
        print(json.dumps(report, indent=2))
        return 1 if report["errors"] else 0
    except (OSError, RuntimeError, ValueError, plistlib.InvalidFileException) as error:
        print("Bundle audit failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
