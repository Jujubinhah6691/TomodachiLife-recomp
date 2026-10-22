# TomodachiLife-recomp

Static recompilation of *Tomodachi Life: Living the Dream* (Switch, title 010051F0207B2000, v0) to a native PC
program, built on the tools and runtime of [ssbu-recomp](../ssbu-recomp) (OpenSmash-Ultimate).

No game code, assets or keys go in this repository. Everything derived from the game is made from your own dump into
`build/` (ignored by git).

## Status: feasibility study done (2026-10-21)

| Check | Result |
|---|---|
| ExeFS extracted with `ssbu-recomp/tools/extract/dump_folder.py` | `main` (code 0x25ce6a0), `sdk`, `rtld`, no subsdk |
| SDK | NintendoSDK 20.5.3 (SSBU is far older) |
| Engine | Nintendo EPD stack: `sead`, `agl`, `xlink`, NintendoWare G3d2 / Ui2d / Font / Vfx2, Havok 2023.2 physics, NVN graphics |
| Lifter coverage (`recomp.py cover`) | 71,289 functions; 99.66% of words translated. The other 0.34% are almost all zero padding (32,796 words); about 1,150 real SIMD / misc instructions are not handled yet |
| SDK imports by `main` | 779; 337 are also imported by SSBU, **442 are new** (219 `nn::audio`, 46 `nn::hid`, 35 `nn::os`, 25 `nn::prepo`, 19 `nn::swkbd`, 14 `nn::mii`, ...). List: `build/imports_missing.txt` |

## Step 1: lifter coverage and full lift (2026-10-21)

The lifter is a copy of `ssbu-recomp/tools/lifter/recomp.py` and `recomp_eh.py` (commit 8323f2b), with
`runtime/neon.h`, `runtime.h`, `fpexact.h` and `eh_runtime.h` from the same commit. Changes:

- **Instructions added** (`recomp.py`, helpers at the end of `runtime/neon.h`): ORR / BIC (vector, immediate),
  FMAXP / FMINP / FMAXNMP / FMINNMP (vector and scalar), FACGE / FACGT, the three-different long forms (S/U ADDL, ADDW,
  SUBL, SUBW, ABAL, ABDL, MLAL, MLSL, MULL and their "2" forms), SQADD / UQADD / SQSUB / UQSUB, SABD / UABD / SABA /
  UABA, MLA / MLS (vector), CLZ / CLS (vector), SHLL, SQXTN / UQXTN / SQXTUN, the saturating shift-right-narrow
  family (SQ(R)SHRUN, SQ(R)SHRN, UQ(R)SHRN), FRINTN/M/P/Z/A/X/I and FCVTNS/NU/MS/MU/PS/PU/AS/AU (vector), LD2R / LD3R /
  LD4R. XTN2 now writes the upper half (SSBU's `v_xtn` ignored the "2" form). Saturation does not set FPSR.QC.
- **Every real instruction of `main` is now translated.** What is left is zero padding and trap words after calls
  that do not return (`guest_unimpl` if ever reached).
- **Differential test** (`difftest.py`, Unicorn as reference): 38,598 tests, all identical, including every new form.
- **Mid entries share their host's body** (`recomp_eh.py`): recomp.py lifts a branch target inside another function
  as a separate copy of the rest of that function. A few huge functions here have hundreds of such targets, which made
  3.0 GB of C (one 509 MB file). Now they are resume points of one `u_<host>(c, rp)` body, the same scheme the
  exception landing pads use: 795 MB, biggest file 2.3 MB, 40,160 mid entries in 7,927 hosts (all checked to have
  their `case` and label).
- Lift: 111,510 functions, 807 files. The game has no C++ exceptions (no `__cxa_throw` import, 0 LSDAs).
- `tools/lifter/compile_main.py` compiles the lift with clang 9 -O1 (falls back to -O0 for a file over 15 minutes).
  **All 809 files compile at -O1** (530 s, 12 jobs, 216 MB of objects; no file needed -O0).
- Link check (`lld-link -dll -noentry -force:unresolved`): a 158 MB DLL whose only unresolved symbols are the C math
  and memory functions (`memcpy`, `floor`, `fma`, ...) and the runtime's `call_indirect`, `host_import`, `guest_trap`,
  `guest_unimpl`, `stx_diag`. The SSBU runtime provides all of them. The 779 SDK imports go through `host_import`.
- Not tested yet: running any lifted function. The mid-entry change has the same shape as the landing pads SSBU runs,
  but nothing has executed it in this game yet.

## Reproduce

```
set SSBU_PROD_KEYS=<your prod.keys>
python ..\ssbu-recomp\tools\extract\dump_folder.py <game.nsp> <game.nsp> build\dump --exefs-only
python ..\ssbu-recomp\tools\extract\nso2elf.py build\dump\exefs\main build\dump\exefs\main.elf
set RECOMP_BIN=build\dump\exefs
cd tools\lifter
python recomp.py cover
python difftest.py sample && python difftest.py gen
clang -O0 -w -fno-strict-aliasing -I ..\..\runtime difftest_gen.c difftest_host.c -o ..\..\build\difftest.exe
set DIFFTEST_EXE=..\..\build\difftest.exe
python difftest.py check
python recomp_eh.py ..\..\build\gen_eh 16
python compile_main.py ..\..\build\gen_eh ..\..\build\obj 12
```

(`python` is `..\ssbu-recomp\deps\python\python.exe`, `clang` is `..\ssbu-recomp\deps\llvm\bin\clang.exe`, clang 9.)

The keys must cover key generation 0x0f or newer, since this game is newer than the keys in some emulator folders.

## Next steps

1. ~~Handle the remaining SIMD encodings in the lifter, then do a full lift of `main` and compile it.~~ Done, see above.
2. Lift the game's own `sdk` module (SDK 20) with `sdklift.py` in place of SSBU's 13.x one.
3. Runtime: reuse the SSBU runtime's NVN-to-Vulkan, fs, hid, os and thread layers; write `nn::audio` (AudioRenderer)
   new, because SSBU never used it; stub `prepo`, `ngc`, `pctl`, `ssl`.
4. Boot to the first frame: `sead::GameFrameworkNx` start-up, then the RomFS (full `dump_folder.py` without `--exefs-only`).
