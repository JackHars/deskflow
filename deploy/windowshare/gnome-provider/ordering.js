// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {
    ORDERING_MAPS_MAX, mappedOrderingModules, matchesMappedOrderingFile, orderingContractFor,
} from './logic.js';

const nowMs = () => GLib.get_monotonic_time() / 1000;

// These checks run in the exact Shell process which owns the provider name.
// They verify mapped file identity, not merely a same-named installed package.
const orderingAttributes = 'standard::type,standard::size,unix::device,unix::inode,unix::mode,unix::uid,time::modified,time::modified-usec,time::changed,time::changed-usec';
function orderingFileInfo(stream) {
    const info = stream.query_info(orderingAttributes, null);
    for (const attribute of orderingAttributes.split(',')) {
        if (!info.has_attribute(attribute))
            throw new Error('missing-mutter-file-identity');
    }
    if (info.get_file_type() !== Gio.FileType.REGULAR)
        throw new Error('nonregular-mutter-module');
    return {device: info.get_attribute_uint32('unix::device'),
        inode: info.get_attribute_uint64('unix::inode'), size: info.get_size(),
        uid: info.get_attribute_uint32('unix::uid'), mode: info.get_attribute_uint32('unix::mode'),
        modified: info.get_attribute_as_string('time::modified'),
        modifiedUsec: info.get_attribute_as_string('time::modified-usec'),
        changed: info.get_attribute_as_string('time::changed'),
        changedUsec: info.get_attribute_as_string('time::changed-usec')};
}
function readOrderingMaps() {
    const stream = Gio.File.new_for_path('/proc/self/maps').read(null);
    try {
        const parts = [];
        let total = 0;
        for (;;) {
            const data = stream.read_bytes(Math.min(65536, ORDERING_MAPS_MAX + 1 - total), null).get_data();
            if (!data.length)
                break;
            total += data.length;
            if (total > ORDERING_MAPS_MAX)
                throw new Error('mutter-mappings-too-large');
            parts.push(new TextDecoder('utf-8', {fatal: true}).decode(data));
        }
        return mappedOrderingModules(parts.join(''));
    } finally {
        stream.close(null);
    }
}
export function orderingStillMapped(proof) {
    try {
        if (!proof || JSON.stringify(readOrderingMaps()) !== proof.mappings)
            return false;
        return proof.files.every(file => {
            const stream = Gio.File.new_for_path(file.path).read(null);
            try {
                return JSON.stringify(orderingFileInfo(stream)) === file.identity;
            } finally {
                stream.close(null);
            }
        });
    } catch (_) {
        return false;
    }
}
export async function verifyMappedOrdering() {
    // Only ordinary local root-owned libraries, at most 8 MiB apiece. Chunked
    // async reads yield to Shell; a separate two-second budget bounds startup.
    const cancel = new Gio.Cancellable();
    const deadline = nowMs() + 2000;
    const timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 2000, () => {
        cancel.cancel();
        return GLib.SOURCE_REMOVE;
    });
    try {
        const modules = readOrderingMaps();
        const proof = {contract: '', mappings: JSON.stringify(modules), files: []};
        for (const mapped of modules) {
            const stream = Gio.File.new_for_path(mapped.path).read(cancel);
            try {
                const before = orderingFileInfo(stream);
                if (!matchesMappedOrderingFile(mapped, before))
                    throw new Error('mutter-mapped-file-mismatch');
                const checksum = new GLib.Checksum(GLib.ChecksumType.SHA256);
                let total = 0;
                for (;;) {
                    if (nowMs() >= deadline)
                        throw new Error('mutter-build-check-timeout');
                    const bytes = await new Promise((resolve, reject) => {
                        stream.read_bytes_async(Math.min(262144, before.size + 1 - total),
                            GLib.PRIORITY_DEFAULT, cancel, (source, result) => {
                                try {
                                    resolve(source.read_bytes_finish(result));
                                } catch (error) {
                                    reject(error);
                                }
                            });
                    });
                    const data = bytes.get_data();
                    if (!data.length)
                        break;
                    total += data.length;
                    if (total > before.size)
                        throw new Error('mutter-module-size-changed');
                    checksum.update(data);
                }
                if (total !== before.size || JSON.stringify(before) !== JSON.stringify(orderingFileInfo(stream)))
                    throw new Error('mutter-module-changed-during-hash');
                proof.files.push({path: mapped.path, size: total, sha256: checksum.get_string(),
                    identity: JSON.stringify(before)});
            } finally {
                stream.close(null);
            }
        }
        proof.contract = orderingContractFor(proof.files);
        if (!proof.contract || nowMs() >= deadline || !orderingStillMapped(proof))
            throw new Error('unsupported-mutter-mapped-build');
        proof.files.forEach(Object.freeze);
        Object.freeze(proof.files);
        return Object.freeze(proof);
    } catch (error) {
        console.warn(`Deskflow sender ordering unavailable: ${error.message}`);
        return null;
    } finally {
        if (!cancel.is_cancelled())
            GLib.source_remove(timeout);
    }
}
