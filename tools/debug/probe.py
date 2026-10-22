"""Insert a printf probe at guest addresses in the generated C:  python tools/debug/probe.py <gen dir> <hex addr> ...
Each file holding one of the labels is rewritten in place (a new lift, recomp_eh.py, undoes it). Then recompile those
files (compile_main.py recompiles what changed) and relink the game DLL. The probe prints x0..x3 and the link register."""
import os
import sys

gen = sys.argv[1]
for spec in sys.argv[2:]:
    a, _, kind = spec.partition(':')                  # <addr>:x2str also prints the C string that *x2 points to
    lab = 'L_%s: ' % a
    for n in sorted(os.listdir(gen)):
        if not n.startswith('c_'):
            continue
        p = os.path.join(gen, n)
        s = open(p).read()
        if lab not in s:
            continue
        probe = ('{ printf("probe %s x0=%%llx x1=%%llx x2=%%llx x3=%%llx lr=%%llx" "\\n", (unsigned long long)c->x[0], (unsigned long long)c->x[1], '
                 '(unsigned long long)c->x[2], (unsigned long long)c->x[3], (unsigned long long)c->x[30]); } ' % a)
        if kind == 'x2str':
            probe = ('{ const char *s_ = c->x[2] ? *(const char **)(uintptr_t)c->x[2] : 0; printf("probe %s x0=%%llx x1=%%llx x2str=%%s lr=%%llx" "\\n", '
                     '(unsigned long long)c->x[0], (unsigned long long)c->x[1], s_ ? s_ : "(null)", (unsigned long long)c->x[30]); } ' % a)
        if probe in s:
            print(a, 'already in', n)
            break
        s = s.replace(lab, lab + probe, 1)
        if '#include <stdio.h>' not in s:
            s = '#include <stdio.h>\n' + s
        open(p, 'w').write(s)
        print(a, 'probed in', n)
        break
    else:
        print(a, 'NOT FOUND')
