"""Lay the sources out in build/work the way ssbu-recomp's setup.py `stage` step does (runtime + lifter in one folder,
tools/nro as ro_study, tools/audit as tools_audit). Only files newer than their copy are copied; generated files in
build/work stay.

    python scripts/stage.py
"""
import os
import shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(REPO, 'build', 'work')
PLAN = [('runtime', ''), ('tools/lifter', ''), ('tools/nro', 'ro_study'), ('tools/audit', 'tools_audit'), ('fighter_sdk', 'fighter_sdk')]


def copy_tree(src, dst):
    n = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d != '__pycache__']
        out = os.path.join(dst, os.path.relpath(root, src))
        os.makedirs(out, exist_ok=True)
        for f in files:
            s, d = os.path.join(root, f), os.path.join(out, f)
            if not os.path.exists(d) or os.path.getmtime(d) < os.path.getmtime(s):
                shutil.copy2(s, d)
                n += 1
    return n


if __name__ == '__main__':
    total = sum(copy_tree(os.path.join(REPO, a), os.path.join(WORK, b)) for a, b in PLAN)
    print('staged into %s: %d files copied' % (WORK, total))
