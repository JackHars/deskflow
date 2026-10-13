// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
// Read-only GJS probe. Loads the real packaged libraries but starts no Shell,
// compositor, bus, device or extension. Actual authority still requires the
// separate owned native Shell integration test and requested sender cutoff.
import GLib from 'gi://GLib';
import Meta from 'gi://Meta?version=18';
import Clutter from 'gi://Clutter?version=18';
import {orderingStillMapped, verifyMappedOrdering} from '../ordering.js';

if (!Meta.Window.$gtype || !Clutter.Stage.$gtype)
    throw new Error('Native libraries were not loaded');
const started = GLib.get_monotonic_time();
const proof = await verifyMappedOrdering();
if (!proof || !orderingStillMapped(proof))
    throw new Error('Exact supported native module contract was not verified');
const wrong = {...proof, mappings: `${proof.mappings} `};
if (orderingStillMapped(wrong))
    throw new Error('A substituted mapping identity was accepted');
const changedFile = {...proof, files: proof.files.map(file => ({...file, identity: `${file.identity} `}))};
if (orderingStillMapped(changedFile))
    throw new Error('A substituted opened-file identity was accepted');
print(JSON.stringify({scope: 'mapped-library-probe-only', contract: proof.contract,
    elapsedMs: (GLib.get_monotonic_time() - started) / 1000,
    mappings: JSON.parse(proof.mappings), files: proof.files}));
