<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Legacy input sequence tracing

This extension associates a captured Mac input stamp with the actual legacy
Deskflow input packet and the receiver's backend API submissions. It is a
prerequisite for an ordered original-input handoff, not that handoff itself.
It does not prove compositor consumption, ownership by a particular native
input device, Qt delivery, or delivery to the Mac application. No original-input
authority is granted by negotiation or by a receipt.

## Negotiation and lifetime

Both sides must use protocol 1.8, build with `BUILD_WINDOWSHARE=ON`, and hold the
same live authenticated TLS connection throughout the exchange. The server
advertises the `ISQ1` option only while its original-capture route is live.
Configuration cannot enable it. Older peers ignore the ordinary option and
continue exchanging their existing messages.

`ISQ1` carries a nonzero 32-bit generation. The receiver acknowledges it with
`CISA` followed by that generation. Input remains unwrapped until this reply.
Re-advertising the active generation is idempotent and never rewinds sequence
numbers. `CROP` or `ISQ1=0` retires the generation. A subsequent generation must
be strictly higher. Missing `ISQ1` in unrelated incremental options changes
nothing. Duplicate option rows and retired generations are protocol errors.
Connection replacement or revocation invalidates the retained TLS ticket.

## Frames

Every integer is unsigned and in network byte order. These are complete
Deskflow message bodies, inside the normal packet stream's length framing.

| Message | Fields after the four-byte code |
| --- | --- |
| `CISA` | Generation: 4 bytes |
| `DSEQ` | Body length: 2 bytes; generation: 4; wire sequence: 8; capture epoch: 8; capture sequence: 8; capture part: 2; one complete legacy input packet |
| `CISR` | Generation: 4 bytes; wire sequence: 8; outcome: 1; submission count: 2 |

The inner packet is bounded to 1,024 bytes. Only key down/repeat/up, mouse
button down/up, absolute/relative motion and wheel packets are accepted. The
protocol 1.8 language string layout is required for `DKDL` and `DKRP`; historical
short repeats, controls, nested envelopes, trailing bytes and truncation are
rejected before input dispatch. The receiver checks the actual packet boundary
before reading the message code so a short packet cannot borrow the next one.

Wire sequences start at one and are contiguous within a generation. Each
enveloped packet gets its own sequence, even when one captured event expands
into several legacy packets. Capture epoch, sequence and part are retained
exactly; no stamp is reconstructed from translated input.

## Receipt meaning

| Outcome | Submission count | Meaning |
| --- | --- | --- |
| Unsupported (1) | 0 | The backend did not participate in tracing |
| Filtered (2) | 0 | An explicit client gate or participating backend made no API submission |
| Submitted (3) | 1 | One native backend input/frame call pair was submitted |
| Expanded (4) | Greater than 1 | The dispatch made multiple such submissions |
| Failed (5) | Any | Dispatch failed or attribution became ambiguous |

The real EIS backend records submissions after its existing libei input and
frame calls. Tracing never adds input calls. Other backends default to
unsupported. Earlier compressed, unstamped motion is flushed outside the
current token's scope. Stamped motion is dispatched separately, without
compression across tokens. Reentrant dispatch fails the trace.

The sender retains at most 256 outstanding tokens across active and retired
generations. Capacity exhaustion, an oversized payload or sequence exhaustion
preserves the raw legacy input and increments an unavailable counter; it cannot
produce a successful trace. Retirement preserves pending tokens only to drain
exact late receipts, without calling the active receipt observer. Duplicate or
unknown receipts are protocol errors. A valid active receipt invokes the local
observer with the retained token after removing that pending entry, allowing
the observer to reset or destroy the proxy safely.

## Remaining delivery work

The post-dispatch `LocalInputSession.onRoutedInput` observation must not be wired
directly to `sendOriginal`: it is too late to replace the legacy dispatch and
would risk duplicate or reordered input. The optional
[Server dispatcher](original-input-dispatch.md) now makes that choice before
the legacy write, but the production launcher does not install its native
controller yet. Cutover still needs to account for untraced packets, filtering,
expansion and connection retirement, and obtain native ownership and consumption
evidence separately. This trace provides no compositor fence and must never be
treated as one.
