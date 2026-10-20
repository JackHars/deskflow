#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Fault-selection and recovery-evidence tests; never launch or signal a process."""

import copy
import importlib.util
import json
from pathlib import Path
import signal
import unittest
from unittest.mock import Mock
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("pipeline_recovery", Path(__file__).with_name("pipeline-recovery.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
remote_spec = importlib.util.spec_from_file_location(
    "pipeline_recovery_linux", Path(__file__).with_name("pipeline-recovery-linux.py"))
remote = importlib.util.module_from_spec(remote_spec)
remote_spec.loader.exec_module(remote)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.core = {"pid": 10, "parent": 9, "birthMicroseconds": 100, "executable": "/own/core"}
        self.host = {"pid": 11, "parent": 10, "birthMicroseconds": 110, "executable": "/own/host"}
        self.inspector = Mock()
        self.inspector.snapshot.return_value = self.core
        self.inspector.child.return_value = self.host
        self.inspector.live.return_value = True
        self.report = {"faultSubmitted": True, "fault": "host-kill", "restorationObserved": True}
        self.pipeline = {
            "pipelineCheck": True, "pipelineSyntheticInputDeclared": False,
            "sourceRelocationObserved": True, "status": "failed", "failure": "companion process exited",
            "sourceRestored": True, "companionStopped": True, "localProcessesStopped": True,
            "finalSource": {"receipt": {"heldKeys": 0, "heldButtons": 0}},
            "ordinaryMacConfigBefore": {"ordinary": "hash"}, "ordinaryMacConfigAfter": {"ordinary": "hash"},
            "remoteCleanup": json.dumps({"ordinaryPreserved": True, "ownedProcessesStopped": True,
                                        "desktopRestored": True}),
            "cleanupErrors": ["Input evidence: Timed out waiting for evidence recorder stop"],
            "coreExit": 0, "sourceExit": 0,
        }

    def select(self, fault="host-kill"):
        return module.select_target(self.inspector, 9, 10, "/own/core", "/own/host", fault)

    def test_only_exact_coordinator_core_or_direct_companion_can_be_selected(self):
        for fault, expected in (("host-kill", self.host), ("core-kill", self.core)):
            self.assertEqual(self.select(fault)["target"], expected)
            self.inspector.child.assert_called_with(self.core, "/own/host")
        with self.assertRaisesRegex(RuntimeError, "Unknown fault"):
            self.select("arbitrary-pid")

    def test_missing_replaced_or_unrelated_process_is_refused(self):
        for key, wrong in (("parent", 99), ("executable", "/ordinary/core")):
            with self.subTest(key=key):
                changed = {**self.core, key: wrong}
                self.inspector.snapshot.return_value = changed
                with self.assertRaisesRegex(RuntimeError, "exact packaged child"):
                    self.select()
        self.inspector.snapshot.return_value = self.core
        for changed in (None, {**self.host, "parent": 99}, {**self.host, "executable": "/ordinary/host"}):
            self.inspector.child.return_value = changed
            with self.assertRaisesRegex(RuntimeError, "companion is unavailable"):
                self.select()
        self.inspector.child.return_value = self.host
        for states in ([False], [True, False]):
            self.inspector.live.side_effect = states
            with self.assertRaisesRegex(RuntimeError, "identity changed"):
                self.select()

    def test_holding_helper_requires_the_exact_companion_parent(self):
        helper = {"pid": 12, "parent": 11, "birthMicroseconds": 120, "executable": "/own/helper"}
        self.inspector.child.side_effect = [self.host, helper]
        result = module.select_target(self.inspector, 9, 10, "/own/core", "/own/host",
                                      "holding-helper-kill", "/own/helper")
        self.assertEqual(result["target"], helper)
        self.inspector.child.assert_called_with(self.host, "/own/helper")
        for changed in (None, {**helper, "parent": 99}, {**helper, "executable": "/ordinary/helper"}):
            self.inspector.child.side_effect = [self.host, changed]
            with self.assertRaisesRegex(RuntimeError, "holding helper is unavailable"):
                module.select_target(self.inspector, 9, 10, "/own/core", "/own/host",
                                     "holding-helper-kill", "/own/helper")

    def test_complete_recovery_keeps_pipeline_failed_and_evidence_unmodified(self):
        before = copy.deepcopy(self.pipeline)
        self.assertTrue(module.assess(self.report, self.pipeline))
        self.assertEqual(self.pipeline, before)
        self.assertEqual(self.pipeline["status"], "failed")
        self.report["fault"] = "core-kill"
        self.assertFalse(module.assess(self.report, self.pipeline))
        self.pipeline["coreExit"] = -signal.SIGKILL
        self.assertTrue(module.assess(self.report, self.pipeline))

    def test_initial_snapshot_waits_for_native_presentation(self):
        original = Mock()
        original.source_ready_for_start.return_value = False
        waiting = {"manual": {"state": "ready"}, "sourceWindowId": 0}
        self.assertIsNone(module.initial_observation(waiting, original))
        original.source_identity.assert_not_called()
        ready = {**waiting, "sourceWindowId": 71}
        self.assertIsNone(module.initial_observation(ready, original))
        original.source_identity.assert_not_called()
        original.source_ready_for_start.return_value = True
        self.assertIs(module.initial_observation(ready, original), ready)
        original.source_identity.assert_called_once_with(ready, None, True)
        self.assertIsNone(module.initial_observation({**ready, "manual": {"state": "active"}}, original))

    def test_incomplete_or_changed_cleanup_never_qualifies(self):
        for key, wrong in (
            ("pipelineCheck", False), ("pipelineSyntheticInputDeclared", True),
            ("sourceRelocationObserved", False), ("status", "pipeline-verified-no-physical-input"),
            ("failure", None), ("sourceRestored", False), ("companionStopped", False),
            ("localProcessesStopped", False), ("ordinaryMacConfigBefore", {}),
            ("sourceExit", -11), ("coreExit", -11),
            ("ordinaryMacConfigAfter", {"ordinary": "changed"}),
            ("finalSource", {"receipt": {"heldKeys": 1, "heldButtons": 0}}),
            ("cleanupErrors", ["Source restoration: journal unresolved"]),
            ("remoteCleanup", "{}"), ("remoteCleanup", "invalid"),
        ):
            with self.subTest(key=key, wrong=wrong):
                self.assertFalse(module.assess(self.report, {**self.pipeline, key: wrong}))
        for key in ("faultSubmitted", "restorationObserved"):
            self.assertFalse(module.assess({**self.report, key: False}, self.pipeline))
        for field in ("ordinaryPreserved", "ownedProcessesStopped", "desktopRestored"):
            remote = json.loads(self.pipeline["remoteCleanup"])
            remote[field] = False
            self.assertFalse(module.assess(self.report, {**self.pipeline, "remoteCleanup": json.dumps(remote)}))

    def test_viewer_recovery_requires_exact_remote_plan_and_exit(self):
        report = {**self.report, "fault": "viewer-kill", "linuxPlanSha256": "plan",
                  "remoteFault": {"fault": "viewer-kill", "planSha256": "plan", "targetExited": True}}
        self.assertTrue(module.assess(report, self.pipeline))
        for field, wrong in (("fault", "core-kill"), ("planSha256", "other"), ("targetExited", False)):
            changed = {**report, "remoteFault": {**report["remoteFault"], field: wrong}}
            self.assertFalse(module.assess(changed, self.pipeline))
        self.assertFalse(module.assess({**report, "remoteFaultUnverified": True}, self.pipeline))
        self.assertFalse(module.assess({**self.report, "fault": "arbitrary-pid"}, self.pipeline))

    def test_viewer_disconnect_retains_failed_session_and_requires_separate_cleanup(self):
        report = {**self.report, "fault": "viewer-kill", "linuxPlanSha256": "plan",
                  "remoteFault": {"fault": "viewer-kill", "planSha256": "plan", "targetExited": True}}
        failed = {"event": "failed", "lifecycleCompleted": False, "linuxEnvironment": "user-desktop",
                  "routeMode": "production-original-input", "macOriginalInputConfigured": True,
                  "coreExit": 1, "cleanupErrors": [], "sessionCleanupErrors": [
                      "private core IPC stop: [Errno 104] Connection reset by peer",
                      "relay: Private relay failed: Actual TLS connection ended before coordinated stop"],
                  "failure": "Controller reported failure: Actual TLS connection ended before coordinated stop",
                  "cleanup": {"controllerExit": 1, "ordinaryPreserved": True,
                              "ownedProcessesStopped": True, "desktopRestored": True}}
        message = "Linux stop: Ubuntu test failed: " + failed["failure"] + \
                  "; cleanup=[]; sessionCleanup=" + str(failed["sessionCleanupErrors"])
        pipeline = {**self.pipeline, "cleanupLinuxFailure": failed, "cleanupErrors": [message]}
        before = copy.deepcopy(pipeline)
        self.assertTrue(module.assess(report, pipeline))
        self.assertEqual(pipeline, before)
        self.assertFalse(module.assess(self.report, pipeline))  # Not an exception for other faults.
        self.assertFalse(module.assess(report, {**pipeline, "cleanupLinuxFailure": None}))
        for changes in ({"coreExit": -11}, {"event": "stopped"}, {"lifecycleCompleted": True},
                        {"failure": "Unrelated crash"}, {"cleanupErrors": ["Desktop restore failed"]},
                        {"sessionCleanupErrors": ["Unrelated shutdown failure"]}):
            self.assertFalse(module.assess(report, {**pipeline, "cleanupLinuxFailure": {**failed, **changes}}))
        for field in ("ordinaryPreserved", "ownedProcessesStopped", "desktopRestored", "controllerExit"):
            changed = copy.deepcopy(failed)
            changed["cleanup"][field] = False
            self.assertFalse(module.assess(report, {**pipeline, "cleanupLinuxFailure": changed}))
        self.assertFalse(module.assess(report, {**pipeline, "cleanupErrors": [message, "Unrelated cleanup failure"]}))


class LinuxViewerFaultTests(unittest.TestCase):
    def setUp(self):
        self.plan = {"linuxEnvironment": "user-desktop", "routeMode": "production-original-input",
                     "token": "unique", "viewerExecutable": "/own/deskflow-windowshare",
                     "desktopAtPrepare": {"bootId": "boot", "shell": {"pid": 22}}}
        self.lease = {"token": "unique", "before": self.plan["desktopAtPrepare"], "restored": False}
        self.expected = {"pid": 31, "startTicks": "12345", "executable": "/own/deskflow-windowshare"}

    def validate(self, plan=None, lease=None, expected=None, current=None, parent=22, boot="boot"):
        remote.validate_target(plan or self.plan, lease or self.lease, expected or self.expected,
                               current or {**self.expected, "commandSha256": "command"}, parent, boot)

    def test_exact_live_viewer_is_selected(self):
        self.validate()

    def test_changed_desktop_owner_or_process_is_refused(self):
        for changes in ({"linuxEnvironment": "isolated-container"}, {"routeMode": "legacy"},
                        {"viewerExecutable": "/ordinary/deskflow-windowshare"}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                self.validate(plan={**self.plan, **changes})
        for changes in ({"token": "other"}, {"before": {}}, {"restored": True}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                self.validate(lease={**self.lease, **changes})
        for changes in ({"pid": 1}, {"executable": "/own/deskflow-core"}, {"commandSha256": "extra"}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                self.validate(expected={**self.expected, **changes})
        for changes in ({"pid": 32}, {"startTicks": "new"}, {"executable": "/ordinary/viewer"}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                self.validate(current={**self.expected, **changes})
        with self.assertRaises(RuntimeError):
            self.validate(parent=99)
        with self.assertRaises(RuntimeError):
            self.validate(boot="restarted")

    def test_pidfd_identity_check_precedes_signal_and_always_closes(self):
        desktop = Mock()
        with patch.object(remote.os, "pidfd_open", create=True, return_value=77), \
             patch.object(remote.os, "close") as close, \
             patch.object(remote.signal, "pidfd_send_signal", create=True) as send, \
             patch.object(remote.selectors, "DefaultSelector") as selector:
            desktop.same_process.return_value = False
            with self.assertRaisesRegex(RuntimeError, "changed before"):
                remote.signal_owned_viewer(desktop, self.expected)
            send.assert_not_called()
            close.assert_called_once_with(77)
            close.reset_mock()
            desktop.same_process.return_value = True
            selector.return_value.__enter__.return_value.select.return_value = [(77, 1)]
            remote.signal_owned_viewer(desktop, self.expected)
            send.assert_called_once_with(77, signal.SIGKILL)
            close.assert_called_once_with(77)
            selector.return_value.__enter__.return_value.register.assert_called_once_with(
                77, remote.selectors.EVENT_READ)


if __name__ == "__main__":
    unittest.main()
