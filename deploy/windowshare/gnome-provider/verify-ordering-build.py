#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
"""Reproduce the closed Mutter source/build/package evidence, without installing.

Python >=3.12, patch and zstd are required. Network is used only with --download,
for the pinned primary Launchpad artifacts. This validates published builder
provenance; it does not claim an independent bit-for-bit rebuild of Mutter.
The runtime never reads this manifest or this script's output.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.request

HERE = Path(__file__).resolve().parent


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError(reason)


def archive(data: bytes) -> tarfile.TarFile:
    result = tarfile.open(fileobj=io.BytesIO(data))
    members = result.getmembers()
    require(len(members) <= 20000 and sum(m.size for m in members) < 256 * 1024 * 1024,
            'Archive exceeds source bounds')
    return result


def binary_tar(data: bytes) -> tarfile.TarFile:
    require(data.startswith(b'!<arch>\n'), 'Expected Debian ar archive')
    offset, payloads = 8, []
    while offset < len(data):
        header = data[offset:offset + 60]
        require(len(header) == 60 and header[58:60] == b'`\n', 'Malformed Debian member')
        length = int(header[48:58])
        name = header[:16].decode().strip().removesuffix('/')
        payload = data[offset + 60:offset + 60 + length]
        require(len(payload) == length, 'Truncated Debian member')
        if name.startswith('data.tar'):
            if name.endswith('.zst'):
                payload = subprocess.run(['zstd', '-d', '-c'], input=payload, capture_output=True,
                                         timeout=20, check=True).stdout
            payloads.append(payload)
        offset += 60 + length + length % 2
    require(len(payloads) == 1, 'Expected exactly one Debian data archive')
    return archive(payloads[0])


def verify(cache: Path, download: bool) -> dict:
    manifest = json.loads((HERE / 'ordering-builds.json').read_text())
    require(manifest['schema'] == 1, 'Unknown evidence manifest')
    cache.mkdir(parents=True, exist_ok=True)
    artifacts = {}
    for item in manifest['artifacts']:
        name = item['name']
        require(Path(name).name == name and item['bytes'] < 16 * 1024 * 1024, 'Unbounded artifact')
        path = cache / name
        if not path.exists() and download:
            require(item['url'].startswith('https://launchpad.net/ubuntu/'), 'Non-primary artifact URL')
            with urllib.request.urlopen(item['url'], timeout=40) as response:
                data = response.read(item['bytes'] + 1)
            require(len(data) == item['bytes'] and digest(data) == item['sha256'], f'Download mismatch: {name}')
            path.write_bytes(data)
        data = path.read_bytes()
        require(len(data) == item['bytes'] and digest(data) == item['sha256'], f'Artifact mismatch: {name}')
        artifacts[name] = data
    reports = []
    for build in manifest['builds']:
        version = build['version']
        info = artifacts[f'mutter_{version}_amd64.buildinfo'].decode()
        changes = artifacts[f'mutter_{version}_amd64.changes'].decode()
        dsc = artifacts[f'mutter_{version}.dsc'].decode()
        deb = f'libmutter-18-0_{version}_amd64.deb'
        for text in (info, changes):
            require(re.search(rf'^ {digest(artifacts[deb])} +{len(artifacts[deb])} +{re.escape(deb)}$', text, re.M)
                    is not None, f'Builder did not bind exact package: {version}')
        for name in ('mutter_50.1.orig.tar.xz', f'mutter_{version}.debian.tar.xz'):
            require(re.search(rf'^ {digest(artifacts[name])} +{len(artifacts[name])} +{re.escape(name)}$', dsc, re.M)
                    is not None, f'Source descriptor did not bind archive: {name}')
        for package in ('libeis-dev', 'libeis1'):
            require(f' {package} (= 1.5.0-3),' in info, f'Unexpected sender build dependency: {version}')
        log = gzip.decompress(artifacts[f'buildlog_ubuntu-resolute-amd64.mutter_{version}_BUILDING.txt.gz']).decode()
        require('Run-time dependency libeis-1.0 found: YES 1.5.0' in log and
                re.search(r'Native Backend\s+: true', log) is not None and
                'src/libmutter-18.so.0.0.0.p/backends_meta-eis.c.o' in log,
                f'Build did not compile supported native EIS path: {version}')
        with tempfile.TemporaryDirectory(prefix='deskflow-mutter-source-') as temporary:
            work = Path(temporary)
            with archive(artifacts['mutter_50.1.orig.tar.xz']) as tar:
                tar.extractall(work, filter='data')
            source = (work / 'mutter-50.1').resolve()
            with archive(artifacts[f'mutter_{version}.debian.tar.xz']) as tar:
                tar.extractall(source, filter='data')
            for line in (source / 'debian/patches/series').read_text().splitlines():
                name = line.split('#', 1)[0].strip()
                if not name:
                    continue
                patch = source / 'debian/patches' / name
                require(patch.resolve().is_relative_to(source), 'Patch outside source')
                subprocess.run(['patch', '-p1', '--batch', '--forward', '-i', str(patch)], cwd=source,
                               capture_output=True, check=True, timeout=20)
            for name, expected in build['sourceFiles'].items():
                require(digest((source / name).read_bytes()) == expected, f'Patched source mismatch: {name}')
            meson = (source / 'meson.build').read_text()
            eis = (source / 'src/backends/meta-eis.c').read_text()
            require("cdata.set('HAVE_EIS_EVENT_REF',\n  libeis_dep.version().version_compare('>= 1.5.0'))" in meson,
                    'Sender reference feature condition changed')
            require('#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_EIS_EVENT_REF)' in eis and
                    'eis_event_ref (event)' in eis and 'meta_seat_impl_run_input_task' in eis and
                    'meta_seat_impl_queue_main_thread_idle' in eis, 'Reviewed native sender path changed')
        with binary_tar(artifacts[deb]) as tar:
            for module in build['modules']:
                entry = tar.getmember(module['path'])
                require(entry.isfile() and entry.size == module['bytes'], 'Unexpected mapped module')
                require(digest(tar.extractfile(entry).read()) == module['sha256'], 'Mapped module hash mismatch')
                require(module['sha256'] in (HERE / 'logic.js').read_text(), 'Runtime allowlist lost build binding')
        reports.append({'package': version, 'architecture': 'amd64', 'nativeBackend': True,
                        'buildLibeis': '1.5.0-3', 'contract': 'sha256:' + build['modules'][0]['sha256'],
                        'modules': build['modules'], 'sourceFiles': build['sourceFiles']})
    return {'schema': 1, 'status': 'verified-published-build-provenance',
            'manifestSha256': digest((HERE / 'ordering-builds.json').read_bytes()), 'builds': reports}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.cache, args.download)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Verified {len(result['builds'])} published native Mutter build contracts; no installation or desktop access")


if __name__ == '__main__':
    main()
