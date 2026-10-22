"""BL / B callers of a function of main, and data slots (RELR) that point at it:  python tools/debug/callers.py <hex address>"""
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault('RECOMP_BIN', os.path.join(REPO, 'build', 'dump', 'exefs'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'lifter'))
import recomp as R

t = int(sys.argv[1], 16) - R.BASE
for i, (w,) in enumerate(struct.iter_unpack('<I', R.IMG[R.FILE_OFF:R.FILE_OFF + R.TEXT_SIZE])):
    if (w & 0x7c000000) == 0x14000000:
        d = w & 0x3ffffff
        if d & (1 << 25): d -= 1 << 26
        if i * 4 + d * 4 == t:
            print('%s from %x' % ('bl' if w >> 31 else 'b ', R.BASE + i * 4))
for off in R.relr_offsets(R.IMG, R.FILE_OFF, R.RELR, R.RELRSZ):
    if struct.unpack_from('<Q', R.IMG, R.FILE_OFF + off)[0] == t:
        print('pointer in data at %x' % (R.BASE + off))
