"""The launcher's logo, from the player's own game files: Title_WW_LogoS of romfs/Layout/Common_Title_00 (a zstd SARC
holding timg/__Combined.bntx), decoded by tools/extract/bntx_png.c into build/work/title-logo.png. Game art: it stays in
build/ and is never committed.

    python scripts/launcher_art.py [game folder]      (default build/dump: exefs/ and romfs/)

The island behind the menu (build/work/launcher-background.jpg) is not in the game files: it is a 4K render of the title
screen with the title's 2D drawn out (TOMO_SKIPPROG=3ad3b2cf,a2462987,7328f6bd,8ac327a9,d8efa8a1,4abb6b23,9d8999e9,
012b6b99,9578c377 with SSBU_SHOT_*). Without it the launcher draws a plain gradient.
Needs the zstandard module (pip install zstandard).
"""
import os
import struct
import subprocess
import sys
import tempfile

import zstandard

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(REPO, 'build', 'work')
DEPS = os.environ.get('SSBU_DEPS') or os.path.join(REPO, '..', 'ssbu-recomp', 'deps')
ARCHIVE = os.path.join('romfs', 'Layout', 'Common_Title_00.Nin_NX_NVN.blarc.zs')
LOGO = 'Title_WW_LogoS^w'


def sarc_file(blob, want):
    """One file of a SARC archive by name, or None."""
    assert blob[:4] == b'SARC', 'not a SARC archive'
    data = struct.unpack_from('<I', blob, 0xC)[0]
    n = struct.unpack_from('<H', blob, 0x14 + 6)[0]
    names = 0x14 + 12 + n * 16 + 8
    for i in range(n):
        _, attr, start, end = struct.unpack_from('<IIII', blob, 0x14 + 12 + i * 16)
        if attr & 0x01000000:
            o = names + (attr & 0xFFFF) * 4
            if blob[o:blob.index(b'\0', o)].decode() == want:
                return blob[data + start:data + end]
    return None


def bntx_png():
    exe = os.path.join(WORK, 'tools', 'bntx_png.exe')
    if not os.path.exists(exe):
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        src = os.path.join(REPO, 'tools', 'extract', 'bntx_png.c'), os.path.join(REPO, 'runtime', 'tex_decode.c')
        subprocess.run([os.path.join(DEPS, 'llvm', 'bin', 'clang.exe'), '-O2', '-w', '-D_CRT_SECURE_NO_WARNINGS', *src,
                        '-I' + os.path.join(REPO, 'runtime'), '-o', exe], check=True)
    return exe


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'build', 'dump')
    path = os.path.join(game, ARCHIVE)
    if not os.path.exists(path):
        sys.exit('no %s: give the game folder (exefs/ and romfs/)' % path)
    with open(path, 'rb') as f:
        blob = zstandard.ZstdDecompressor().decompress(f.read(), max_output_size=1 << 30)
    bntx = sarc_file(blob, 'timg/__Combined.bntx')
    if not bntx:
        sys.exit('%s has no timg/__Combined.bntx' % path)
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, 'title.bntx')
        with open(p, 'wb') as f:
            f.write(bntx)
        subprocess.run([bntx_png(), p, LOGO, os.path.join(WORK, 'title-logo.png')], check=True)


if __name__ == '__main__':
    main()
