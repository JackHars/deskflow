#!/usr/bin/env python3
"""Explain scroll delivery differences without changing the acceptance evidence.

The source, process event tap and AppKit journals are different observation
points. In particular, a sum of adjacent AppKit scrolls is NOT an exact receipt.
This report retains that failure even when an independent process tap received
every original field. It never rewrites a journal or grants test acceptance.
"""

import argparse
import hashlib
import json
import pathlib
import statistics


def raw(row):
    values = row["raw"]
    if len(values) != 17:
        raise ValueError("Expected all 17 native fields")
    result = tuple(int(value) for value in values)
    if result[0] != 22:
        raise ValueError("Expected a scroll event")
    return result


def key(row):
    return raw(row), int(row["flags"])


def compare(source, received):
    """Ordered diagnostic only. Never equate a combined receipt with an exact one."""
    expected = [key(row) for row in source]
    observed = [key(row) for row in received]
    if not expected:
        raise ValueError("A diagnostic needs a nonempty source sequence")
    cursor = 0
    combined = []
    issue = None
    for index, (fields, flags) in enumerate(observed):
        if cursor >= len(expected):
            issue = {"receivedIndex": index + 1, "reason": "extra receipt"}
            break
        if expected[cursor] == (fields, flags):
            cursor += 1
            continue
        candidates = []
        total = list(expected[cursor][0])
        # Only adjacent Changed records with identical phase, momentum,
        # continuity and flags are considered. Begin/End, gaps and reordering
        # cannot be hidden by equal totals. This bound is diagnostic, not a
        # production batching, queue or delivery limit.
        first, first_flags = expected[cursor]
        direction = lambda values: tuple((value > 0) - (value < 0) for value in values[10:12])
        if first[14:] == (2, 0, 1) and flags == first_flags and direction(first) != (0, 0):
            for end in range(cursor + 1, min(len(expected), cursor + 16)):
                following, following_flags = expected[end]
                if (following[:8], following[14:], following_flags) != (first[:8], first[14:], first_flags):
                    break
                if direction(following) != direction(first):
                    break
                for field in range(8, 14):
                    total[field] += following[field]
                if tuple(total) == fields:
                    candidates.append(end + 1)
        if len(candidates) != 1:
            issue = {"sourceIndex": cursor + 1, "receivedIndex": index + 1,
                     "reason": "ambiguous sum" if candidates else "unexplained fields, loss or order"}
            break
        end = candidates[0]
        combined.append({"firstSourceIndex": cursor + 1, "lastSourceIndex": end,
                         "receivedIndex": index + 1, "receivedRaw": list(fields)})
        cursor = end
    if issue is None and cursor != len(expected):
        issue = {"sourceIndex": cursor + 1, "reason": "missing tail"}
    return {"sourceCount": len(source), "receivedCount": len(received),
            "exactRawAndFlags": expected == observed,
            "consecutiveSumsExplainEveryReceipt": issue is None,
            "combinedRecords": combined, "unexplained": issue}


def read_rows(path):
    if path.stat().st_size > 512 * 1024 * 1024:
        raise ValueError("Diagnostic file exceeds its bound")
    with path.open() as stream:
        return [json.loads(line) for line in stream]


def burst(rows, expected):
    first = next((i for i, row in enumerate(rows) if key(row) == key(expected[0])), len(rows))
    if first == len(rows):
        return []
    # End is a real native boundary, not an invented missing receipt.
    # If it is missing, retain the entire remaining stream for the diagnostic.
    last = next((i for i in range(first, len(rows)) if raw(rows[i])[14] == 4), len(rows) - 1)
    return rows[first:last + 1]


def report(root):
    files = {name: root / relative for name, relative in {
        "producer": "burst-source.jsonl", "processTap": "pid-observer.jsonl",
        "core": "session-1/evidence/core.jsonl", "native": "session-1/evidence/native.jsonl",
        "result": "session-1/result.json", "source": "session-1/source.json",
    }.items()}
    result = json.loads(files["result"].read_text())
    fixture = json.loads(files["source"].read_text())
    sent = read_rows(files["producer"])
    tap = read_rows(files["processTap"])
    core = read_rows(files["core"])
    native = read_rows(files["native"])
    host_pid = result["companionIdentity"]["pid"]
    if (tap[0].get("step") != "observing-owned-fixture" or tap[0]["pid"] != fixture["pid"] or
            int(core[0]["pid"]) != result["coreIdentity"]["pid"] or
            int(native[0]["pid"]) != fixture["pid"] or core[0]["runId"] != native[0]["runId"]):
        raise ValueError("Observation points do not belong to the same test")
    captured = burst([r for r in core if r.get("type") == "source" and int(r["raw"][0]) == 22], sent)
    boundary = burst([r for r in tap if r.get("type") == 22 and r.get("sourcePid") == host_pid and
                      r.get("tag") == "445357494e503031"], sent)
    window = burst([r for r in native if r.get("type") == "native" and int(r["raw"][0]) == 22 and
                    int(r["sourcePid"]) == host_pid and int(r["window"]) == fixture["windowId"] and
                    int(r["tag"]) == 0x445357494e503031], sent)
    checkpoints = {"coreCapture": compare(sent, captured), "targetProcessTap": compare(sent, boundary),
                   "appKitWindow": compare(sent, window)}
    latency = None
    if checkpoints["coreCapture"]["exactRawAndFlags"] and checkpoints["targetProcessTap"]["exactRawAndFlags"]:
        numerator, denominator = tap[0]["clockNumer"], tap[0]["clockDenom"]
        if not 0 < numerator <= 0xFFFFFFFF or not 0 < denominator <= 0xFFFFFFFF:
            raise ValueError("Invalid Mach timebase")
        # Both are mach_absolute_time nanoseconds, unlike the separate Core
        # steady-clock journal. Do not subtract clocks from different hosts.
        values = [(int(b["observedTicks"]) * numerator // denominator - int(a["timestamp"])) / 1e6
                  for a, b in zip(sent, boundary)]
        if min(values) < 0:
            raise ValueError("Inconsistent source/tap clock observations")
        latency = {"scope": "HID source to target-process tap, same Mac clock; diagnostic only",
                   "medianMs": statistics.median(values), "p95Ms": sorted(values)[int(len(values) * .95) - 1],
                   "maximumMs": max(values), "firstMs": values[0], "lastMs": values[-1]}
    footer = tap[-1] if tap[-1].get("step") == "observer-end" else None
    observer_complete = bool(footer and footer["dropped"] == footer["disabled"] == 0 and
                             footer.get("ownerLost") is False and footer["records"] == len(tap) - 2 and
                             all(r.get("step") == "pid-tap-event" for r in tap[1:-1]))
    return {"schema": "deskflow-scroll-diagnostic-v1", "diagnosticOnly": True, "acceptanceGranted": False,
            "physicalAcceptance": "not-assessed", "strictTimestampPreservation": "not-assessed",
            "runId": core[0]["runId"], "checkpoints": checkpoints, "targetProcessLatency": latency,
            "processObserverEnd": footer,
            "processObserverComplete": observer_complete,
            "pipelineStatus": result.get("status"), "pipelineFailure": result.get("failure"),
            "fullActiveIntervalCompleted": result.get("autoStoppedAtDeadline", False),
            "sourceRestored": result.get("sourceRestored", False),
            "localProcessesStopped": result.get("localProcessesStopped", False),
            "fileSha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(report(args.run), indent=2))
