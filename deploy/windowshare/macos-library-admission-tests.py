#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Verify macOS library admission with disposable headless code and a real signer.

This never starts Deskflow, NativeHost, Qt, an event tap or a display helper.
It signs only freshly compiled fixtures in a private temporary directory. The
tests prove Apple-or-same-Team library admission, not an exact image allowlist.
An explicit Apple-issued --signing-identity is required; ad-hoc CI signatures
cannot substitute for the positive OS enforcement tests.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("macos_bundle", Path(__file__).with_name("macos-bundle.py"))
bundle_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bundle_tool
SPEC.loader.exec_module(bundle_tool)

LIBRARY = r"""
#include <unistd.h>
__attribute__((constructor)) static void loaded(void) {
  static const char marker[] = "LIBRARY_CONSTRUCTOR\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
}
int fixture_value(void) { return 42; }
"""
LOADER = r"""
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
  static const char marker[] = "MAIN_STARTED\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
  if (argc != 2) return 2;
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) { fprintf(stderr, "%s\n", dlerror()); return 73; }
  int (*value)(void) = (int (*)(void))dlsym(library, "fixture_value");
  if (!value || value() != 42) return 74;
  return dlclose(library) == 0 ? 0 : 75;
}
"""
STARTUP = r"""
#include <unistd.h>
extern int fixture_value(void);
int main(void) {
  static const char marker[] = "MAIN_STARTED\n";
  (void)write(STDOUT_FILENO, marker, sizeof(marker) - 1);
  return fixture_value() == 42 ? 0 : 74;
}
"""


def run(*arguments, **kwargs):
    return subprocess.run(arguments, capture_output=True, text=True, timeout=20, check=False, **kwargs)


def require(result, purpose):
    if result.returncode:
        raise RuntimeError(purpose + ": " + (result.stderr.strip() or result.stdout.strip()))
    return result


def sign(path, identity, executable=False):
    options = ["--options", "runtime,library"] if executable else []
    require(run("codesign", "--force", "--sign", identity, *options, str(path)), "Fixture signing failed")


def check_execution(executable, arguments, allowed, startup=False):
    environment = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
    result = run(str(executable), *map(str, arguments), env=environment)
    if allowed:
        if (result.returncode or result.stdout.count("LIBRARY_CONSTRUCTOR\n") != 1 or
                result.stdout.count("MAIN_STARTED\n") != 1):
            raise RuntimeError("Same-Team fixture did not load correctly: " + result.stderr + result.stdout)
    elif "LIBRARY_CONSTRUCTOR" in result.stdout or "LIBRARY_CONSTRUCTOR" in result.stderr:
        raise RuntimeError("Rejected library executed its constructor")
    elif startup:
        if result.returncode == 0 or "MAIN_STARTED" in result.stdout:
            raise RuntimeError("Startup admission failed to reject an untrusted dependency before main")
    elif result.returncode != 73 or result.stdout != "MAIN_STARTED\n":
        raise RuntimeError("dlopen did not reach and reject the untrusted library: " + result.stderr)


def exercise(identity):
    if sys.platform != "darwin" or not identity or identity == "-":
        raise RuntimeError("This test requires macOS and an explicit Apple-issued signing identity")
    completed = []
    with tempfile.TemporaryDirectory(prefix="deskflow library admission ") as temporary:
        root = Path(temporary)
        sdk = require(run("xcrun", "--sdk", "macosx", "--show-sdk-path"), "Cannot locate SDK").stdout.strip()

        def compile_source(source, output, *arguments):
            source_path = output.with_suffix(".c")
            source_path.write_text(source)
            require(run("xcrun", "clang", "-isysroot", sdk, "-Wall", "-Wextra", "-Werror",
                        "-mmacosx-version-min=14.0", str(source_path), "-o", str(output),
                        *arguments), "Fixture compilation failed")

        template = root / "template.dylib"
        compile_source(LIBRARY, template, "-dynamiclib", "-Wl,-install_name,@executable_path/startup.dylib")
        loader, startup = root / "loader", root / "startup"
        compile_source(LOADER, loader)
        compile_source(STARTUP, startup, str(template))
        ordinary_loader = root / "ordinary-loader"
        shutil.copy2(loader, ordinary_loader)
        sign(ordinary_loader, identity)
        ordinary_startup = root / "ordinary-startup"
        shutil.copy2(startup, ordinary_startup)
        sign(ordinary_startup, identity)
        for executable in (loader, startup):
            sign(executable, identity, executable=True)
        architectures = bundle_tool.command("lipo", "-archs", str(loader)).split()
        if len(architectures) != 1:
            raise RuntimeError("Headless fixture must have exactly the current architecture")
        facts = bundle_tool.inspect_signing(loader, architectures[0])
        required = bundle_tool.CS_RUNTIME | bundle_tool.CS_LIBRARY_VALIDATION
        if facts.flags & required != required:
            raise RuntimeError("Fixture lacks required executable flags")
        requirement = '=anchor apple generic and certificate leaf[subject.OU] = "{}"'.format(facts.team)
        bundle_tool.command("codesign", "--verify", "--strict", "-R", requirement, str(loader))

        for kind in ("same-team", "unsigned", "ad-hoc"):
            case = root / kind
            case.mkdir()
            library = case / "startup.dylib"
            shutil.copy2(template, library)
            if kind == "same-team":
                sign(library, identity)
                bundle_tool.command("codesign", "--verify", "--strict", "-R", requirement, str(library))
            elif kind == "ad-hoc":
                sign(library, "-")
                # Control for the same library's usability: removing the
                # executable restrictions admits it and runs its constructor.
                check_execution(ordinary_loader, [library], allowed=True)
                completed.append("ordinary-dlopen-ad-hoc-control")
                copied_ordinary_startup = case / "ordinary-startup"
                shutil.copy2(ordinary_startup, copied_ordinary_startup)
                check_execution(copied_ordinary_startup, [], allowed=True, startup=True)
                completed.append("ordinary-startup-ad-hoc-control")
            else:
                # Apple Silicon's linker may have emitted an ad-hoc signature.
                # codesign --remove-signature also succeeds when none is present.
                bundle_tool.command("codesign", "--remove-signature", str(library))
            check_execution(loader, [library], allowed=kind == "same-team")
            completed.append("dlopen-" + kind)
            copied_startup = case / "startup"
            shutil.copy2(startup, copied_startup)
            check_execution(copied_startup, [], allowed=kind == "same-team", startup=True)
            completed.append("startup-" + kind)
        return {
            "passed": completed, "team_id": facts.team, "architecture": architectures[0],
            "scope": "Apple-or-same-Team library admission",
            "different_team_fixture_tested": False,
            "exact_loaded_images_verified": False, "original_input_authorized": False,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--signing-identity", required=True)
    arguments = parser.parse_args()
    try:
        print(json.dumps(exercise(arguments.signing_identity), indent=2))
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print("Library admission test failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
