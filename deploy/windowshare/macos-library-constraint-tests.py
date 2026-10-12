#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise exact-CDHash library constraints in disposable headless macOS code.

Requires an explicit Apple-issued signing identity and native Intel x86_64.
No Deskflow application, Qt, input hook, display helper or installed binary is
started or modified. All compiled/signed code lives in a private temporary
directory and is removed afterward. JSON results include execution evidence.

This tests permitted non-Apple library identities, not paths or an inventory of
loaded images. An explicit unlisted system-library test records the Apple/system
exception. No arm64, Rosetta, production admission or original-input authority
is established. A failing positive control invalidates the experiment.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "macos_library_admission", Path(__file__).with_name("macos-library-admission-tests.py"))
admission = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = admission
SPEC.loader.exec_module(admission)

ARCHITECTURE = "x86_64"
LIBRARY_IDENTIFIER = "org.deskflow.exact-library-fixture"
SYSTEM_LIBRARY = "/usr/lib/libsqlite3.dylib"
# dyld's diagnostic spelling varies; "contraint" is emitted by macOS 26.1.
# Require an explicit library-policy reason, never just a crash/nonzero exit.
POLICY_DENIALS = ("library load disallowed by system policy",
                 "library violates process' library load contraint",
                 "library violates process' library load constraint")
REQUIRED_FLAGS = admission.bundle_tool.CS_RUNTIME | admission.bundle_tool.CS_LIBRARY_VALIDATION

TRANSITIVE = r"""
#include <unistd.h>
__attribute__((constructor)) static void loaded(void) {
  static const char marker[] = "TRANSITIVE_CONSTRUCTOR\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
}
int transitive_value(void) { return 40; }
"""
PARENT = r"""
#include <unistd.h>
extern int transitive_value(void);
__attribute__((constructor)) static void loaded(void) {
  static const char marker[] = "LIBRARY_CONSTRUCTOR\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
}
int fixture_value(void) { return transitive_value() + 2; }
"""
SYSTEM_DLOPEN = r"""
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  static const char path[] = "/usr/lib/libsqlite3.dylib";
  static const char marker[] = "MAIN_STARTED\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
  void *previous = dlopen(path, RTLD_NOLOAD | RTLD_NOW);
  if (previous) {
    (void)dlclose(previous);
    fprintf(stderr, "System fixture already loaded; explicit admission was not exercised\n");
    return 76;
  }
  void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) { fprintf(stderr, "%s\n", dlerror()); return 73; }
  const char *(*version)(void) = (const char *(*)(void))dlsym(library, "sqlite3_libversion");
  Dl_info info;
  if (!version || !version() || !version()[0]) return 74;
  if (!dladdr((void *)version, &info) || !info.dli_fname || strcmp(info.dli_fname, path)) {
    fprintf(stderr, "SQLite symbol did not resolve to the fixed system library\n");
    return 77;
  }
  static const char success[] = "SYSTEM_NOT_PRELOADED\nSYSTEM_LOADED\n";
  (void)write(STDOUT_FILENO, success, sizeof(success) - 1);
  return dlclose(library) == 0 ? 0 : 75;
}
"""
SYSTEM_STARTUP = r"""
#include <unistd.h>
extern const char *sqlite3_libversion(void);
int main(void) {
  static const char marker[] = "MAIN_STARTED\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
  const char *version = sqlite3_libversion();
  if (!version || !version[0]) return 74;
  static const char success[] = "SYSTEM_LOADED\n";
  (void)write(STDOUT_FILENO, success, sizeof(success) - 1);
  return 0;
}
"""


def run(*arguments):
    environment = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
    return admission.run(*map(str, arguments), env=environment)


def require(result, purpose):
    if result.returncode:
        raise RuntimeError("{} (exit {}): {}".format(
            purpose, result.returncode, (result.stderr + result.stdout).strip()))
    return result


def native_intel():
    if sys.platform != "darwin" or platform.machine() != ARCHITECTURE:
        raise RuntimeError("Only native Intel x86_64 macOS is supported; arm64/Rosetta are not tested")
    translated = run("sysctl", "-n", "sysctl.proc_translated")
    hardware = run("sysctl", "-n", "hw.cputype", "hw.optional.x86_64", "machdep.cpu.vendor")
    # Native Intel can lack both proc_translated and hw.optional.arm64. Require
    # positive Intel CPU facts as well as denying any reported translation.
    if (hardware.returncode or hardware.stdout.splitlines() != ["7", "1", "GenuineIntel"] or
            (translated.returncode == 0 and translated.stdout.strip() != "0") or
            (translated.returncode and "unknown oid" not in translated.stderr.lower())):
        raise RuntimeError("Cannot establish native Intel execution; no Rosetta enforcement claim is permitted")


def inspect_signature(path, expected_identifier, team=None):
    architectures = require(run("lipo", "-archs", path), "Cannot inspect fixture architecture").stdout.split()
    if architectures != [ARCHITECTURE]:
        raise RuntimeError("Fixture must contain only x86_64: " + str(path))
    result = require(run("codesign", "--display", "--verbose=4", "--architecture", ARCHITECTURE, path),
                     "Cannot inspect fixture signature")
    # Verbose codesign writes a diagnostic constraint dump to stdout. It is not
    # XML entitlements; inspect those separately at the default verbosity.
    entitlements = require(run("codesign", "--display", "--architecture", ARCHITECTURE,
                               "--entitlements", "-", "--xml", path), "Cannot inspect fixture entitlements")
    facts = admission.bundle_tool.signing_facts(result.stderr, entitlements.stdout.encode())
    if facts.flags != REQUIRED_FLAGS:
        raise RuntimeError("Fixture must have exactly runtime+library flags: " + str(path))
    identifiers = re.findall(r"^Identifier=(.*)$", result.stderr, re.MULTILINE)
    hashes = re.findall(r"^CDHash=([0-9a-fA-F]{40})$", result.stderr, re.MULTILINE)
    if identifiers != [expected_identifier] or len(hashes) != 1:
        raise RuntimeError("Missing/ambiguous fixture identifier or native 20-byte CDHash: " + str(path))
    if team is not None and facts.team != team:
        raise RuntimeError("Fixture changed Apple signing Team: " + str(path))
    requirement = '=anchor apple generic and certificate leaf[subject.OU] = "{}"'.format(facts.team)
    require(run("codesign", "--verify", "--strict", "--all-architectures", "-R", requirement, path),
            "Fixture is not valid Apple-issued code for the expected Team")
    return {"team": facts.team, "identifier": identifiers[0], "cdhash": hashes[0].lower(), "flags": facts.flags}


def sign(path, identity, identifier, constraint=None):
    options = [] if constraint is None else ["--library-constraint", constraint, "--enforce-constraint-validity"]
    require(run("codesign", "--force", "--sign", identity, "--identifier", identifier,
                "--options", "runtime,library", *options, path), "Signing disposable fixture failed")


def write_constraint(path, team, hashes):
    if not hashes or len(hashes) != len(set(hashes)) or any(not re.fullmatch(r"[0-9a-f]{40}", value) for value in hashes):
        raise RuntimeError("Constraint must contain unique native 20-byte CDHashes")
    # Dictionary entries are ANDed: matching Team alone is insufficient.
    # plist <data>, not strings, is required for the cdhash $in operands.
    policy = {"team-identifier": team, "cdhash": {"$in": [bytes.fromhex(value) for value in sorted(hashes)]}}
    path.write_bytes(plistlib.dumps(policy, fmt=plistlib.FMT_XML, sort_keys=True))
    require(run("codesign", "--validate-constraint", path), "Canonical constraint rejected by codesign")


def check_execution(report, name, executable, arguments=(), allowed=True, startup=False, transitive=False,
                    system=False, denied_library=None):
    result = run(executable, *arguments)
    evidence = {"name": name, "expected_allowed": allowed, "exit_code": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    report["executions"].append(evidence)
    if system:
        expected = "MAIN_STARTED\n" + ("" if startup else "SYSTEM_NOT_PRELOADED\n") + "SYSTEM_LOADED\n"
    else:
        constructors = ("TRANSITIVE_CONSTRUCTOR\n" if transitive else "") + "LIBRARY_CONSTRUCTOR\n"
        expected = constructors + "MAIN_STARTED\n" if startup else "MAIN_STARTED\n" + constructors
    if allowed:
        if result.returncode or result.stdout != expected:
            raise RuntimeError("{} positive control failed (exit {}); fixture loading/order is not established. {}{}".
                               format(name, result.returncode, result.stderr, result.stdout))
    else:
        if "CONSTRUCTOR" in result.stdout or "CONSTRUCTOR" in result.stderr:
            raise RuntimeError(name + ": excluded code reached a constructor before rejection")
        if ((startup and (result.returncode == 0 or result.stdout)) or
                (not startup and (result.returncode != 73 or result.stdout != "MAIN_STARTED\n"))):
            raise RuntimeError("{}: unexpected rejection path (exit {}): {}{}".
                               format(name, result.returncode, result.stderr, result.stdout))
        if denied_library is None or not any(
                str(denied_library) in line and any(reason in line.lower() for reason in POLICY_DENIALS)
                for line in result.stderr.splitlines()):
            raise RuntimeError(name + ": rejection did not identify library policy for the omitted library; "
                               "refusing to count a broken "
                               "fixture/crash as enforcement. Diagnostic: " + result.stderr)
    report["passed"].append(name)
    print("PASS: " + name, file=sys.stderr, flush=True)


def exercise(identity, report):
    native_intel()
    if not identity or identity == "-" or identity.startswith("-"):
        raise RuntimeError("An explicit Apple-issued --signing-identity is required; ad-hoc signing is insufficient")
    report.update({
        "architecture": ARCHITECTURE, "native_architecture_only": True, "arm64_tested": False,
        "rosetta_tested": False,
        "macos_version": require(run("sw_vers", "-productVersion"), "Cannot read macOS version").stdout.strip(),
        "macos_build": require(run("sw_vers", "-buildVersion"), "Cannot read macOS build").stdout.strip(),
        "system_library": SYSTEM_LIBRARY, "scope": "permitted non-Apple native library identities; Apple/system exception",
        "exact_loaded_images_verified": False, "library_paths_restricted": False,
        "original_input_authorized": False, "production_bundle_changed": False,
    })
    with tempfile.TemporaryDirectory(prefix="deskflow exact library constraint ") as temporary:
        root = Path(temporary)
        sdk = require(run("xcrun", "--sdk", "macosx", "--show-sdk-path"), "Cannot locate macOS SDK").stdout.strip()
        report["sdk"] = sdk

        def compile_source(source, output, *arguments):
            source_path = output.with_suffix(".c")
            source_path.write_text(source)
            require(run("xcrun", "clang", "-arch", ARCHITECTURE, "-isysroot", sdk, "-Wall", "-Wextra", "-Werror",
                        "-mmacosx-version-min=14.0", source_path, "-o", output, *arguments),
                    "Disposable C fixture compilation failed: " + output.name)

        for directory in ("A", "B", "chain", "templates", "executables"):
            (root / directory).mkdir()
        libraries = {"A": root / "A/startup.dylib", "B": root / "B/startup.dylib",
                     "parent": root / "chain/startup.dylib", "transitive": root / "chain/transitive.dylib"}
        startup_name = "-Wl,-install_name,@executable_path/startup.dylib"
        compile_source(admission.LIBRARY, libraries["A"], "-dynamiclib", startup_name)
        compile_source(admission.LIBRARY + "\nint excluded_identity(void) { return 7; }\n",
                       libraries["B"], "-dynamiclib", startup_name)
        compile_source(TRANSITIVE, libraries["transitive"], "-dynamiclib",
                       "-Wl,-install_name,@loader_path/transitive.dylib")
        compile_source(PARENT, libraries["parent"], "-dynamiclib", startup_name, libraries["transitive"])
        parent_loads = require(run("otool", "-L", libraries["parent"]), "Cannot inspect transitive fixture").stdout
        if "@loader_path/transitive.dylib" not in parent_loads:
            raise RuntimeError("Parent fixture does not actually link its transitive dependency")

        facts, team = {}, None
        for name, path in libraries.items():
            identifier = LIBRARY_IDENTIFIER if name in {"A", "B"} else LIBRARY_IDENTIFIER + "." + name
            sign(path, identity, identifier)
            facts[name] = inspect_signature(path, identifier, team)
            team = facts[name]["team"]
        if facts["A"]["cdhash"] == facts["B"]["cdhash"]:
            raise RuntimeError("Excluded B must change code/hash while keeping A's Team and signing identifier")
        report["team"] = team
        report["library_signatures"] = facts
        policies = {"a-only": [facts["A"]["cdhash"]], "parent-only": [facts["parent"]["cdhash"]],
                    "full": [value["cdhash"] for value in facts.values()]}
        constraints = {}
        for name, hashes in policies.items():
            constraints[name] = root / (name + ".plist")
            write_constraint(constraints[name], team, hashes)
        report["constraint_hashes"] = policies

        templates = {name: root / "templates" / name for name in ("loader", "startup", "system-loader", "system-startup")}
        compile_source(admission.LOADER, templates["loader"])
        compile_source(admission.STARTUP, templates["startup"], libraries["A"])
        compile_source(SYSTEM_DLOPEN, templates["system-loader"])
        compile_source(SYSTEM_STARTUP, templates["system-startup"], "-lsqlite3")
        system_loads = require(run("otool", "-L", templates["system-startup"]), "Cannot inspect system startup fixture").stdout
        if SYSTEM_LIBRARY not in system_loads:
            raise RuntimeError("Explicit system startup dependency was not linked")
        executables = {}
        for policy in ("control", "a-only", "parent-only", "full"):
            for name, template in templates.items():
                if name.startswith("system-") and policy not in {"control", "a-only"}:
                    continue
                output = root / "executables" / (policy + "-" + name)
                shutil.copy2(template, output)
                identifier = "org.deskflow.exact-constraint-" + name
                sign(output, identity, identifier, constraints.get(policy))
                inspect_signature(output, identifier, team)
                executables[policy, name] = output

        def both(policy, library, allowed):
            prefix = policy + "-" + library
            denied = None if allowed else libraries["transitive" if library == "parent" else library]
            check_execution(report, prefix + "-dlopen", executables[policy, "loader"], [libraries[library]],
                            allowed=allowed, transitive=library == "parent", denied_library=denied)
            startup = libraries[library].parent / (policy + "-startup")
            shutil.copy2(executables[policy, "startup"], startup)
            check_execution(report, prefix + "-startup", startup, allowed=allowed, startup=True,
                            transitive=library == "parent", denied_library=denied)

        # Establish that these exact B/transitive bytes are usable under the
        # same flags, signer, identifier and entitlement policy. Only the added
        # constraint differs from the identical compiled-template controls.
        for library in ("A", "B", "parent"):
            both("control", library, True)
        both("a-only", "A", True)
        both("a-only", "B", False)
        both("parent-only", "parent", False)
        # Recovery with the complete non-Apple list distinguishes omissions from
        # broken linkage, covering both variants and the transitive chain.
        for library in ("A", "B", "parent"):
            both("full", library, True)
        for policy in ("control", "a-only"):
            check_execution(report, policy + "-unlisted-system-dlopen", executables[policy, "system-loader"], system=True)
            check_execution(report, policy + "-unlisted-system-startup", executables[policy, "system-startup"],
                            system=True, startup=True)
        report["system_exception_observed"] = True
    report["fixtures_removed_after_run"] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--signing-identity", required=True)
    arguments = parser.parse_args()
    report = {"passed": [], "executions": [], "success": False}
    try:
        exercise(arguments.signing_identity, report)
        report["success"] = True
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("Library constraint test failed: " + str(error), file=sys.stderr)
    print(json.dumps(report, indent=2))
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
