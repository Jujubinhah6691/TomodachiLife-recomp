"""Disassemble SDK functions by (part of) their mangled name:  python tools/debug/sdk_disasm.py <name part> [max instructions]"""
import os
import struct
import sys
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
d = open(os.path.join(REPO, 'build', 'dump', 'exefs', 'sdk.elf'), 'rb').read()
m = struct.unpack_from('<I', d, 0x1004)[0]
o = 0x1000 + m + struct.unpack_from('<i', d, 0x1000 + m + 4)[0]
t = {}
while True:
    a, b = struct.unpack_from('<qQ', d, o); o += 16
    if a == 0:
        break
    t.setdefault(a, b)
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 80
for i in range((t[5] - t[6]) // 24):
    no, info, oth, sh, val, sz = struct.unpack_from('<IBBHQQ', d, 0x1000 + t[6] + i * 24)
    p = 0x1000 + t[5] + no
    n = d[p:d.index(b'\0', p)].decode()
    if sh and (info & 0xf) == 2 and sys.argv[1] in n:
        print('%s  [sdk %#x, %d bytes]' % (n, val, sz))
        for ins in md.disasm(d[0x1000 + val:0x1000 + val + min(sz, limit * 4)], val):
            print('  %x  %-8s %s' % (ins.address, ins.mnemonic, ins.op_str))
