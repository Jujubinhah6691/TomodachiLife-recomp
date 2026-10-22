"""Disassemble main at a guest address:  python tools/debug/disasm_at.py <hex address> [count]"""
import os
import sys
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
img = open(os.path.join(REPO, 'build', 'dump', 'exefs', 'main.elf'), 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
a, n = int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 40
off = a - 0x7100000000 + 0x1000
for i in md.disasm(img[off:off + n * 4], a):
    print('%x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
