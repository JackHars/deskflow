#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Compile isolated dylib fixtures; never launch an app or touch the desktop."""

import hashlib
import importlib.util
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


sys.dont_write_bytecode = True
import macos_library_policy as policy
SPEC = importlib.util.spec_from_file_location("macos_bundle", Path(__file__).with_name("macos-bundle.py"))
bundle_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bundle_tool
SPEC.loader.exec_module(bundle_tool)


class SigningFactsTests(unittest.TestCase):
    metadata = "CodeDirectory v=20500 size=256 flags=0x12000(library-validation,runtime) hashes=2+3 location=embedded\nTeamIdentifier=ABCDE12345\n"

    def test_explicit_absent_or_empty_entitlements_only(self):
        for entitlements in (b"", plistlib.dumps({})):
            facts = bundle_tool.signing_facts(self.metadata, entitlements)
            self.assertEqual(facts.team, "ABCDE12345")
            self.assertEqual(facts.flags, bundle_tool.CS_RUNTIME | bundle_tool.CS_LIBRARY_VALIDATION)
        for claims in ({"com.apple.security.cs.disable-library-validation": True},
                       {"com.apple.security.get-task-allow": False}, {"unknown": False}, [], False):
            with self.subTest(claims=claims), self.assertRaisesRegex(RuntimeError, "entitlements"):
                bundle_tool.signing_facts(self.metadata, plistlib.dumps(claims))
        with self.assertRaisesRegex(RuntimeError, "entitlements"):
            bundle_tool.signing_facts(self.metadata, b"not a property list")

    def test_missing_ambiguous_malformed_and_adhoc_facts_deny(self):
        invalid = [
            self.metadata.replace("TeamIdentifier=ABCDE12345\n", ""),
            self.metadata + "TeamIdentifier=ABCDE12345\n",
            self.metadata + "CodeDirectory flags=invalid\n",
            self.metadata.replace("ABCDE12345", "not set"),
            self.metadata.replace("0x12000", "invalid"),
            self.metadata.replace("0x12000", "0x100012000"),
            self.metadata.replace("0x12000", "0x12002"),
            self.metadata.replace("0x12000", "0x32000"),
        ]
        for metadata in invalid:
            with self.subTest(metadata=metadata), self.assertRaises(RuntimeError):
                bundle_tool.signing_facts(metadata, b"")


class ExactPolicyTests(unittest.TestCase):
    # Actual full slot-11 blob extracted from codesign on macOS 26.1. The
    # fixture's public Team/hash are identities only, never signing secrets.
    constraint = bytes.fromhex(
        "fade818100000084707a020101b07530090c046363617402010030090c04636f6d70020101"
        "30520c0472657173b04a30290c06636468617368b01f301d0c0324696e30160414"
        "b6318e78379c45a23075b227c9d1b2254b344f50"
        "301d0c0f7465616d2d6964656e7469666965720c0a4d4d3550424a39443459"
        "30090c0476657273020101"
    )
    cdhash = "b6318e78379c45a23075b227c9d1b2254b344f50"

    def manifest(self):
        return {"schema": 1, "policy": "same-team-cdhash-v1", "teamIdentifier": "MM5PBJ9D4Y",
                "libraries": [{"path": "Contents/Frameworks/library.dylib", "cpuType": 0x1000007,
                               "cpuSubtype": 0x80000003, "cdhash": self.cdhash}]}

    def test_authentic_constraint_and_fail_closed_mutations(self):
        self.assertEqual(policy.parse_constraint(self.constraint), ("MM5PBJ9D4Y", [self.cdhash]))
        for length in range(len(self.constraint)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                policy.parse_constraint(self.constraint[:length])
        malformed = [self.constraint + b"\0", self.constraint * 1000,
                     self.constraint.replace(b"$in", b"$or"), self.constraint.replace(b"ccat", b"xxxx"),
                     self.constraint.replace(b"MM5PBJ9D4Y", b"mm5pbj9d4y"),
                     self.constraint[:-1] + b"\x02", self.constraint.replace(b"\x04\x14", b"\x0c\x14")]
        for blob in malformed:
            with self.subTest(blob=blob[:16]), self.assertRaises(ValueError):
                policy.parse_constraint(blob)

    def test_der_rejects_indefinite_nonminimal_and_trailing_values(self):
        for data in (b"\x30\x80\0\0", b"\x30\x81\x01\0", b"\x30\x82\0\x80" + bytes(128),
                     b"\x30\x83\x01\0\0", b"\x30\x82\x01\0"):
            with self.subTest(data=data[:5]), self.assertRaises(ValueError):
                policy.DER(data).take(0x30)
        reader = policy.DER(b"\x30\0\0")
        self.assertEqual(reader.take(0x30), b"")
        with self.assertRaises(ValueError):
            reader.done()

    def test_manifest_canonical_form_and_all_hash_variants(self):
        manifest = self.manifest()
        second = dict(manifest["libraries"][0], cdhash="c" * 40)
        manifest["libraries"].append(second)
        data = policy.canonical_json(manifest)
        self.assertEqual(policy.parse_manifest(data), manifest)
        self.assertEqual(policy.manifest_hashes(manifest), [self.cdhash, "c" * 40])
        for altered in (data + b"\n", data.replace(b'"schema":1', b'"schema":1.0'),
                        data.replace(b'"schema":1', b'"schema":1,"schema":1'),
                        json_bytes(manifest, indent=2)):
            with self.subTest(data=altered[:30]), self.assertRaises(ValueError):
                policy.parse_manifest(altered)

    def test_manifest_rejects_unknown_duplicate_unsafe_and_ambiguous_records(self):
        for field, value in (("path", "Contents/../escape"), ("path", "Contents//library"),
                             ("path", "Contents/library/"), ("path", "Contents/lib\0"),
                             ("path", "Contents/lib\\evil"), ("cpuType", True), ("cpuSubtype", -1),
                             ("cpuSubtype", 0x100000000), ("cpuType", 7.0), ("cdhash", self.cdhash.upper())):
            manifest = self.manifest()
            manifest["libraries"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                policy.parse_manifest(policy.canonical_json(manifest))
        for count in (0, 2, policy.MAX_RECORDS + 1):
            manifest = self.manifest()
            manifest["libraries"] *= count
            with self.assertRaises(ValueError):
                policy.parse_manifest(policy.canonical_json(manifest))
        manifest = self.manifest()
        manifest["unrecognized"] = False
        with self.assertRaises(ValueError):
            policy.parse_manifest(policy.canonical_json(manifest))

    def test_bounded_binary_reader_rejects_malformed_containers(self):
        for data in (b"", bytes(32), b"\xca\xfe\xba\xbe\0\0\0\x11", b"\xca\xfe\xba\xbe\0\0\0\x01",
                     b"\xcf\xfa\xed\xfe" + bytes(28)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                policy.slices(data)
        for offset, size in ((-1, 1), (1, -1), (5, 0), (3, 2)):
            with self.assertRaises(ValueError):
                policy.part(b"1234", offset, size)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO check requires a Unix filesystem")
    def test_bounded_file_reader_rejects_fifo_without_waiting_for_writer(self):
        with tempfile.TemporaryDirectory(prefix="deskflow-policy-fifo-") as directory:
            path = Path(directory) / "manifest"
            os.mkfifo(path)
            result = subprocess.run([
                sys.executable, "-B", "-c",
                "import sys; import macos_library_policy as p; p.read_file(sys.argv[1], 1024)", str(path),
            ], cwd=Path(__file__).parent, capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"Invalid bounded policy file", result.stderr)

    def test_inventory_denies_external_dangling_directories_and_scan_failure(self):
        with tempfile.TemporaryDirectory(prefix="deskflow-policy-scan-") as directory:
            root = Path(directory)
            bundle = root / "bundle"
            bundle.mkdir()
            link = bundle / "external"
            link.symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, "directory symlink"):
                bundle_tool.mach_files(bundle)
            link.unlink()
            link.symlink_to(root / "missing", target_is_directory=True)
            with self.assertRaises(OSError):
                bundle_tool.mach_files(bundle)
            link.unlink()
            with mock.patch.object(os, "scandir", side_effect=PermissionError("unreadable inventory")):
                with self.assertRaisesRegex(RuntimeError, "Cannot inspect bundle directory"):
                    bundle_tool.mach_files(bundle)


def json_bytes(value, **options):
    import json
    return json.dumps(value, **options).encode("utf-8")


class BundleInventoryTests(unittest.TestCase):
    """Pure layout/audit fixtures: no compiler, codesign, or executable launch."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="deskflow-bundle-inventory-")
        self.addCleanup(self.temporary.cleanup)
        self.app = Path(self.temporary.name).resolve() / "Core.app"
        self.executables = self.app / "Contents/MacOS"
        self.executables.mkdir(parents=True)
        self.names = ("deskflow-core", "deskflow-holding-display-helper", "deskflow-windowshare")
        for name in self.names:
            path = self.executables / name
            path.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-only")
            path.chmod(0o755)
        with (self.app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "deskflow-core", "LSMinimumSystemVersion": "13.0"}, stream)
        inspector = mock.patch.object(bundle_tool, "inspect", side_effect=lambda path: bundle_tool.MachO(
            path, [], [], [], (13, 0, 0), {"arm64"}, set()))
        inspector.start()
        self.addCleanup(inspector.stop)
        # Any accidental system inspection/signing is a test failure.
        commands = mock.patch.object(bundle_tool.subprocess, "run", side_effect=AssertionError("not a pure audit"))
        commands.start()
        self.addCleanup(commands.stop)

    def test_core_bundle_requires_its_companion_before_inspection(self):
        (self.executables / "deskflow-windowshare").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing or external bundle executable.*deskflow-windowshare"):
            bundle_tool.Bundle(self.app)

    def test_complete_core_bundle_covers_all_fixed_executables(self):
        bundle = bundle_tool.Bundle(self.app)
        self.assertEqual(bundle.fixed_executables(), {self.executables / name for name in self.names})
        self.assertEqual(bundle.verify(signature=False)["errors"], [])

    def test_removed_companion_cannot_disappear_from_later_audit(self):
        bundle = bundle_tool.Bundle(self.app)
        (self.executables / "deskflow-windowshare").unlink()
        with self.assertRaisesRegex(RuntimeError, "fixed executable must be a contained regular file"):
            bundle.verify(signature=False)

    def test_non_mach_companion_fails_even_without_restricted_signing(self):
        (self.executables / "deskflow-windowshare").write_text("#!/bin/sh\nexit 0\n")
        report = bundle_tool.Bundle(self.app).verify(signature=False)
        self.assertEqual(len(report["errors"]), 1)
        self.assertIn("fixed executable is not a contained Mach-O binary", report["errors"][0])
        self.assertIn("deskflow-windowshare", report["errors"][0])

    def test_windowshare_only_bundle_does_not_require_optional_core(self):
        (self.executables / "deskflow-core").unlink()
        with (self.app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "deskflow-windowshare", "LSMinimumSystemVersion": "13.0"}, stream)
        self.assertEqual(bundle_tool.Bundle(self.app).verify(signature=False)["errors"], [])


@unittest.skipUnless(sys.platform == "darwin", "Mach-O fixtures require macOS command-line tools")
class BundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="deskflow bundle fixtures ")
        cls.root = Path(cls.temporary.name)
        cls.libraries = cls.root / "third party/lib"
        cls.cellar = cls.root / "brew/Cellar/leaf/1/lib"
        cls.opt = cls.root / "brew/opt/leaf"
        cls.brew_lib = cls.root / "brew/lib"
        for directory in (cls.libraries, cls.cellar, cls.opt.parent, cls.brew_lib):
            directory.mkdir(parents=True, exist_ok=True)
        cls.opt.symlink_to(cls.cellar.parent, target_is_directory=True)
        cls.template = cls.root / "Template.app"
        executables = cls.template / "Contents/MacOS"
        executables.mkdir(parents=True)
        sdk = bundle_tool.command("xcrun", "--sdk", "macosx", "--show-sdk-path").strip()
        cls.compiler = ["xcrun", "clang", "-isysroot", sdk, "-Wl,-headerpad_max_install_names"]

        def compile_source(source, output, *arguments):
            result = subprocess.run(
                cls.compiler + ["-x", "c", "-", "-x", "none", "-o", str(output)] + list(arguments),
                input=source, capture_output=True, text=True,
            )
            if result.returncode:
                raise RuntimeError(result.stderr)

        leaf = cls.cellar / "libleaf.1.2.dylib"
        compile_source("int leaf(void) { return 7; }", leaf, "-dynamiclib", "-mmacosx-version-min=14.0",
                       "-Wl,-install_name,@rpath/libleaf.1.dylib")
        (cls.cellar / "libleaf.1.dylib").symlink_to(leaf.name)
        (cls.brew_lib / "libleaf.1.dylib").symlink_to(leaf)
        middle = cls.libraries / "libmiddle.dylib"
        compile_source("extern int leaf(void); int middle(void) { return leaf(); }", middle,
                       "-dynamiclib", "-mmacosx-version-min=13.0", "-Wl,-install_name," + str(middle),
                       "-Wl,-rpath," + str(cls.cellar), str(leaf))
        main = executables / "fixture"
        compile_source("extern int middle(void); int main(void) { return middle(); }", main,
                       "-mmacosx-version-min=13.0", str(middle), "-Wl,-rpath," + str(cls.libraries))
        shutil.copy2(main, executables / "deskflow-holding-display-helper")
        with (cls.template / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({
                "CFBundleExecutable": "fixture", "CFBundleIdentifier": "org.deskflow.bundle-fixture",
                "CFBundlePackageType": "APPL", "LSMinimumSystemVersion": "13.0",
            }, stream)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.app = self.root / (self.id().split(".")[-1] + ".app")
        shutil.copytree(self.template, self.app)
        self.search_roots = [self.libraries, self.brew_lib, self.opt / "lib"]

    def hashes(self):
        return {str(path.relative_to(self.app)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in self.app.rglob("*") if path.is_file()}

    def test_recursive_closure_symlinks_minimum_and_signing(self):
        bundle = bundle_tool.Bundle(self.app, self.search_roots)
        report = bundle.repair()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["minimum_macos"], "14.0")
        self.assertEqual(report["declared_minimum_macos"], "14.0")
        self.assertEqual(set(report["copied_libraries"]), {
            "Contents/Frameworks/libmiddle.dylib", "Contents/Frameworks/libleaf.1.dylib",
        })
        # The second dependency existed only outside the bundle behind a
        # Homebrew-style lib/opt symlink. Verify without supplying any roots.
        for path in bundle_tool.mach_files(self.app):
            if path != bundle.main:
                bundle_tool.command("codesign", "--force", "--sign", "-", str(path))
        bundle_tool.command("codesign", "--force", "--sign", "-", str(self.app))
        before = self.hashes()
        self.assertEqual(bundle_tool.Bundle(self.app).verify()["errors"], [])
        admission = bundle_tool.Bundle(self.app).verify(restricted_libraries=True)
        self.assertTrue(any("Restricted library admission failed" in error for error in admission["errors"]))
        self.assertIsNone(admission["restricted_library_admission"])
        self.assertEqual(self.hashes(), before, "verify must not rewrite signed files")

        (self.app / "Contents/Frameworks/libleaf.1.dylib").unlink()
        report = bundle_tool.Bundle(self.app).verify(signature=False)
        self.assertTrue(any("libleaf.1.dylib" in error for error in report["errors"]))

    def test_read_only_audit_rejects_machine_dependencies(self):
        before = self.hashes()
        report = bundle_tool.Bundle(self.app).verify(signature=False)
        self.assertTrue(any("dependency is missing or external" in error for error in report["errors"]))
        self.assertTrue(any("run path escapes" in error for error in report["errors"]))
        self.assertEqual(self.hashes(), before)

    def test_reuses_the_same_library_already_rewritten_by_macdeployqt(self):
        frameworks = self.app / "Contents/Frameworks"
        frameworks.mkdir()
        leaf = frameworks / "libleaf.1.dylib"
        shutil.copy2(self.cellar / "libleaf.1.2.dylib", leaf)
        bundle_tool.command("install_name_tool", "-id", "@executable_path/../Frameworks/libleaf.1.dylib", str(leaf))
        report = bundle_tool.Bundle(self.app, self.search_roots).repair()
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["copied_libraries"], ["Contents/Frameworks/libmiddle.dylib"])

    def test_missing_explicit_root_fails_before_output_is_accepted(self):
        empty = self.root / "empty library root"
        empty.mkdir()
        with self.assertRaisesRegex(RuntimeError, "explicit library search roots"):
            bundle_tool.Bundle(self.app, [empty]).repair()

    def test_external_helper_symlink_is_rejected(self):
        helper = self.app / "Contents/MacOS/deskflow-holding-display-helper"
        helper.unlink()
        helper.symlink_to(self.template / "Contents/MacOS/fixture")
        with self.assertRaisesRegex(RuntimeError, "Missing or external bundle executable"):
            bundle_tool.Bundle(self.app)

    def test_restricted_audit_cannot_skip_a_non_mach_helper(self):
        helper = self.app / "Contents/MacOS/deskflow-holding-display-helper"
        helper.write_text("#!/bin/sh\nexit 0\n")
        bundle = bundle_tool.Bundle(self.app)
        records = [bundle_tool.inspect(path) for path in bundle_tool.mach_files(self.app)]
        with self.assertRaisesRegex(RuntimeError, "fixed executable is not a contained Mach-O"):
            bundle.verify_restricted_library_admission(records)

        helper.unlink()
        helper.symlink_to("fixture")
        records = [bundle_tool.inspect(path) for path in bundle_tool.mach_files(self.app)]
        with self.assertRaisesRegex(RuntimeError, "fixed executable must be a contained regular file"):
            bundle.verify_restricted_library_admission(records)

    def test_signed_constraints_manifest_and_special_slot_binding(self):
        bundle = bundle_tool.Bundle(self.app, self.search_roots)
        self.assertEqual(bundle.repair()["errors"], [])
        libraries = bundle.exact_library_paths()
        for path in libraries:
            bundle_tool.command("codesign", "--force", "--sign", "-", str(path))
        data = policy.manifest_for(bundle.path, libraries, "MM5PBJ9D4Y")
        manifest = policy.parse_manifest(data)
        destination = self.app / policy.MANIFEST
        destination.parent.mkdir(parents=True)
        destination.write_bytes(data)
        constraint = self.root / (self.id().split(".")[-1] + ".plist")
        constraint.write_bytes(plistlib.dumps({
            "team-identifier": manifest["teamIdentifier"],
            "cdhash": {"$in": [bytes.fromhex(value) for value in policy.manifest_hashes(manifest)]},
        }))
        options = ["--force", "--sign", "-", "--options", "runtime,library", "--library-constraint", str(constraint),
                   "--enforce-constraint-validity"]
        bundle_tool.command("codesign", *options, str(bundle.helper))
        bundle_tool.command("codesign", *options, str(self.app))
        info = bundle.plist_path.read_bytes()
        resources = (self.app / "Contents/_CodeSignature/CodeResources").read_bytes()
        policy.verify_executable(bundle.main, manifest, info=info, resources=resources)
        policy.verify_executable(bundle.helper, manifest)
        with self.assertRaisesRegex(ValueError, "special slot"):
            policy.verify_executable(bundle.main, manifest, resources=resources + b"\n")
        manifest["teamIdentifier"] = "ABCDE12345"
        with self.assertRaisesRegex(ValueError, "differs"):
            policy.verify_executable(bundle.helper, manifest)
        # A correct embedded policy cannot convert an ad-hoc fixture into
        # Apple-issued restricted/exact admission. These files are never run.
        report = bundle_tool.Bundle(self.app).verify(exact_libraries=True)
        self.assertTrue(report["errors"])
        self.assertIsNone(report["exact_library_admission"])

if __name__ == "__main__":
    unittest.main(verbosity=2)
