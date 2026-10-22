#!/bin/sh
# Lift the SDK functions of tools/lifter/sdklift_roots.txt again (sdklift_gen.c, sdklift_install.h) in build/work.
#   sh scripts/regen_sdk.sh        then: sh scripts/dev_run.sh <log> <seconds> dll
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
PY=${SSBU_DEPS:-$REPO/../ssbu-recomp/deps}/python/python.exe
"$PY" "$REPO/scripts/stage.py" > /dev/null
cd "$REPO/build/work"
"$PY" -c "
import sys
sys.path.insert(0, '../../scripts')
import setup
open('sdklift_install.h', 'w', newline='\n').write(setup.install_header(open('sdklift_roots.txt', encoding='utf-8').read()))"
RECOMP_BIN=$(cygpath -w ../dump/exefs) "$PY" sdklift.py gen > sdklift_run.log 2>&1
head -1 sdklift_report.txt
echo "service calls (svc) reached by lifted code: $(grep -c 'svc in' sdklift_run.log)"
