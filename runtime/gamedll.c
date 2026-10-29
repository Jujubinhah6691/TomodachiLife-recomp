/* Host side of the exe / ssbu_game.dll split (see game_shim.c).
 *
 * build.py compiles every host file with
 *     -Dfn_table=(*game_fn_table_p) -Dfn_table_count=(*game_fn_table_count_p)
 *     -Ddyn_syms=(*game_dyn_syms_p) -Ddyn_sym_count=(*game_dyn_sym_count_p)
 * so the tables declared in runtime.h become pointers into the DLL, filled in here before main(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "game_data.h"

typedef struct {
    void (*call_indirect)(Ctx *, uint64_t);
    void (*host_import)(Ctx *, int);
    void (*guest_trap)(const char *, uint64_t);
    void (*guest_unimpl)(uint64_t, uint32_t, const char *);
    void (*guest_svc)(Ctx *, uint32_t);
    void (*stx_diag)(Ctx *, uint64_t, uint64_t, uint64_t, uint64_t, int);
    void (*site_inline)(Ctx *, uint64_t);      /* appended (game_shim.c): an older DLL copies only the fields it knows */
    int (*site_midhook)(Ctx *, uint64_t);
} GameHost;

typedef struct {
    const FnEntry *table;
    const int *table_count;
    const DynSym *syms;
    const int *sym_count;
} GameTables;

#ifdef _WIN32
/* The exe loads the game DLL itself, so one folder can hold one per game version: main.elf of update 1.0.4 (v262144; argv[1],
 * code 0x256d250 bytes) runs <name>_v262144.dll when it is there, anything else <name>.dll (GAME_DLL_NAME, build.py).
 * hostrt.c then checks that the DLL was lifted from this main.elf. */
#include <windows.h>
static void (*game_bind)(const GameHost *h, GameTables *t);
static void (*game_data_bind)(const GameDataHost *h, GameData *d);
static uint64_t elf_text_size(const char *path) {           /* the first PT_LOAD's file size (nso2elf.py: text) */
    unsigned char h[64], ph[56];
    uint64_t phoff = 0, size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(h, 1, 64, f) == 64) {
        memcpy(&phoff, h + 0x20, 8);
        for (int i = 0, n = h[0x38] | h[0x39] << 8; i < n && !size; i++)
            if (!fseek(f, (long)(phoff + 56u * i), SEEK_SET) && fread(ph, 1, 56, f) == 56 && ph[0] == 1) memcpy(&size, ph + 32, 8);
    }
    fclose(f);
    return size;
}
static void load_game_dll(void) {
    char path[MAX_PATH + 64], *slash;
    HMODULE m = NULL;
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    slash = n ? strrchr(path, '\\') : NULL;
    if (!slash) slash = path - 1;
    if (__argc > 1 && elf_text_size(__argv[1]) == 0x256d250) {
        snprintf(slash + 1, 64, "%.*s_v262144.dll", (int)strlen(GAME_DLL_NAME) - 4, GAME_DLL_NAME);
        m = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!m) { snprintf(slash + 1, 64, "%s", GAME_DLL_NAME); m = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH); }
    if (m) {
        game_bind = (void (*)(const GameHost *, GameTables *))(void *)GetProcAddress(m, "game_bind");
        game_data_bind = (void (*)(const GameDataHost *, GameData *))(void *)GetProcAddress(m, "game_data_bind");
    }
    if (!game_bind || !game_data_bind) {
        void romfs_fail(const char *msg);                   /* boot.c */
        romfs_fail("The game library " GAME_DLL_NAME " beside the exe is missing or cannot be loaded: build the game first.");
    }
}
#else
__declspec(dllimport) void game_bind(const GameHost *h, GameTables *t);
/* game_data.h: the dump-derived tables and the lifted SDK code, in the DLL too (game_data.c) */
__declspec(dllimport) void game_data_bind(const GameDataHost *h, GameData *d);
static void load_game_dll(void) {}
#endif
GameData game_data;
void sdk_host_call(Ctx *c, int index);                   /* sdklift_rt.c */
void sdk_trap_call(Ctx *c, const char *name);
void sdk_dispatch(Ctx *c, uint64_t addr);
void *sdk_jmp_record(Ctx *c);
void sdk_jmp_restore(Ctx *c, void *r);
void *sdk_longjmp_target(Ctx *c);

/* The site callouts of a DLL re-lifted from a site list (tools/lifter/relift_sites.py) go to the Skyline layer
 * (skyline.c, included by nnro.c). */
void sky_site_inline(Ctx *c, uint64_t addr);
int sky_site_midhook(Ctx *c, uint64_t addr);

#undef fn_table
#undef fn_table_count
#undef dyn_syms
#undef dyn_sym_count
const FnEntry (*game_fn_table_p)[];
const int *game_fn_table_count_p;
const DynSym (*game_dyn_syms_p)[];
const int *game_dyn_sym_count_p;

__attribute__((constructor)) static void bind_game(void) {
    GameHost h = {call_indirect, host_import, guest_trap, guest_unimpl, guest_svc, stx_diag, sky_site_inline, sky_site_midhook};
    GameTables t;
    load_game_dll();
    game_bind(&h, &t);
    game_fn_table_p = (const FnEntry (*)[])t.table;
    game_fn_table_count_p = t.table_count;
    game_dyn_syms_p = (const DynSym (*)[])t.syms;
    game_dyn_sym_count_p = t.sym_count;
    GameDataHost dh = {GAME_DATA_VERSION, sdk_host_call, sdk_trap_call, sdk_dispatch, sdk_jmp_record, sdk_jmp_restore, sdk_longjmp_target};
    game_data_bind(&dh, &game_data);
    if (game_data.version != GAME_DATA_VERSION || game_data.size != (int)sizeof game_data) {
        fprintf(stderr, "the game DLL was built for another version of this exe (game data interface %d, this exe %d): build both "
                        "from the same tree (scripts/setup.py)\n", game_data.version, GAME_DATA_VERSION);
        exit(9);
    }
}
