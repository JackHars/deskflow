#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Repeatable headless PeerTrust fixture suite; only mutates new private copies.

Input must be the exact-signed --make-fixture-bundle output packaged with
package-macos.sh --exact-libraries, never a real application. The helper binary
is never launched. A real Apple signing identity is required for the negative
cases to retain ordinary and restricted trust while changing exact policy.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import plistlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture_bundle", type=Path)
    parser.add_argument("new_output", type=Path)
    parser.add_argument("--signing-identity", required=True)
    args = parser.parse_args()
    source = args.fixture_bundle.resolve(strict=True)
    output = args.new_output.absolute()
    if output.exists() or output.is_symlink() or output.is_relative_to(source):
        parser.error("output must be a new directory outside the source fixture")
    if (source / "Contents/Resources/proof.txt").read_bytes() != b"sealed-fixture\n":
        parser.error("input is not the headless PeerTrust test fixture")
    manifest_path = Path("Contents/Resources/deskflow-exact-libraries.json")
    original_manifest = json.loads((source / manifest_path).read_bytes())
    output.mkdir(mode=0o700)
    os.chmod(output, 0o700)
    results = []
    with (output / "run.log").open("w", encoding="utf-8") as log:
        def run(command, expected=None):
            log.write("COMMAND " + json.dumps([str(arg) for arg in command]) + "\n")
            log.flush()
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            text = result.stdout.decode("utf-8", errors="replace")
            log.write(text)
            log.flush()
            if result.returncode or (expected is not None and text != expected):
                raise RuntimeError(f"command failed ({result.returncode}): {command}; see {output / 'run.log'}")

        def clone(name):
            target = output / (name + ".app")
            run(["/usr/bin/ditto", source, target])
            return target

        def policy(manifest):
            return {
                "team-identifier": manifest["teamIdentifier"],
                "cdhash": {"$in": [bytes.fromhex(value) for value in sorted({row["cdhash"] for row in manifest["libraries"]})]},
            }

        def plist(name, value):
            path = output / (name + ".plist")
            path.write_bytes(plistlib.dumps(value, fmt=plistlib.FMT_XML, sort_keys=True))
            return path

        def sign(target, constraint):
            command = ["/usr/bin/codesign", "--force", "--sign", args.signing_identity, "--options", "runtime,library"]
            if constraint is not None:
                command += ["--library-constraint", constraint, "--enforce-constraint-validity"]
            run(command + [target])

        valid = clone("positive-tamper-reseal")
        for mode in ("exact-pass", "exact-tamper", "exact-reseal"):
            run([valid / "Contents/MacOS/deskflow-core", "--fixture-core", mode], "VERIFIED\n")
            results.append(mode)

        cases = {
            "restricted-only": "Pinned executable has no library constraint",
            "missing-manifest": "Symlink or missing packaged code path",
            "broader-or-policy": "unsupported library constraint",
            "extra-policy-hash": "differs from the sealed manifest",
            "different-host-policy": "differs from the sealed manifest",
            "altered-architecture-record": "differs from a signed library architecture or CDHash",
            "noncanonical-manifest": "not canonical compact JSON",
        }
        for name, expected in cases.items():
            target = clone(name)
            manifest = json.loads(json.dumps(original_manifest))
            exact_policy = policy(manifest)
            host_policy = None
            if name == "missing-manifest":
                (target / manifest_path).unlink()
            elif name == "broader-or-policy":
                exact_policy = {"$or": exact_policy}
            elif name == "extra-policy-hash":
                exact_policy["cdhash"]["$in"].append(bytes.fromhex("ff" * 20))
            elif name == "different-host-policy":
                host_policy = policy(manifest)
                host_policy["cdhash"]["$in"].append(bytes.fromhex("ff" * 20))
            elif name == "altered-architecture-record":
                native = {"x86_64": 0x01000007, "arm64": 0x0100000C}[platform.machine()]
                row = next((row for row in manifest["libraries"] if row["cpuType"] != native), manifest["libraries"][0])
                # Preserve actual admitted hashes so startup still succeeds on
                # single-architecture fixtures. Only the sealed slice claim is
                # wrong; exact inspection must find that mismatch independently.
                row["cpuSubtype"] ^= 1
                manifest["libraries"].sort(key=lambda row: (row["path"], row["cpuType"], row["cpuSubtype"], row["cdhash"]))
                (target / manifest_path).write_bytes(json.dumps(manifest, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode())
                exact_policy = policy(manifest)
            elif name == "noncanonical-manifest":
                with (target / manifest_path).open("ab") as stream:
                    stream.write(b"\n")
            constraint = None if name == "restricted-only" else plist(name, exact_policy)
            host_constraint = plist(name + "-host", host_policy) if host_policy is not None else constraint
            sign(target / "Contents/MacOS/deskflow-windowshare", host_constraint)
            sign(target / "Contents/MacOS/deskflow-holding-display-helper", constraint)
            sign(target, constraint)
            run(["/usr/bin/codesign", "--verify", "--strict", "--deep", target])
            run([target / "Contents/MacOS/deskflow-core", "--fixture-core", "exact-deny", expected], "VERIFIED\n")
            results.append(name)
    (output / "result.json").write_text(json.dumps({"passed": results, "native_input_started": False, "production_bundle_changed": False}, indent=2) + "\n")
    print("PASS: " + ", ".join(results))


if __name__ == "__main__":
    main()
