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

## Steps 2-4: SDK lift, runtime, boot (2026-10-21/22)

The whole SSBU runtime and build tooling are forked into this tree (`runtime/`, `tools/`, `scripts/`); the game-specific
answers live in `runtime/tomo.c`. What works:

- **The game boots and runs its frame loop at 30 fps for as long as it is left running** (tested 4 minutes): nninitStartup,
  4,599 static constructors, nnMain, 40 game threads, NVN device / queue / window, the RomFS (66,343 files), fonts, the
  audio renderer (a silent one: wave buffers are consumed at the sample rate, nothing is mixed yet), save data, controller.
- **It draws its own loading screen** (the yellow pattern with the cycling food icon) with the OpenGL back end
  (`SSBU_RENDERER=gl`, the default of `scripts/dev_run.sh`; the Vulkan back end lacks `nvnCommandBufferReportCounter`).
- Lifter: jump-table bases are no longer taken for function starts; DT_RELR relocations; `ctr_el0`, cache maintenance.
- NVN 55 differences handled: API version 55.15, 16-byte `NVNtextureView`, `Bind*Deferred`, `MemoryPoolMapVirtual`
  (whole-pool aliases; partial mappings are copied: a shortcut), window origin / depth mode set after start-up, command
  memory modelled at 4 bytes per command, texture storage from the runtime's own layout.
- SDK 20 services: light semaphores / message queues, timer events, the nn::os memory heap, SdkMutex / SdkConditionVariable
  cut to host locks, ~300 pure SDK functions lifted (prepo, mii values, vibration, png/jpeg, the nn::audio client).

**Where it is (2026-10-22, evening): in the game world.** The loading screen, the "demo save data detected" dialog
(OK), "transfer the demo save?" (Don't Transfer, then its confirmation) and the date-and-time check (Yes, Proceed) are
passed; the game loads its world and shows the island in the sea with the "take a look around the island" prompt,
at 14 fps while its shaders are first prepared, 30 fps on the dialogs. The scripted path:
`SSBU_SEQ="900:Z 500:l 60:Z 500:r 60:Z 800:Z 500:Z"` (poll-counted presses, nnhid.c) with `scripts/ps_run.ps1`.

Host bugs fixed on the way, each one a hang:

- **Lost event wake-up** (`os_threads.c`): the scene start-up signals a manual-clear light event and clears it at once
  (`SignalLightEvent` / `ClearLightEvent` in the task-group step at 0x71005e1f40). The host event woke the waiter, which
  re-checked `signaled`, found it cleared and slept again, so the Scene Prepare Thread never left `WaitLightEvent`
  (0x71004aee60). Events now carry a signal generation: a waiter released by a signal returns even if a clear followed.
- **GPU hang** (`mx_translate.c`): program #5401 (the cube map array mip chain) ends its vertex shader in a BRX jump
  through a table in the shader's constant buffer 1; the value read at run time differs from the table the translation
  saw, the computed target is no case of the dispatcher and the shader looped for ever. The driver reset the GPU after
  2 s (event log 4101) and ended the process with 0xC0000409, silently. A BRX target outside its table now ends the
  stage. Also: each stage's constant block is copied when the program is set (`stage_keep_cdata`, the game reuses that
  memory) and the shader cache keys carry a translator salt.
- **Layered render targets** (`nvn_gl.c`): one face / layer of a cube map array or 2D array as a colour target was a
  one-layer array view, a layered attachment next to the plain 2D depth target (GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS):
  now a 2D view of that level and layer (`rt_view`); 3D textures attach by layer. A draw that samples the texture it
  renders to samples a refreshed copy (`feedback_guard`).
- **A mutex born locked** (`os_threads.c`): the game destroys its GPU memory pools while holding their mutex
  (0x71002ab68c); the host object keyed by that address kept the lock, and the next pool made at the same address
  started locked by a thread that had moved on: the world load hung in 0x7100329cc4. `InitializeMutex` /
  `FinalizeMutex` (and the SDK mutex resets) drop such a lock.

Open renderer gaps: fragment shaders with `TEX (b)` (bindless texture handle in a register) are not translated; `VMNMX`
is (video min/max, yuzu's field layout, unverified against hardware); views of ASTC textures are refused by the driver
(sampled as the plain texture). Not touched yet: audio output, rumble, the Vulkan back end.

Diagnostics: `TOMO_SAMPLE=1` (1 ms function sampler of every guest thread and the main thread, report at exit, and a
thread / wait report after 3 s without a present), `TOMO_GPUTIME=<ms>` (every draw and dispatch finished and timed; with
`TOMO_GPUTIME_EVERY=<n>` only every n-th; the last 256 commands are kept in `gputime_last.txt` through a file mapping
that survives a GPU reset), `scripts/ps_run.ps1` (runs the game from PowerShell with the real exit code: under the bash
`timeout` wrapper a driver reset showed as exit 127 and a truncated log), `TOMO_EVLOG=1` (events waited on for over 2 s and who signals them), `TOMO_TASKLOG=1` (scene task states),
`TOMO_HEAPLOG=1|<heap name>`, `TOMO_ALIASLOG=1`, `SSBU_PEEK=<addr,...>` in the crash report, `tools/debug/*.py`
(disassemble main or sdk by address / name, callers, relocation of a slot, cross-references, host code at a DLL RVA).

## Reproduce

Everything below is in `scripts/dev_run.sh` (rebuild what changed and run for N seconds):
```
sh scripts/regen_sdk.sh                     # after a change of tools/lifter/sdklift_roots.txt
sh scripts/dev_run.sh run.log 60 dll        # dll = relink the game DLL too; log in build/work/run.log
```
First build from the dump:
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

1. Play from the island prompt on (cursor and camera; the game believes a demo save exists, which the fs / save
   answers in tomo.c decide) and look at what the world is missing visually.
2. Translate `TEX (b)` (bindless handle in a register: a table from NVN handle to GL bindless handle, filled by
   nvnDeviceGetTextureHandle).
3. Audio mixing in the silent renderer (voices -> a host audio device) once the game is past loading.
4. The Vulkan back end: `nvnCommandBufferReportCounter`.
5. Rumble (the cut `SendVibrationValue` calls can drive nnhid.c's SDL rumble).
