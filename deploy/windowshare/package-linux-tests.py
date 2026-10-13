#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Unprivileged, no-install regression tests for the reproducible Linux archive."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import stat
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

    def test_source_link_and_unwritable_output_parent_are_denied(self):
        linked_root = self.root / "provider-link"
        linked_root.symlink_to(REPO / "deploy/windowshare/gnome-provider", target_is_directory=True)
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
