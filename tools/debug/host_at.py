"""Disassemble the x64 code of the game DLL at an RVA (the "dll + 0x..." of a crash report), to see which guest register
(Ctx field: x[n] at 8 * n) the faulting access came from:  python tools/debug/host_at.py <hex rva> [bytes before] [count]"""
import os
import struct
import sys
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
d = open(os.path.join(REPO, 'build', 'work', 'ssbu_game_eh_o1.dll'), 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
rva = int(sys.argv[1], 16)
back = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x40
for i in range(nsec):
    s = pe + 24 + opt + i * 40
    va, vsize, raw = struct.unpack_from('<I', d, s + 12)[0], struct.unpack_from('<I', d, s + 8)[0], struct.unpack_from('<I', d, s + 20)[0]
    if va <= rva < va + vsize:
        off = raw + rva - va - back
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        for ins in md.disasm(d[off:off + back + 0x30], rva - back):
            print('%s %x  %-8s %s' % ('>>' if ins.address == rva else '  ', ins.address, ins.mnemonic, ins.op_str))
