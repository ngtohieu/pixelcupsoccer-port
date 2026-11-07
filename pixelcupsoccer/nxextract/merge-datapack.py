#!/usr/bin/env python3
"""Fold Pixel Cup Soccer's UnityDataAssetPack bundle into data.unity3d.

On Android, Unity mounts bin/Data/datapack.unity3d (resources.assets and its
.resS) through the Java Play Asset Delivery API, which the so-loader does not
provide. Without it every Resources.Load returns null and CGame.Awake dies in
CBaseDataLoader.init. Unity always mounts data.unity3d, so its block list and
directory are extended with the datapack ones. Blocks are compressed one by
one, so they are copied as-is: no payload is decompressed.
"""

import os
import shutil
import struct
import sys

DATA = os.path.join("assets", "bin", "Data")
CHUNK = 1 << 20
FLAG_COMPRESSION = 0x3F
FLAG_INFO_AT_END = 0x80
FLAG_PAD_START = 0x200


def fail(message):
    print("merge-datapack: %s" % message, file=sys.stderr, flush=True)
    raise SystemExit(1)


def progress(done, total, detail="merging datapack.unity3d"):
    print("NXEXTRACT_PROGRESS %d %d %s" % (done, total, detail), flush=True)


def lz4_block_decompress(src, size):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        token = src[i]
        i += 1
        length = token >> 4
        if length == 15:
            while True:
                extra = src[i]
                i += 1
                length += extra
                if extra != 255:
                    break
        out += src[i:i + length]
        i += length
        if i >= n:
            break
        offset = src[i] | (src[i + 1] << 8)
        i += 2
        if offset == 0 or offset > len(out):
            fail("corrupt LZ4 block info")
        length = token & 15
        if length == 15:
            while True:
                extra = src[i]
                i += 1
                length += extra
                if extra != 255:
                    break
        length += 4
        start = len(out) - offset
        for k in range(length):
            out.append(out[start + k])
    if len(out) != size:
        fail("LZ4 block info size mismatch (%d != %d)" % (len(out), size))
    return bytes(out)


def read_cstr(stream):
    raw = bytearray()
    while True:
        ch = stream.read(1)
        if not ch:
            fail("truncated bundle header")
        if ch == b"\0":
            return raw.decode("utf-8")
        raw += ch


def align16(stream):
    pos = stream.tell()
    stream.seek((pos + 15) & ~15)


def read_bundle(path):
    with open(path, "rb") as stream:
        signature = read_cstr(stream)
        if signature != "UnityFS":
            fail("%s is not a UnityFS bundle" % path)
        (version,) = struct.unpack(">I", stream.read(4))
        player_version = read_cstr(stream)
        engine_version = read_cstr(stream)
        total, info_csize, info_usize, flags = struct.unpack(">QIII", stream.read(20))
        if version >= 7:
            align16(stream)
        if flags & FLAG_INFO_AT_END:
            fail("%s keeps block info at the end (unsupported)" % path)
        info = stream.read(info_csize)
        if flags & FLAG_PAD_START:
            align16(stream)
        data_offset = stream.tell()
    compression = flags & FLAG_COMPRESSION
    if compression in (2, 3):
        info = lz4_block_decompress(info, info_usize)
    elif compression != 0:
        fail("%s block info compression %d unsupported" % (path, compression))

    pos = 16
    (block_count,) = struct.unpack_from(">I", info, pos)
    pos += 4
    blocks = []
    for _ in range(block_count):
        blocks.append(struct.unpack_from(">IIH", info, pos))
        pos += 10
    (node_count,) = struct.unpack_from(">I", info, pos)
    pos += 4
    nodes = []
    for _ in range(node_count):
        offset, size, node_flags = struct.unpack_from(">QQI", info, pos)
        pos += 20
        end = info.index(b"\0", pos)
        nodes.append((offset, size, node_flags, info[pos:end]))
        pos = end + 1
    return {
        "version": version,
        "player_version": player_version,
        "engine_version": engine_version,
        "flags": flags,
        "data_offset": data_offset,
        "blocks": blocks,
        "nodes": nodes,
    }


def merge(base_path, pack_path, out_path):
    base = read_bundle(base_path)
    pack = read_bundle(pack_path)
    names = set(node[3] for node in base["nodes"])
    for node in pack["nodes"]:
        if node[3] in names:
            fail("datapack entry already in data.unity3d: %s" % node[3].decode())

    shift = sum(block[0] for block in base["blocks"])
    blocks = base["blocks"] + pack["blocks"]
    nodes = list(base["nodes"]) + [
        (offset + shift, size, node_flags, name)
        for offset, size, node_flags, name in pack["nodes"]
    ]

    info = bytearray(16)
    info += struct.pack(">I", len(blocks))
    for block in blocks:
        info += struct.pack(">IIH", *block)
    info += struct.pack(">I", len(nodes))
    for offset, size, node_flags, name in nodes:
        info += struct.pack(">QQI", offset, size, node_flags) + name + b"\0"

    # Uncompressed block info, same layout flags as the source bundle.
    flags = (base["flags"] & ~(FLAG_COMPRESSION | FLAG_INFO_AT_END)) | FLAG_PAD_START
    header = bytearray(b"UnityFS\0")
    header += struct.pack(">I", base["version"])
    header += base["player_version"].encode() + b"\0"
    header += base["engine_version"].encode() + b"\0"
    total_offset = len(header)
    header += struct.pack(">QIII", 0, len(info), len(info), flags)
    if base["version"] >= 7:
        header += b"\0" * (-len(header) % 16)
    body = header + info
    body += b"\0" * (-len(body) % 16)

    base_bytes = sum(block[1] for block in base["blocks"])
    pack_bytes = sum(block[1] for block in pack["blocks"])
    total = len(body) + base_bytes + pack_bytes
    struct.pack_into(">Q", body, total_offset, total)

    done = 0
    with open(out_path, "wb") as sink:
        sink.write(body)
        for path, bundle, count in (
            (base_path, base, base_bytes),
            (pack_path, pack, pack_bytes),
        ):
            with open(path, "rb") as source:
                source.seek(bundle["data_offset"])
                remaining = count
                while remaining:
                    chunk = source.read(min(CHUNK, remaining))
                    if not chunk:
                        fail("%s is truncated" % path)
                    sink.write(chunk)
                    remaining -= len(chunk)
                    done += len(chunk)
                    if done % (32 * CHUNK) < len(chunk):
                        progress(done, base_bytes + pack_bytes)
        sink.flush()
        os.fsync(sink.fileno())
    if os.path.getsize(out_path) != total:
        fail("merged bundle size mismatch")
    progress(base_bytes + pack_bytes, base_bytes + pack_bytes)
    return len(base["nodes"]), len(pack["nodes"])


def main():
    if len(sys.argv) != 2:
        fail("usage: merge-datapack.py <stage>")
    root = os.path.join(sys.argv[1], DATA)
    base_path = os.path.join(root, "data.unity3d")
    pack_path = os.path.join(root, "datapack.unity3d")
    if not os.path.isfile(base_path):
        fail("data.unity3d missing from stage")
    if not os.path.isfile(pack_path):
        print("merge-datapack: no datapack.unity3d, nothing to do", flush=True)
        return 0
    out_path = base_path + ".merged"
    base_nodes, pack_nodes = merge(base_path, pack_path, out_path)
    os.replace(out_path, base_path)
    os.remove(pack_path)
    print("merge-datapack: data.unity3d now carries %d + %d entries"
          % (base_nodes, pack_nodes), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
