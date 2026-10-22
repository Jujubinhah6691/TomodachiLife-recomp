"""Run a Skyline plugin built for game 13.0.4 on 13.0.5: its game offsets, translated as instruction patches.

    python port_13_0_4.py <plugin.nro> <13.0.5 main.elf> [functions.csv]      (prints the patches and what it left)

Plugins name game code and data as offsets from getRegionAddress(Text), written into their code for one game build
(e.g. The CSK Collection 5.0.6 is "for 13.0.4"). 13.0.5 moved read-only data, data and bss by one page and code by
different amounts in different places (docs/VERSIONS.md), so such a plugin reads the wrong objects and hooks the wrong
functions there; on a console too. build_plugins.py calls patches() for every plugin when the game is 13.0.5:

  1 the offsets: 32-bit constants the plugin builds with MOVZ (lsl 0) + MOVK (lsl 16) on one register (no further MOVK:
    a hash40 has bits above 32) that fall inside 13.0.4's main: code below 0x39c7e90 (and 4-aligned), or read-only data,
    data and bss (0x39c8000 up to the end of the image)
  2 the build: a plugin is taken for 13.0.4 when more of its code offsets that move become 13.0.5 function starts (the
    unwind table of main.elf) after the translation than before it; a 13.0.5 plugin (ARCropolis 4.1.1, smashline 1.6.8)
    gets nothing
  3 the 13.0.5 offset: data + 0x1000; code by the delta of its place. With functions.csv (data/functions.csv of
    sbergeron42/ssbu-decomp: 13.0.4 function addresses and sizes, as tools/port/verify_13_0_5.py uses it; build_plugins.py
    takes build/work/functions.csv or SSBU_FUNCTIONS_CSV) the delta at which the listed 13.0.4 functions on both sides
    line up with 13.0.5's unwind table, over -0x800..+0x800 (13.0.5 has steps docs/VERSIONS.md does not list); inside a
    function 13.0.5 changed, the delta of that function's start. Every hostrt.c g_code_13_0_5 entry comes out right
    this way. Without functions.csv: the delta of the g_code_13_0_5 entries on both sides when they agree, else the one
    candidate delta that makes the offset a 13.0.5 function start. An offset with no certain delta is left as it is and
    listed (unresolved).
No offsets of particular plugin builds are kept here. Nothing of the game is stored: both inputs come from your dump."""
import bisect
import csv
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CODE4, RO4 = 0x39c7e90, 0x39c8000          # 13.0.4: size of main's code; start of its read-only data
CODE5 = 0x39c8440
STEPS = (0, -0x1a0, -0x40, 0x450, 0x490, 0x590, 0x5b0)     # the deltas 13.0.5's code moved by (docs/VERSIONS.md)
WIDE = sorted(range(-0x800, 0x801, 4), key=abs)             # with functions.csv: every delta verify_13_0_5.py tries


class Main:
    """13.0.5 main.elf: segments and {function start: size} from .eh_frame_hdr (found through MOD0)."""
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        phoff, = struct.unpack_from('<Q', self.b, 0x20)
        phnum, = struct.unpack_from('<H', self.b, 0x38)
        self.segs = []
        for i in range(phnum):
            t, _, off, va, _, fsz, msz, _ = struct.unpack_from('<IIQQQQQQ', self.b, phoff + i * 56)
            if t == 1:
                self.segs.append((va, off, fsz, msz))
        if self.segs[0][2] != CODE5:
            raise ValueError('%s: code size %#x, not 13.0.5 (%#x)' % (path, self.segs[0][2], CODE5))
        self.end4 = self.segs[-1][0] + self.segs[-1][3] - 0x1000          # 13.0.4's image end (bss end)
        m = self.u32(4)
        eh = m + self.s32(m + 16)
        self.fde = {}
        for i in range(self.u32(eh + 8)):
            loc = eh + self.s32(eh + 12 + i * 8)
            self.fde[loc] = self.u32(eh + self.s32(eh + 16 + i * 8) + 12)

    def rd(self, va, n):
        for v, o, s, _ in self.segs:
            if v <= va < v + s:
                return self.b[o + va - v:o + va - v + n]
        raise KeyError(hex(va))

    def u32(self, va):
        return struct.unpack('<I', self.rd(va, 4))[0]

    def s32(self, va):
        return struct.unpack('<i', self.rd(va, 4))[0]


def anchors():
    """runtime/hostrt.c g_code_13_0_5 as sorted (13.0.4 offset, delta); staged beside build/work/ro_study or in the repo"""
    for p in (os.path.join(HERE, '..', 'hostrt.c'), os.path.join(HERE, '..', '..', 'runtime', 'hostrt.c')):
        if os.path.exists(p):
            s = open(p, encoding='utf-8').read()
            block = s[s.index('g_code_13_0_5[][2] = {'):]
            block = block[:block.index('};')]
            return sorted((int(a, 16), int(b, 16) - int(a, 16)) for a, b in re.findall(r'\{0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', block))
    return []


class CodeMap:
    def __init__(self, main, functions_csv=None):
        self.m = main
        self.anchor = anchors()
        self.funcs = None
        if functions_csv:
            rows = csv.DictReader(open(functions_csv))
            self.funcs = sorted(f for f in ((int(r['Address'], 16) - 0x7100000000, int(r['Size'])) for r in rows) if f[0] < RO4)
            self.starts = [f[0] for f in self.funcs]

    def by_functions(self, a, k=25):
        """the delta at which the 13.0.4 functions on both sides of a line up with 13.0.5 (verify_13_0_5.py delta_near,
        over the same range: 13.0.5 has steps docs/VERSIONS.md does not list, e.g. -0xe0 after 0x710178aa50)"""
        fde = self.m.fde
        i = bisect.bisect_right(self.starts, a)
        own = self.funcs[i - 1] if i and a < self.funcs[i - 1][0] + self.funcs[i - 1][1] else None

        def matches(j):
            s, z = self.funcs[j]
            return {d for d in WIDE if fde.get(s + d) == z}

        # on each side, the nearest delta two listed functions in a row line up at (one same-size function at some
        # delta can be another function; two in a row are not). When both sides agree, nothing between them moved
        # differently: a change in between would show as two deltas.
        near = []
        for rng in (range(i - 1, max(-1, i - 1 - k), -1), range(i, min(len(self.funcs), i + k))):
            prev, found = None, None
            for j in rng:
                cur = matches(j)
                both = (prev or set()) & cur
                if both:
                    found = min(both, key=abs)
                    break
                prev = cur
            near.append(found)
        if near[0] is not None and near[0] == near[1]:
            return near[0], 'the listed functions on both sides line up at it'
        if own:                     # only the deltas of its neighbours: a same-size function elsewhere is no evidence
            ds = [d for d in set(near) if d is not None and fde.get(own[0] + d) == own[1]]
            if len(ds) == 1:
                return ds[0], 'its 13.0.4 function, same size in 13.0.5'
        # a in the function 13.0.5 changed (the delta steps inside it): of the two neighbour deltas, the one under which
        # that function's 13.0.5 start, moved back, does not fall inside a listed 13.0.4 function and no listed function
        # starts between it and a. a then keeps its
        # offset from the function start (right when the change is after a in the function)
        if None not in near:
            keys = sorted(fde)
            ok = []
            for d in set(near):
                s5 = keys[bisect.bisect_right(keys, a + d) - 1]
                s4 = s5 - d
                j = bisect.bisect_right(self.starts, s4) - 1
                inside = j >= 0 and self.funcs[j][0] < s4 < self.funcs[j][0] + self.funcs[j][1]
                inside |= bisect.bisect_right(self.starts, a) > bisect.bisect_right(self.starts, s4)     # or one starts between it and a
                if s4 <= a and not inside:
                    ok.append((d, s4))
            if len(ok) == 1:
                return ok[0][0], 'in the function 13.0.5 changed (13.0.4 start %#x, +%#x); the other delta would start it inside a listed function' % (ok[0][1], a - ok[0][1])
        res = []
        for side in (self.funcs[max(0, i - k):i], self.funcs[i:i + k]):
            best = max(WIDE, key=lambda d: (sum(fde.get(s + d) == z for s, z in side), -abs(d)))
            res.append((best, sum(fde.get(s + best) == z for s, z in side)))
        (db, nb), (da, na) = res
        if db == da and max(nb, na) >= 10:
            return db, 'functions around it (%d/%d of 25)' % (nb, na)
        if min(nb, na) == 0 and max(nb, na) >= 15:                  # the first or last functions of the code: one side only
            return (db if nb else da), 'functions on one side of it (%d/%d of 25)' % (nb, na)
        return None, 'functions around it disagree (%+#x %d/25, %+#x %d/25)' % (db, nb, da, na)

    def by_anchors(self, a):
        i = bisect.bisect_right([x for x, _ in self.anchor], a)
        lo = self.anchor[i - 1] if i else (0, 0)
        hi = self.anchor[i] if i < len(self.anchor) else (CODE4, 0x5b0)
        if lo[1] == hi[1]:
            return lo[1], 'hostrt.c entries on both sides (%#x, %#x)' % (lo[0], hi[0])
        cands = [d for d in STEPS if a + d in self.m.fde]
        if len(cands) == 1:
            return cands[0], 'the one delta that makes it a 13.0.5 function start (between %#x %+#x and %#x %+#x)' % (lo[0], lo[1], hi[0], hi[1])
        return None, 'between hostrt.c entries %#x %+#x and %#x %+#x, %d function-start candidates' % (lo[0], lo[1], hi[0], hi[1], len(cands))

    def delta(self, a):
        if self.funcs is not None:
            return self.by_functions(a)
        return self.by_anchors(a)


def wider(w, start, rd):
    """a MOVK lsl 32/48 that continues the constant in rd (a hash40, not an offset): only up to the next instruction
    that writes rd otherwise (from there rd holds another value, e.g. the next constant built in the same register)"""
    for m in range(start, min(start + 6, len(w))):
        if (w[m] & 31) != rd:
            continue
        if (w[m] & 0x7f800000) == 0x72800000:
            if (w[m] >> 21) & 3 >= 2:
                return True
            continue
        return False
    return False


def constants(nro_path):
    """[(movz offset, movz word, movk offset, movk word, value)] in the plugin's code (module offsets)"""
    sys.path.insert(0, HERE)
    import nro_info
    n = nro_info.Nro(nro_path)
    count = n.text_size // 4
    w = struct.unpack_from('<%dI' % count, n.d, n.text_off)
    out = []
    for i in range(count):
        x = w[i]
        if (x & 0x7f800000) != 0x52800000 or (x >> 21) & 3:            # MOVZ, lsl 0
            continue
        rd, lo = x & 31, (x >> 5) & 0xffff
        for j in range(i + 1, min(i + 8, count)):
            k = w[j]
            if (k & 0x7f800000) == 0x72800000 and (k & 31) == rd:      # MOVK on the same register
                if (k >> 21) & 3 == 1 and not wider(w, j + 1, rd):
                    out.append((n.text_off + 4 * i, x, n.text_off + 4 * j, k, ((k >> 5) & 0xffff) << 16 | lo))
                break
            if (k & 31) == rd:                                         # the register is written by something else first
                break
    return out


def with_imm(word, imm16):
    return (word & ~(0xffff << 5)) | (imm16 & 0xffff) << 5


def patches(nro_path, main_elf, functions_csv=None):
    """([(module offset, original word, new word, note)], [unresolved note]) for a 13.0.4 plugin; ([], []) for another"""
    m = Main(main_elf)
    cm = CodeMap(m, functions_csv)
    found = constants(nro_path)
    code = [c for c in found if 0x10000 <= c[4] < CODE4 and c[4] % 4 == 0]
    data = [c for c in found if RO4 <= c[4] < m.end4]
    # which build: count code offsets in places that moved, as they are and translated
    n5 = n4 = 0
    for v in sorted({c[4] for c in code}):
        d, _ = cm.delta(v)
        if d:
            n5 += v in m.fde
            n4 += v + d in m.fde
    if not (n4 > n5 and n4 >= 3):
        return [], []
    out, left, seen = [], [], {}
    for mz, wz, mk, wk, v in data + code:
        if v >= RO4:
            d, why = 0x1000, 'read-only data, data or bss: one page'
        else:
            if v not in seen:
                seen[v] = cm.delta(v)
            d, why = seen[v]
        if d is None:
            left.append('main+%#x at +%#x: no certain 13.0.5 offset (%s)' % (v, mz, why))
            continue
        if d == 0:
            continue
        nv = v + d
        note = '13.0.4 main+%#x -> 13.0.5 main+%#x (%s)' % (v, nv, why)
        out.append((mz, wz, with_imm(wz, nv & 0xffff), note))
        out.append((mk, wk, with_imm(wk, nv >> 16), note))
    return out, left


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    p, left = patches(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    if not p and not left:
        print('not a 13.0.4 plugin (or no offsets that move): nothing to change')
    for off, old, new, note in p[::2]:
        print('%x %08x -> %08x    %s' % (off, old, new, note))
    for x in left:
        print('UNRESOLVED', x)
    print('%d offsets translated, %d unresolved' % (len(p) // 2, len(left)))


if __name__ == '__main__':
    main()
