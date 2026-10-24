#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Unprivileged, no-install regression tests for the reproducible Linux archive."""
from __future__ import annotations

import importlib.util
import io
import json
from pathlib import Path
import posixpath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
spec = importlib.util.spec_from_file_location("package_linux", HERE / "package-linux.py")
package = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(package)


class LinuxArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="deskflow-linux-package-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.viewer = self.root / "deskflow-windowshare"
        self.core = self.root / "deskflow-core"
        # Test fixtures are not executed or mistaken for real Qt/Deskflow builds.
        for file, suffix in ((self.viewer, b"viewer"), (self.core, b"core")):
            file.write_bytes(b"\x7fELF" + b"\x02" * 16 + suffix)
            file.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
        self.source_sha = "ab" * 20

    def files(self):
        return package.package_files(self.viewer, self.core, REPO, self.source_sha)

    def test_reproducible_complete_inert_archive(self):
        files = self.files()
        first, second = self.root / "first.tar.gz", self.root / "second.tar.gz"
        package.write_archive(first, files)
        package.write_archive(second, files)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        manifest = package.verify_archive(first)
        self.assertEqual(manifest["sourceCommit"], self.source_sha)
        self.assertFalse(manifest["gnomeProviderEnabled"])
        self.assertFalse(manifest["ordinaryDeskflowModified"])
        self.assertFalse(manifest["bundledSystemLibraries"])
        self.assertEqual(
            manifest["files"][f"{package.RUNTIME}/deskflow-windowshare"]["sha256"],
            package.digest(self.viewer.read_bytes())
        )
        with tarfile.open(first, "r:gz") as archive:
            names = [entry.name for entry in archive]
            self.assertEqual(len(names), len(set(names)))
            self.assertTrue(all(name.startswith(package.PACKAGE_ROOT + "/") for name in names))
            self.assertFalse(any("/gnome-shell/extensions/" in name for name in names))
            self.assertFalse(any(name.startswith("usr/") for name in names))
            self.assertTrue(all(entry.isfile() for entry in archive))

    def test_mapped_ordering_adapter_is_required_and_integrity_bound(self):
        files = self.files()
        adapter = f"{package.PROVIDER}/ordering.js"
        self.assertEqual(files[adapter][0], (HERE / "gnome-provider/ordering.js").read_bytes())
        original, mode = files[adapter]
        files[adapter] = (original + b"\n// changed", mode)
        changed = self.root / "changed-ordering.tar.gz"
        package.write_archive(changed, files)
        with self.assertRaisesRegex(ValueError, "differs from manifest"):
            package.verify_archive(changed)
        alternative = self.root / "source" / "deploy" / "windowshare" / "gnome-provider"
        alternative.parent.mkdir(parents=True)
        shutil.copytree(HERE / "gnome-provider", alternative)
        (alternative / "ordering.js").unlink()
        with self.assertRaisesRegex(ValueError, "Provider file missing"):
            package.package_files(self.viewer, self.core, self.root / "source", self.source_sha)

    def test_packaged_provider_imports_and_ordering_diagnostics_are_complete(self):
        files = self.files()
        prefix = package.PROVIDER + "/"
        # Follow actual relative imports, including the diagnostic's ../ import,
        # so a future runtime dependency cannot silently miss the fixed list.
        for name, (data, _) in files.items():
            if not name.startswith(prefix) or not name.endswith(".js"):
                continue
            for dependency in re.findall(r"(?:from\s+|import\s*)['\"](\.[^'\"]+)['\"]", data.decode()):
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), dependency))
                self.assertTrue(target.startswith(prefix), (name, dependency))
                self.assertIn(target, files, (name, dependency))
        for name in ("interface.xml", "ordering-builds.json", "ordering-builds.json.license",
                     "verify-ordering-build.py", "tests/ordering-native.js", "tests/launcher.gjs.js",
                     "tests/launcher-native.py"):
            self.assertEqual(files[prefix + name], ((HERE / "gnome-provider" / name).read_bytes(), 0o644))
        npm = json.loads(files[prefix + "package.json"][0])
        self.assertEqual(npm["scripts"]["test"], "node --test tests/*.test.js")
        cases = list((HERE / "gnome-provider/tests").glob("*.test.js"))
        self.assertTrue(cases)
        for case in cases:
            self.assertEqual(files[prefix + "tests/" + case.name], (case.read_bytes(), 0o644))
        evidence = json.loads(files[prefix + "ordering-builds.json"][0])
        self.assertEqual(evidence["schema"], 1)
        self.assertTrue(evidence["builds"])
        logic = files[prefix + "logic.js"][0].decode()
        for build in evidence["builds"]:
            for module in build["modules"]:
                self.assertIn(module["sha256"], logic)
        for name in ("package-linux.md", "app-compatibility.md", "focused-clipboard.md"):
            self.assertEqual(files[f"{package.SHARE}/{name}"], ((HERE / name).read_bytes(), 0o644))

    def test_self_consistent_manifest_cannot_omit_provider_or_diagnostics(self):
        original = self.files()
        required = [f"{package.PROVIDER}/{name}" for name in package.PROVIDER_FILES]
        required += [f"{package.SHARE}/{name}" for name in package.DOCUMENT_FILES]
        required += [f"{package.SHARE}/{name}" for name in package.SESSION_FILES]
        required += ["bin/deskflow-seamless-session", "bin/deskflow-seamless-controls"]
        for index, missing in enumerate(required):
            with self.subTest(missing=missing):
                files = dict(original)
                del files[missing]
                manifest = json.loads(files["MANIFEST.json"][0])
                del manifest["files"][missing]
                files["MANIFEST.json"] = (json.dumps(manifest).encode(), 0o644)
                archive = self.root / f"missing-{index}.tar.gz"
                package.write_archive(archive, files)
                with self.assertRaisesRegex(ValueError, "Package missing"):
                    package.verify_archive(archive)

    def test_missing_or_linked_ordering_evidence_and_compatibility_are_denied(self):
        source = self.root / "source"
        windowshare = source / "deploy/windowshare"
        windowshare.mkdir(parents=True)
        shutil.copytree(HERE / "gnome-provider", windowshare / "gnome-provider")
        for name in package.DOCUMENT_FILES + package.SESSION_FILES:
            shutil.copy2(HERE / name, windowshare / name)
        for name in ("gnome-provider/ordering-builds.json", "gnome-provider/verify-ordering-build.py",
                     "app-compatibility.md", "focused-clipboard.md", "seamless-profile.py"):
            with self.subTest(missing=name):
                path = windowshare / name
                contents = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(ValueError, "missing or symlinked"):
                    package.package_files(self.viewer, self.core, source, self.source_sha)
                path.symlink_to(HERE / name)
                with self.assertRaisesRegex(ValueError, "missing or symlinked"):
                    package.package_files(self.viewer, self.core, source, self.source_sha)
                path.unlink()
                path.write_bytes(contents)

    def test_packaged_session_launcher_runs_with_spaces_in_path_without_core(self):
        root = self.root / 'package with spaces'
        for name, (data, mode) in self.files().items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(mode)
        # The ELF fixtures cannot execute. Help must remain input-free.
        result = subprocess.run([str(root / 'bin/deskflow-seamless-session'), '--help'],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('{init,pair,run,inspect,update}', result.stdout)

    def test_cannot_overwrite_archive_or_follow_output_symlink(self):
        dest = self.root / "do-not-touch.tar.gz"
        dest.write_bytes(b"private-content")
        with self.assertRaises(FileExistsError):
            package.write_archive(dest, self.files())
        self.assertEqual(dest.read_bytes(), b"private-content")
        link = self.root / "link.tar.gz"
        link.symlink_to(dest)
        with self.assertRaises(FileExistsError):
            package.write_archive(link, self.files())
        self.assertEqual(dest.read_bytes(), b"private-content")

    def test_symlink_binary_and_non_elf_are_denied(self):
        linked = self.root / "linked"
        linked.symlink_to(self.core)
        with self.assertRaisesRegex(ValueError, "non-symlink"):
            package.package_files(self.viewer, linked, REPO, self.source_sha)
        self.viewer.write_bytes(b"not-an-ELF")
        with self.assertRaisesRegex(ValueError, "Expected an ELF"):
            self.files()

    def test_invalid_source_hash_and_source_metadata_denied(self):
        with self.assertRaisesRegex(ValueError, "40-digit"):
            package.package_files(self.viewer, self.core, REPO, "unverified")
        alternative = self.root / "source" / "deploy" / "windowshare" / "gnome-provider"
        alternative.parent.mkdir(parents=True)
        shutil.copytree(REPO / "deploy/windowshare/gnome-provider", alternative)
        metadata = alternative / "metadata.json"
        metadata.write_text(metadata.read_text().replace('"50"', '"51"'))
        with self.assertRaisesRegex(ValueError, "Shell 50 pin"):
            package.package_files(self.viewer, self.core, self.root / "source", self.source_sha)

    def test_manifest_detects_tampered_program_and_unlisted_payload(self):
        files = self.files()
        program = f"{package.RUNTIME}/deskflow-core"
        original, mode = files[program]
        files[program] = (original + b"tamper", mode)
        bad = self.root / "tampered.tar.gz"
        package.write_archive(bad, files)
        with self.assertRaisesRegex(ValueError, "differs from manifest"):
            package.verify_archive(bad)
        files = self.files()
        files["private.txt"] = (b"unknown", 0o600)
        bad = self.root / "extra.tar.gz"
        package.write_archive(bad, files)
        with self.assertRaisesRegex(ValueError, "Unlisted"):
            package.verify_archive(bad)

    def test_duplicate_entries_are_rejected_even_with_valid_manifest(self):
        valid = self.root / "single.tar.gz"
        package.write_archive(valid, self.files())
        duplicated = self.root / "duplicated.tar.gz"
        with tarfile.open(valid, "r:gz") as source, tarfile.open(duplicated, "w:gz") as archive:
            first = None
            for member in source:
                data = source.extractfile(member).read()
                archive.addfile(member, io.BytesIO(data))
                if first is None:
                    first = (member, data)
            self.assertIsNotNone(first)
            archive.addfile(first[0], io.BytesIO(first[1]))
        with self.assertRaisesRegex(ValueError, "Duplicated"):
            package.verify_archive(duplicated)

    def test_source_link_and_unwritable_output_parent_are_denied(self):
        expected_source = self.root / "other" / "deploy" / "windowshare"
        expected_source.mkdir(parents=True)
        (expected_source / "gnome-provider").symlink_to(
            REPO / "deploy/windowshare/gnome-provider", target_is_directory=True
        )
        with self.assertRaisesRegex(ValueError, "symlinked"):
            package.package_files(self.viewer, self.core, self.root / "other", self.source_sha)
        with self.assertRaisesRegex(ValueError, "Output parent"):
            package.write_archive(self.root / "missing" / "archive.tar.gz", self.files())


if __name__ == "__main__":
    unittest.main(verbosity=2)
