#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Compile isolated dylib fixtures; never launch an app or touch the desktop."""

import hashlib
import importlib.util
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
