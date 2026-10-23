"""Where main calls an import:  python tools/debug/import_callers.py <substring of the mangled name> [max callers]
Finds the import's GOT slot (JMPREL), the PLT stub that loads it (adrp x16 / ldr x17), and the BL sites of that stub."""
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault('RECOMP_BIN', os.path.join(REPO, 'build', 'dump', 'exefs'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'lifter'))
import recomp as R

want, limit = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 20


def name(sym):
    no = struct.unpack_from('<I', R.IMG, R.FILE_OFF + R.SYMTAB + sym * 24)[0]
    p = R.FILE_OFF + R.STRTAB + no
    return R.IMG[p:R.IMG.index(b'\0', p)].decode('latin-1')


slots = {}
for i in range(R.PLTRELSZ // 24):
    off, info, addend = struct.unpack_from('<QQq', R.IMG, R.FILE_OFF + R.JMPREL + i * 24)
    n = name(info >> 32)
    if want in n:
        slots[off] = n
words = struct.unpack_from('<%dI' % (R.TEXT_SIZE // 4), R.IMG, R.FILE_OFF)
stubs = {}
for i in range(len(words) - 1):
    w, w2 = words[i], words[i + 1]
    if (w & 0x9f00001f) == 0x90000010 and (w2 & 0xffc003ff) == 0xf9400211:      # adrp x16, page ; ldr x17, [x16, #imm]
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
        if imm & (1 << 20): imm -= 1 << 21
        page = ((i * 4) & ~0xfff) + (imm << 12)
        slot = page + ((w2 >> 10) & 0xfff) * 8
        if slot in slots:
            stubs[i * 4] = slots[slot]
for stub, n in sorted(stubs.items()):
    print('%s: stub %x' % (n, R.BASE + stub))
    k = 0
    for i, w in enumerate(words):
        if (w & 0xfc000000) == 0x94000000:
            d = w & 0x3ffffff
            if d & (1 << 25): d -= 1 << 26
            if i * 4 + d * 4 == stub:
                print('   bl from %x' % (R.BASE + i * 4))
                k += 1
                if k >= limit: break
