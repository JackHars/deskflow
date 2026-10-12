# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Bounded, read-only parsing for the exact non-Apple library admission policy.

The caller must also validate Apple-issued signatures with codesign. This
inspects permitted identities, not loaded images, file path enforcement or live
process memory. Apple's system-library exception remains in force.
"""

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import PurePosixPath
import re
import stat


MANIFEST = "Contents/Resources/deskflow-exact-libraries.json"
MAX_MANIFEST, MAX_CONSTRAINT, MAX_RECORDS = 256 * 1024, 64 * 1024, 1024
MAX_IMAGE = 512 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_file(path, maximum):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= maximum, "Invalid bounded policy file")
        data = stream.read(maximum + 1)
        after = os.fstat(stream.fileno())
        require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) and
                len(data) == before.st_size, "Policy file changed during read")
        return data


def part(data, offset, length):
    require(0 <= offset <= len(data) and 0 <= length <= len(data) - offset, "Invalid binary range")
    return data[offset:offset + length]


def number(data, offset, size=4, big=True):
    return int.from_bytes(part(data, offset, size), "big" if big else "little")


@dataclass(frozen=True)
class CodeDirectory:
    data: bytes
    cdhash: str

    def sealed_slot(self, slot, blob):
        offset, special = number(self.data, 16), number(self.data, 24)
        size = self.data[36]
        require(special >= slot and offset >= special * size, "Missing CodeDirectory special slot")
        require(part(self.data, offset - slot * size, size) == hashlib.sha256(blob).digest()[:size],
                "CodeDirectory special slot does not seal the expected bytes")


@dataclass(frozen=True)
class Slice:
    cpu_type: int
    cpu_subtype: int
    file_type: int
    directories: tuple
    constraint: bytes


def slices(image, *, signatures=True, allow_fat=True):
    """Account for every slice and CodeDirectory; reject duplicate/overlapping data."""
    require(0 < len(image) <= MAX_IMAGE, "Invalid Mach-O size")
    magic = number(image, 0)
    if magic in (0xcafebabe, 0xbebafeca, 0xcafebabf, 0xbfbafeca):
        require(allow_fat, "Nested universal Mach-O")
        big, wide = magic in (0xcafebabe, 0xcafebabf), magic in (0xcafebabf, 0xbfbafeca)
        count, entry_size = number(image, 4, big=big), 32 if wide else 20
        require(0 < count <= 16, "Unsupported architecture count")
        end = 8 + count * entry_size
        part(image, 0, end)
        result, ranges, architectures = [], [], set()
        for index in range(count):
            at = 8 + index * entry_size
            offset = number(image, at + 8, 8 if wide else 4, big)
            size = number(image, at + (16 if wide else 12), 8 if wide else 4, big)
            require(offset >= end and size > 0 and all(offset >= b or offset + size <= a for a, b in ranges),
                    "Overlapping or invalid architecture range")
            one, = slices(part(image, offset, size), signatures=signatures, allow_fat=False)
            architecture = (one.cpu_type, one.cpu_subtype)
            require(architecture == (number(image, at, big=big), number(image, at + 4, big=big)) and
                    architecture not in architectures, "Duplicate or inconsistent architecture")
            architectures.add(architecture)
            ranges.append((offset, offset + size))
            result.append(one)
        return tuple(result)
    require(magic in (0xfeedface, 0xfeedfacf, 0xcefaedfe, 0xcffaedfe), "Unsupported Mach-O magic")
    big, wide = magic in (0xfeedface, 0xfeedfacf), magic in (0xfeedfacf, 0xcffaedfe)
    cpu, subtype, file_type = (number(image, offset, big=big) for offset in (4, 8, 12))
    count, command_size = number(image, 16, big=big), number(image, 20, big=big)
    at = 32 if wide else 28
    require(count <= 4096, "Unsupported load command count")
    part(image, at, command_size)
    end, signature = at + command_size, None
    for _ in range(count):
        require(at + 8 <= end, "Truncated load command")
        kind, size = number(image, at, big=big), number(image, at + 4, big=big)
        require(size >= 8 and size <= end - at, "Invalid load command size")
        if kind == 0x1d and signatures:
            require(size == 16 and signature is None, "Duplicate or malformed code signature command")
            offset, length = number(image, at + 8, big=big), number(image, at + 12, big=big)
            require(offset >= end and length >= 12, "Invalid code signature range")
            signature = part(image, offset, length)
        at += size
    require(at == end, "Incomplete Mach-O load commands")
    if not signatures:
        return (Slice(cpu, subtype, file_type, (), b""),)
    require(signature is not None and number(signature, 0) == 0xfade0cc0, "Missing signature superblob")
    length, count = number(signature, 4), number(signature, 8)
    require(0 < count <= 64 and 12 + count * 8 <= length <= len(signature), "Invalid signature superblob")
    types, ranges, directories, hashes, constraint = set(), [], [], set(), b""
    for index in range(count):
        kind, offset = number(signature, 12 + index * 8), number(signature, 16 + index * 8)
        require(kind not in types and 12 + count * 8 <= offset <= length - 8,
                "Duplicate or invalid signature slot")
        types.add(kind)
        magic, size = number(signature, offset), number(signature, offset + 4)
        require(8 <= size <= length - offset and all(offset >= b or offset + size <= a for a, b in ranges),
                "Overlapping or invalid signature blob")
        ranges.append((offset, offset + size))
        blob = part(signature, offset, size)
        if kind == 0 or 0x1000 <= kind < 0x1005:
            require(magic == 0xfade0c02 and size >= 44, "Invalid CodeDirectory")
            require((blob[37], blob[36]) in ((2, 32), (3, 20)),
                    "Exact library identity requires SHA-256 CodeDirectory variants only")
            cdhash = hashlib.sha256(blob).digest()[:20].hex()
            require(cdhash not in hashes, "Duplicate CodeDirectory identity")
            hashes.add(cdhash)
            directories.append(CodeDirectory(blob, cdhash))
        else:
            require(magic != 0xfade0c02, "CodeDirectory in unsupported signature slot")
            if kind == 11:
                require(magic == 0xfade8181 and size <= MAX_CONSTRAINT, "Invalid library constraint blob")
                constraint = blob
    require(0 in types and directories, "Missing primary CodeDirectory")
    return (Slice(cpu, subtype, file_type, tuple(directories), constraint),)


class DER:
    """Minimal nonrecursive schema reader, independent of Apple's constraint compiler."""
    def __init__(self, data):
        self.data, self.offset = data, 0

    def take(self, tag):
        require(self.offset + 2 <= len(self.data) and self.data[self.offset] == tag, "Unexpected constraint DER tag")
        length = self.data[self.offset + 1]
        self.offset += 2
        if length & 0x80:
            count = length & 0x7f
            require(0 < count <= 2, "Unsupported DER length")
            encoded = part(self.data, self.offset, count)
            self.offset += count
            length = int.from_bytes(encoded, "big")
            require(encoded[0] and length >= 128 and (count == 1 or length > 255), "Nonminimal DER length")
        result = part(self.data, self.offset, length)
        self.offset += length
        return result

    def done(self):
        require(self.offset == len(self.data), "Unexpected trailing constraint data")

    def entry(self, name):
        value = DER(self.take(0x30))
        require(value.take(0x0c) == name.encode("ascii"), "Unsupported constraint predicate or ordering")
        return value

    def integer_entry(self, name, expected):
        value = self.entry(name)
        require(value.take(0x02) == bytes([expected]), "Unsupported constraint metadata")
        value.done()


def parse_constraint(blob):
    require(8 <= len(blob) <= MAX_CONSTRAINT and number(blob, 0) == 0xfade8181 and
            number(blob, 4) == len(blob), "Invalid full constraint blob")
    data = DER(blob[8:])
    root = DER(data.take(0x70))
    data.done()
    require(root.take(0x02) == b"\x01", "Unsupported constraint wrapper version")
    metadata = DER(root.take(0xb0))
    root.done()
    metadata.integer_entry("ccat", 0)
    metadata.integer_entry("comp", 1)
    requirements = metadata.entry("reqs")
    rules = DER(requirements.take(0xb0))
    requirements.done()
    hashes_entry = rules.entry("cdhash")
    operators = DER(hashes_entry.take(0xb0))
    hashes_entry.done()
    in_entry = operators.entry("$in")
    operators.done()
    values = DER(in_entry.take(0x30))
    in_entry.done()
    hashes = []
    while values.offset < len(values.data):
        value = values.take(0x04)
        require(len(value) == 20 and len(hashes) < MAX_RECORDS, "Invalid constraint CDHash count or size")
        hashes.append(value.hex())
    require(hashes and len(set(hashes)) == len(hashes), "Empty or duplicate constraint hashes")
    team_entry = rules.entry("team-identifier")
    team = team_entry.take(0x0c).decode("ascii")
    team_entry.done()
    rules.done()
    require(re.fullmatch(r"[A-Z0-9]{10}", team), "Invalid constraint Team ID")
    metadata.integer_entry("vers", 1)
    metadata.done()
    return team, sorted(hashes)


def canonical_json(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def record_key(row):
    return row["path"], row["cpuType"], row["cpuSubtype"], row["cdhash"]


def parse_manifest(data):
    require(0 < len(data) <= MAX_MANIFEST, "Invalid manifest size")
    value = json.loads(data)
    require(canonical_json(value) == data, "Manifest must be canonical compact JSON")
    require(isinstance(value, dict) and set(value) == {"libraries", "policy", "schema", "teamIdentifier"} and
            type(value["schema"]) is int and value["schema"] == 1 and value["policy"] == "same-team-cdhash-v1" and
            isinstance(value["teamIdentifier"], str) and re.fullmatch(r"[A-Z0-9]{10}", value["teamIdentifier"]),
            "Unsupported manifest schema or Team")
    rows = value["libraries"]
    require(isinstance(rows, list) and 0 < len(rows) <= MAX_RECORDS, "Invalid manifest record count")
    previous = None
    for row in rows:
        require(isinstance(row, dict) and set(row) == {"cdhash", "cpuType", "cpuSubtype", "path"},
                "Invalid manifest record fields")
        require(all(type(row[key]) is int and 0 <= row[key] <= 0xffffffff for key in ("cpuType", "cpuSubtype")),
                "Invalid manifest architecture number")
        require(isinstance(row["cdhash"], str) and re.fullmatch(r"[0-9a-f]{40}", row["cdhash"]), "Invalid manifest CDHash")
        path = row["path"]
        require(isinstance(path, str) and path.startswith("Contents/") and len(path.encode("utf-8")) <= 4096 and
                str(PurePosixPath(path)) == path and ".." not in PurePosixPath(path).parts and
                "\\" not in path and not any(ord(c) < 32 for c in path), "Invalid manifest path")
        key = record_key(row)
        require(previous is None or previous < key, "Unsorted or duplicate manifest records")
        previous = key
    return value


def manifest_for(bundle, paths, team):
    rows = []
    for path in paths:
        require(path.resolve() == path and path.is_relative_to(bundle), "Library path is not canonical and contained")
        for item in slices(read_file(path, MAX_IMAGE)):
            require(item.file_type in (6, 8), "Permitted code must be a Mach-O dylib or bundle")
            for directory in item.directories:
                rows.append({"path": path.relative_to(bundle).as_posix(), "cpuType": item.cpu_type,
                             "cpuSubtype": item.cpu_subtype, "cdhash": directory.cdhash})
    rows.sort(key=record_key)
    data = canonical_json({"schema": 1, "policy": "same-team-cdhash-v1", "teamIdentifier": team, "libraries": rows})
    parse_manifest(data)
    return data


def manifest_hashes(manifest):
    return sorted({row["cdhash"] for row in manifest["libraries"]})


def verify_executable(path, manifest, *, info=None, resources=None):
    for item in slices(read_file(path, MAX_IMAGE)):
        require(item.file_type == 2, "Fixed code must be a Mach-O executable")
        require(parse_constraint(item.constraint) == (manifest["teamIdentifier"], manifest_hashes(manifest)),
                "Executable constraint differs from the sealed manifest")
        for directory in item.directories:
            directory.sealed_slot(11, item.constraint)
            if info is not None:
                directory.sealed_slot(1, info)
            if resources is not None:
                directory.sealed_slot(3, resources)
