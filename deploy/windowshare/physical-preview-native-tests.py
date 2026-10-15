#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Real private GNOME preview and cursor, using synthetic fixture input only."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import traceback

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def run(output):
    preview = module('physical_preview', HERE / 'physical-preview.py')
    qt = module('preview_qt', HERE / 'gnome-qt-viewer-tests.py')
    common = qt.common
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {'status': 'failed', 'physicalInput': False, 'userDesktopUsed': False,
              'originalDeliveryAuthority': False, 'syntheticFixtureInput': True}
    try:
        with tempfile.TemporaryDirectory(prefix='df-physical-', dir='/tmp') as temporary:
            root = Path(temporary)
            processes = common.Processes(output, common.private_environment(root), lifetime_seconds=60)
            provider = None
            try:
                frame = preview.install(processes, root)
                qt.start_shell(processes, root, report)
                preview.enable(processes, common)
                common.wait_for(processes, frame.exists, 'first compositor image', timeout=10)
                helper = root / 'ei-fixture'
                flags = shlex.split(processes.run('ei-flags', ['pkg-config', '--cflags', '--libs', 'libei-1.0']))
                processes.run('ei-build', ['cc', '-std=c11', '-O2', str(HERE / 'gnome-ei-fixture.c'),
                                          '-o', str(helper), *flags], timeout=20)
                provider = common.Provider(processes.env['DBUS_SESSION_BUS_ADDRESS'])
                session, generation, descriptor = provider.create()
                try:
                    ei = common.EiFixture(processes, helper, descriptor)
                finally:
                    os.close(descriptor)
                ei.event('ready')
                ei.sync('M {request} 0 350')
                ei.sync('M {request} 400 350')
                ei.sync('E {request}')

                def image(name):
                    baseline = preview.frame_sequence(preview.read_frame(frame))
                    common.wait_for(processes, lambda: preview.frame_sequence(preview.read_frame(frame)) > baseline + 2,
                                    'fresh compositor images', timeout=5)
                    data = preview.read_frame(frame)
                    path = output / (name + '.png')
                    path.write_bytes(data[8:])
                    report[name + 'Sequence'] = preview.frame_sequence(data)
                    return path

                first = image('pointer-at-400-350')
                ei.sync('M {request} 700 450')
                second = image('pointer-at-700-450')

                def crop(path, x, y):
                    return subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path), '-vf',
                                                    f'crop=96:96:{x}:{y},format=rgb24', '-f', 'rawvideo', '-'],
                                                   env=processes.env, timeout=5)
                # Both patches are on the otherwise static private desktop.
                # This verifies actual cursor pixels, not only includeCursor=true.
                for x, y in ((390, 340), (690, 440)):
                    common.require(crop(first, x, y) != crop(second, x, y), 'Captured pointer pixels did not move')
                report['cursorPixelsMoved'] = True
                provider.call('CloseSession', '(su)', (session, generation))
                report['status'] = 'passed'
            finally:
                if provider:
                    provider.close()
                processes.close()
                report['ownedProcessesStopped'] = all(p.poll() is not None for p in processes.children)
    except Exception as error:
        report['error'] = str(error)
        (output / 'failure.log').write_text(traceback.format_exc())
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)
    return 0 if report['status'] == 'passed' and report['ownedProcessesStopped'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=lambda p: Path(p).absolute(), required=True)
    raise SystemExit(run(parser.parse_args().output))
