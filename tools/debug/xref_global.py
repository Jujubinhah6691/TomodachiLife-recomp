"""Code that reaches a global of main through its GOT slot (adrp + ldr) or directly (adrp + add/ldr/str):
    python tools/debug/xref_global.py <global hex offset> [<GOT slot hex offset>]
Prints each use with the instruction after it (a store through the loaded pointer shows who writes the global)."""
import os
import struct
import sys
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
img = open(os.path.join(REPO, 'build', 'dump', 'exefs', 'main.elf'), 'rb').read()
FO, BASE = 0x1000, 0x7100000000
text_size = struct.unpack_from('<Q', img, struct.unpack_from('<Q', img, 0x20)[0] + 32)[0]
targets = {int(a, 16) for a in sys.argv[1:]}
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
words = struct.unpack_from('<%dI' % (text_size // 4), img, FO)
page = {}
for i, w in enumerate(words):
    if (w & 0x9f000000) == 0x90000000:
        imm = ((((w >> 5) & 0x7ffff) << 2) | ((w >> 29) & 3))
        if imm & (1 << 20): imm -= 1 << 21
        page[w & 31] = (i, ((i * 4) & ~0xfff) + (imm << 12))
        continue
    rn = (w >> 5) & 31
    hit = page.get(rn)
    if not hit or i - hit[0] > 16:
        continue
    if (w & 0xffc00000) == 0xf9400000: off = ((w >> 10) & 0xfff) * 8          # ldr x, [xn, #imm]
    elif (w & 0xffc00000) == 0x91000000: off = (w >> 10) & 0xfff               # add
    elif (w & 0xffc00000) == 0xf9000000: off = ((w >> 10) & 0xfff) * 8         # str x, [xn, #imm]
    else: continue
    if hit[1] + off in targets:
        a = BASE + i * 4
        ins = list(md.disasm(img[FO + i * 4:FO + i * 4 + 12], a))
        print('  '.join('%x %s %s' % (x.address, x.mnemonic, x.op_str) for x in ins))
