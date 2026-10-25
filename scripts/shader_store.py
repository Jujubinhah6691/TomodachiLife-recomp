"""Every shader container of the game, taken out of its files, so that the runtime can compile all of them before the
title (runtime/nvn_gl.c shader preparation) instead of when something first appears on screen.

    python scripts/shader_store.py [game folder]      (default build/dump: exefs/ and romfs/)

The game keeps its Maxwell shader stages in "BNSH" containers: the shader archives under romfs/Shader (the main 3D
archive GameUber.*.bfsha holds two), the effect files and the layout archives (a "__ArchiveShader" each). Files are opened
as the game opens them (zstd, then SARC archives, nested); every "BNSH" + byte-order mark 0xFEFF found is a container of
the size its header gives. Each distinct container (by content) is written once as build/cache/shader_store/<hash>.bnsh;
store.txt lists them (hash, size, variations, the first file it was found in). Textures and sounds are skipped.
Measured 2026-10-25: 458 containers, 14,628 variations, 66 MB, under a minute.
Needs the zstandard module (pip install zstandard).
"""
import hashlib
import os
import struct
import sys

import zstandard

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, 'build', 'cache', 'shader_store')
SKIP = ('.bntx', '.bwav', '.bars')                 # never hold a shader container
ZSTD = b'\x28\xb5\x2f\xfd'


def sarc_entries(d):
    data = struct.unpack_from('<I', d, 0xC)[0]
    n = struct.unpack_from('<H', d, 0x1A)[0]
    for i in range(n):
        _, _, s, e = struct.unpack_from('<IIII', d, 0x20 + i * 16)
        yield d[data + s:data + e]


def containers(d, dz, depth=0):
    """The BNSH containers in one file's bytes (zstd and SARC opened)."""
    if d[:4] == ZSTD:
        try:
            d = dz.decompress(d, max_output_size=1 << 31)
        except zstandard.ZstdError:
            return
    if d[:4] == b'SARC' and depth < 4:
        for e in sarc_entries(d):
            yield from containers(e, dz, depth + 1)
        return
    i = d.find(b'BNSH')
    while i >= 0:
        if i + 0x20 <= len(d) and (struct.unpack_from('<I', d, i + 0xC)[0] & 0xffff) == 0xFEFF:
            size = struct.unpack_from('<I', d, i + 0x1C)[0]
            if 0x60 <= size <= len(d) - i:
                yield d[i:i + size]
                i = d.find(b'BNSH', i + size)
                continue
        i = d.find(b'BNSH', i + 4)


def variations(c):
    blk = struct.unpack_from('<I', c, 0x14)[0] >> 16
    return struct.unpack_from('<i', c, blk + 0x1C)[0] if c[blk:blk + 4] == b'grsc' else -1


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'build', 'dump')
    romfs = os.path.join(game, 'romfs')
    if not os.path.isdir(romfs):
        sys.exit('no %s: give the game folder (exefs/ and romfs/)' % romfs)
    os.makedirs(OUT, exist_ok=True)
    dz = zstandard.ZstdDecompressor()
    seen = {}
    sources = [os.path.join(game, 'exefs', 'main.elf')]       # programs built into the executable
    for dp, _, fn in os.walk(romfs):
        sources += [os.path.join(dp, f) for f in fn if not any(s in f for s in SKIP)]
    for p in sources:
        if not os.path.isfile(p):
            continue
        with open(p, 'rb') as f:
            data = f.read()
        for c in containers(data, dz):
            h = hashlib.blake2b(c, digest_size=8).hexdigest()
            if h in seen:
                continue
            seen[h] = (len(c), variations(c), os.path.relpath(p, game))
            path = os.path.join(OUT, h + '.bnsh')
            if not os.path.exists(path) or os.path.getsize(path) != len(c):
                with open(path + '.tmp', 'wb') as f:
                    f.write(c)
                os.replace(path + '.tmp', path)
    for f in os.listdir(OUT):                       # containers of an older game version
        if f.endswith('.bnsh') and f[:-5] not in seen:
            os.remove(os.path.join(OUT, f))
    with open(os.path.join(OUT, 'store.txt.tmp'), 'w') as f:
        for h, (size, nvar, src) in sorted(seen.items()):
            f.write('%s %d %d %s\n' % (h, size, nvar, src))
    os.replace(os.path.join(OUT, 'store.txt.tmp'), os.path.join(OUT, 'store.txt'))
    print('%d shader containers, %d variations, %.1f MB -> %s' % (len(seen), sum(max(v[1], 0) for v in seen.values()),
                                                               sum(v[0] for v in seen.values()) / 1048576, OUT))


if __name__ == '__main__':
    main()
