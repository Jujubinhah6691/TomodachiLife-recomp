/* bntx_png: one texture of a BNTX (the game's texture archive) as a PNG, through the runtime's decoder (tex_decode.c).
 *   bntx_png <file.bntx>                    lists the textures
 *   bntx_png <file.bntx> <name> <out.png>   writes that texture's level 0
 * Used by scripts/launcher_art.py: the launcher's logo comes from the player's own game files.
 * Build: clang -O2 tools/extract/bntx_png.c runtime/tex_decode.c -Iruntime -o bntx_png.exe */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tex_decode.h"

static uint8_t *g_d;
static size_t g_n;
static uint16_t u16(size_t o) { return o + 2 <= g_n ? (uint16_t)(g_d[o] | g_d[o + 1] << 8) : 0; }
static uint32_t u32(size_t o) { return o + 4 <= g_n ? (uint32_t)u16(o) | (uint32_t)u16(o + 2) << 16 : 0; }
static uint64_t u64(size_t o) { return (uint64_t)u32(o) | (uint64_t)u32(o + 4) << 32; }

/* BNTX format (high byte: kind, low byte: type) -> NVN format of the runtime's decoder; 0 = not handled */
static int nvn_of(uint32_t f) {
    int kind = (int)(f >> 8), srgb = (f & 0xff) == 6;
    if (kind == 0x0b) return srgb ? 0x38 /* RGBA8 sRGB */ : 0x25 /* RGBA8 */;
    if (kind >= 0x2d && kind <= 0x3a) return (srgb ? 0x87 : 0x79) + (kind - 0x2d);      /* ASTC 4x4 .. 12x12 */
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: bntx_png <file.bntx> [<name> <out.png>]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); g_n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    g_d = malloc(g_n);
    if (!g_d || fread(g_d, 1, g_n, f) != g_n) { printf("cannot read %s\n", argv[1]); return 1; }
    fclose(f);
    if (memcmp(g_d, "BNTX", 4)) { printf("%s is not a BNTX\n", argv[1]); return 1; }
    uint32_t count = u32(0x24);
    uint64_t arr = u64(0x28);
    for (uint32_t i = 0; i < count; i++) {
        size_t b = (size_t)u64(arr + 8 * i);
        if (memcmp(g_d + b, "BRTI", 4)) continue;
        uint32_t fmt = u32(b + 0x1c), w = u32(b + 0x24), h = u32(b + 0x28), bh = u32(b + 0x34);
        size_t name = (size_t)u64(b + 0x60), data = (size_t)u64((size_t)u64(b + 0x70));
        char nm[128];
        int len = u16(name);
        snprintf(nm, sizeof nm, "%.*s", len < 127 ? len : 127, (const char *)g_d + name + 2);
        if (argc < 4) { printf("%-40s format %04x %ux%u\n", nm, fmt, w, h); continue; }
        if (strcmp(nm, argv[2])) continue;
        int nf = nvn_of(fmt);
        const TexFormat *tf = nf ? tex_format(nf) : NULL;
        if (!tf) { printf("%s: format %04x not handled\n", nm, fmt); return 1; }
        int wb = (int)((w + tf->bw - 1) / tf->bw) * tf->bpb, hb = (int)(h + tf->bh - 1) / tf->bh;
        size_t sw = tex_bl_size(wb, hb, 1, (int)bh, 0);
        if (data + sw > g_n) { printf("%s: data past the end of the file\n", nm); return 1; }
        uint8_t *lin = malloc((size_t)wb * hb), *rgba = malloc((size_t)w * h * 4);
        tex_bl_to_linear(lin, g_d + data, wb, hb, 1, (int)bh, 0);
        if (tex_decode_rgba8(nf, lin, (int)w, (int)h, rgba) || tex_write_png(argv[3], rgba, (int)w, (int)h)) { printf("%s: decode or write failed\n", nm); return 1; }
        printf("%s: %ux%u -> %s\n", nm, w, h, argv[3]);
        return 0;
    }
    if (argc >= 4) { printf("no texture %s in %s\n", argv[2], argv[1]); return 1; }
    return 0;
}
