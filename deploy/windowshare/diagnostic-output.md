<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Bounded lifecycle diagnostic output

Mac and Linux lifecycle observations keep their producer timestamp, process and
thread metadata, then enqueue the formatted line for one dedicated writer.
Writing a full stderr pipe no longer blocks that producer. This changes only
diagnostic output: input authority, original event data, receipt publication,
native identity checks and deadlines are unchanged. JSON formatting and the
short queue lock remain on the calling thread; this is not a hard real-time
allocation or execution guarantee.

The writer admits at most 128 records and 256 KiB of record bytes, including the
record currently being written. Each line is limited to 16 KiB. Empty lines are
rejected. Full/oversized records are discarded with saturating counters; once
output progresses, a `Diagnostics: diagnostic.loss` failure reports those
counts. That notice can add at most one bounded line outside the retained-record
budget. Records admitted from each producer retain their order. Loss notices
describe output loss, not a completed input operation or a new authority check.

The worker owns a close-on-exec duplicate of stderr and uses raw descriptor
writes, avoiding a held stdio lock while blocked. Blocking mode is unchanged.
Linux blocks SIGPIPE only on this dedicated thread. Darwin can send a broken-pipe
signal to the process, so the output uses `F_SETNOSIGPIPE`. That option is shared
with the original stderr file description; it does not change process-wide
signal handlers or unrelated descriptors. See Apple's
[write implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/sys_generic.c)
and [descriptor options](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_descrip.c).
Closed/failed output terminates the writer without a retry loop. It cannot report
failure through an output that no longer works. Windows retains its existing
synchronous logging; this change supplies the Mac/Linux native path.

Normal destruction drains for up to 250 ms. A still-blocked worker detaches with
its own bounded state and descriptor until output resumes or the process exits;
it never touches Qt during this late shutdown path. Exit can therefore lose the
remaining diagnostics. No assertion of complete evidence may be based on the
absence of a failure line. Other stderr writers are outside this queue, and
records larger than the platform's atomic pipe-write limit are not guaranteed
to remain contiguous with their output. The physical diagnostic observer rejects
explicit loss and malformed lifecycle records during Active and still permits
owned cleanup. Existing input recorder/footer requirements remain separate.

`windowshare-diagnostic-writer` exercises blocked output, record/byte bounds,
loss reporting, shutdown lifetime, errors, concurrent ordering and descriptor
ownership. `windowshare-lifecycle-diagnostics` runs the actual lifecycle header
in disposable processes with normal, full and closed stderr pipes. Neither
opens a desktop, registers an event tap or posts input. These checks establish
diagnostic backpressure behavior, not a fix for native scroll timing or repeat.
