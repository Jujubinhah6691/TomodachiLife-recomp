"""Which relocation fills a slot of main:  python tools/debug/reloc_of.py <hex offset or 0x71.. address> ..."""
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault('RECOMP_BIN', os.path.join(REPO, 'build', 'dump', 'exefs'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'lifter'))
import recomp as R

def name(sym):
    no = struct.unpack_from('<I', R.IMG, R.FILE_OFF + R.SYMTAB + sym * 24)[0]
    p = R.FILE_OFF + R.STRTAB + no
    return R.IMG[p:R.IMG.index(b'\0', p)].decode('latin-1')

want = {int(a, 16) - (0x7100000000 if int(a, 16) >= 0x7100000000 else 0) for a in sys.argv[1:]}
for base, size in ((R.RELA, R.RELASZ), (R.JMPREL, R.PLTRELSZ)):
    for i in range(size // 24):
        off, info, addend = struct.unpack_from('<QQq', R.IMG, R.FILE_OFF + base + i * 24)
        if off in want:
            print('%#x: type %#x symbol %s addend %#x' % (off, info & 0xffffffff, name(info >> 32), addend))
            want.discard(off)
for off in sorted(want):
    if off in set(R.relr_offsets(R.IMG, R.FILE_OFF, R.RELR, R.RELRSZ)):
        print('%#x: RELR -> %#x' % (off, struct.unpack_from('<Q', R.IMG, R.FILE_OFF + off)[0]))
    else:
        print('%#x: no relocation' % off)
