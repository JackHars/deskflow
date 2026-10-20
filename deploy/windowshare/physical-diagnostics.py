#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Diagnostic observations only: never input authority or physical acceptance.

Every stage is flushed before its operation starts. Local monotonic durations
and UTC timestamps have separate fields; remote clocks are never subtracted.
"""
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import time
import traceback


class Diagnostics:
    def __init__(self, path, component, sink=None, now=time.monotonic_ns):
        self.path, self.component, self.sink, self.now = Path(path), component, sink, now
        descriptor = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        self.stream = os.fdopen(descriptor, 'w', buffering=1)
        self.started = now()
        self.sequence = 0
        self.stack = []
        self.first_failure = None
        self.last_completed = None
        self.observations = {}
        self.error = None

    def emit(self, step, state, message, /, **details):
        # Event payloads can themselves contain message/state/step fields.
        # Keep them in details without colliding with the diagnostic envelope.
        self.sequence += 1
        row = {'schema': 'deskflow-physical-diagnostic-v1', 'sequence': self.sequence,
               'utc': datetime.now(timezone.utc).isoformat(timespec='milliseconds'),
               'elapsedMs': (self.now() - self.started) // 1000000,
               'component': self.component, 'pid': os.getpid(),
               'step': step, 'state': state, 'message': message, 'details': details}
        encoded = json.dumps(row, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
        if len(encoded.encode()) > 32768:
            raise RuntimeError('Diagnostic row exceeded its bound')
        if state == 'failed' and self.first_failure is None:
            self.first_failure = row
        if state == 'completed':
            self.last_completed = row
        try:
            self.stream.write(encoded + '\n')
            self.stream.flush()
        except OSError as error:
            self.error = 'Diagnostic log: ' + str(error)
        if self.sink:
            try:
                self.sink(row)
            except (OSError, BrokenPipeError) as error:
                # Closed SSH/stdout must never interrupt owned cleanup.
                self.error = 'Diagnostic output: ' + str(error)
        return row

    @contextmanager
    def step(self, name, **details):
        self.stack.append(name)
        started = self.now()
        self.emit(name, 'started', name, **details)
        try:
            yield
        except BaseException as error:
            # Traceback contains code/locations, never locals or typed input.
            self.emit(name, 'failed', str(error), exception=type(error).__name__,
                      traceback=traceback.format_exc(limit=8)[-12000:],
                      durationMs=(self.now() - started) // 1000000)
            raise
        else:
            self.emit(name, 'completed', name, durationMs=(self.now() - started) // 1000000)
        finally:
            self.stack.pop()

    def observe(self, name, value):
        """Record state changes, not every mouse move or polling iteration."""
        if name not in self.observations or self.observations[name] != value:
            self.observations[name] = value
            self.emit(name, 'observed', name, value=value)

    def remote(self, value):
        if not isinstance(value, dict) or value.get('schema') != 'deskflow-physical-diagnostic-v1' or \
                not isinstance(value.get('step'), str) or value.get('state') not in \
                ('started', 'completed', 'failed', 'observed') or not isinstance(value.get('message'), str):
            raise RuntimeError('Invalid remote diagnostic row')
        # Keep the sender's time and sequence as observations, not local order.
        self.emit('Ubuntu: ' + value['step'], value['state'], value['message'], remote=value)

    def summary(self):
        return {'log': str(self.path), 'events': self.sequence,
                'firstFailure': self.first_failure, 'lastCompleted': self.last_completed, 'writeError': self.error}

    def close(self):
        try:
            self.stream.close()
        except OSError as error:
            self.error = 'Diagnostic close: ' + str(error)


class LogObserver:
    """Incrementally retain native lifecycle events and actionable log lines.

    The production log remains the complete record. Diagnostic selection never
    parses keys, clipboard data, or normal per-input debug lines.
    """
    def __init__(self, path, diagnostics):
        self.path, self.diagnostics = Path(path), diagnostics
        self.offset, self.pending = 0, b''
        self.native = {}
        self.input_failure = None

    def require_live_input(self, active):
        """A revoked input route or stopped stream cannot continue an Active run.

        The normal shutdown path still consumes final logs and recorder
        footers. This guard never interrupts that owned cleanup.
        """
        if active and self.input_failure is not None:
            value = self.input_failure
            details = value.get('details')
            reason = details.get('reason') if isinstance(details, dict) else None
            suffix = ' (reason=' + str(reason) + ')' if reason is not None else ''
            prefix = 'Window capture failed at ' if value['component'] == 'Host' and \
                value['step'] == 'capture.failure' else 'Original input route failed at '
            raise RuntimeError(prefix + value['step'] +
                               ': ' + value['message'] + suffix)

    def poll(self):
        try:
            with self.path.open('rb') as stream:
                stream.seek(self.offset)
                block = stream.read(262144)
                self.offset += len(block)
        except FileNotFoundError:
            return
        lines = (self.pending + block).split(b'\n')
        self.pending = lines.pop()[-32768:]
        for raw in lines:
            text = raw.decode('utf-8', errors='replace')
            marker = 'DESKFLOW_STEP '
            if marker in text:
                try:
                    value = json.loads(text.split(marker, 1)[1])
                    if not isinstance(value, dict):
                        raise ValueError('Native diagnostic must be an object')
                    # Preserve native timing and refusal states too. Silently
                    # dropping 'slow' hid the operation that blocked input.
                    if not all(isinstance(value.get(key), str) for key in ('component', 'step', 'state', 'message')) or \
                            not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', value['state']):
                        raise ValueError('Invalid native lifecycle fields')
                    name = value['component'] + ': ' + value['step']
                    self.native[name] = value
                    terminal_capture = value['component'] == 'Core input' and \
                        value['step'] in ('capture.continuity', 'capture.delivery', 'capture.route-join') and \
                        value['state'] == 'failed'
                    terminal_delivery = value['component'] == 'Host' and \
                        value['step'] == 'input.native-delivery' and value['state'] == 'denied'
                    terminal_stream = value['component'] == 'Host' and \
                        value['step'] == 'capture.failure' and value['state'] == 'failed'
                    terminal_dispatch = value['component'] == 'Core input' and \
                        value['step'] == 'dispatch.stopped' and value['state'] == 'failed'
                    if self.input_failure is None and \
                            (terminal_capture or terminal_delivery or terminal_stream or terminal_dispatch):
                        self.input_failure = value
                    self.diagnostics.emit(name, value['state'], value['message'], native=value)
                except (ValueError, KeyError, TypeError):
                    self.diagnostics.emit('Native diagnostic parsing', 'failed', 'Malformed native lifecycle diagnostic')
            elif 'WARNING: original input dispatch stopped: ' in text:
                # Older packages have only this terminal Server warning.
                # Ordinary owner/lease revocation also occurs on an intentional
                # return, so those observations alone must not abort a test.
                value = dict(component='Core input', step='dispatch.stopped', state='failed',
                             message=text.split('WARNING: original input dispatch stopped: ', 1)[1])
                if self.input_failure is None:
                    self.input_failure = value
                self.diagnostics.emit('Core input: dispatch.stopped', 'failed', value['message'],
                                      native=value, log=str(self.path))
            elif any(word in text for word in ('WARNING:', 'ERROR:', 'FATAL:', 'Holding display unavailable;',
                                                'Source-visible fallback changed', 'Cannot start window capture:')):
                self.diagnostics.emit('Native log', 'observed', text[:4000], log=str(self.path))


def wait_until(predicate, description, timeout, wake, check=lambda: None, now=time.monotonic):
    """Wake on receipt publication; deadline checks stay bounded without sleeps."""
    deadline = now() + timeout
    while now() < deadline:
        wake.clear()
        check()
        value = predicate()
        if value:
            return value
        # Receipt changes wake this immediately. The maximum interval also
        # observes native process exits when no source publication arrives.
        wake.wait(min(0.025, max(0, deadline - now())))
    raise RuntimeError('Timed out waiting for ' + description)
