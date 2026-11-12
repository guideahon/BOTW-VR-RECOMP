// Function-entry profiler. SAVAGEWIND_FNPROF=<from>:<to> counts guest function entries (every generated
// function calls FN_ENTER) between those presented frames, then writes build/fnprof.txt (addr count, most
// called first). Diffing two runs shows which game functions a change brings in; counts also show hot code.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "platform.h"

volatile int g_fnprof;

#define NSLOT (1 << 18)
static struct { _Atomic uint64_t addr; _Atomic uint64_t count; } slots[NSLOT];

// SAVAGEWIND_FNPROF_BT=<hex addr>[,...]: also print the host backtrace (guest functions are f_<addr>) the first time
// each listed function is entered inside the window - who calls it
static uint64_t bt_addrs[256];
static int bt_n = -1;
static _Atomic uint8_t bt_done[256];

uint64_t g_bt_first_x0;

static void bt_check(uint64_t a, uint64_t lr, uint64_t x0) {
    if (bt_n < 0) {
        bt_n = 0;
        const char *e = getenv("SAVAGEWIND_FNPROF_BT");
        for (const char *p = e; p && *p && bt_n < 256;) {
            bt_addrs[bt_n++] = strtoull(p, (char **)&p, 16);
            while (*p == ',' || *p == ' ') p++;
        }
    }
    for (int i = 0; i < bt_n; i++)
        if (bt_addrs[i] == a && !atomic_exchange(&bt_done[i], 1)) {
            if (i == 0) g_bt_first_x0 = x0;
            uint64_t vt = x0 >= 0x7000000000ull && x0 < 0x7800000000ull ? *(uint64_t *)(uintptr_t)x0 : 0;
            uint64_t v100 = vt >= 0x7000000000ull && vt < 0x7800000000ull ? *(uint64_t *)(uintptr_t)(vt + 0x100) : 0;
            uint64_t p8 = x0 >= 0x7000000000ull && x0 < 0x7800000000ull ? *(uint64_t *)(uintptr_t)(x0 + 8) : 0;
            fprintf(stderr, "[fnprof-bt] %llx entered from guest lr %llx x0 %llx (vtable %llx, +0x100 %llx, [x0+8] %llx):\n",
                    (unsigned long long)a, (unsigned long long)lr, (unsigned long long)x0, (unsigned long long)vt,
                    (unsigned long long)v100, (unsigned long long)p8);
            plat_print_backtrace();  // (execinfo is macOS / glibc only)
        }
}

// SAVAGEWIND_FNPROF_ARGS=<hex addr>[,...]: log x0-x5 and the caller on each entry of those functions (the first
// SAVAGEWIND_FNPROF_ARGS_MAX entries each, default 64) - what a function is called with, without a debugger - and
// what one register points to: SAVAGEWIND_FNPROF_DEREF=<reg>:<qwords> (default 1:4, x1: often a descriptor or out
// argument).
static uint64_t args_addrs[64];
static int args_n = -1, args_max = 64, deref_reg = 1, deref_n = 4;
static _Atomic int args_count[64];

static void args_check(uint64_t a, uint64_t lr) {
    if (args_n < 0) {
        args_n = 0;
        const char *e = getenv("SAVAGEWIND_FNPROF_ARGS");
        if (getenv("SAVAGEWIND_FNPROF_ARGS_MAX")) args_max = atoi(getenv("SAVAGEWIND_FNPROF_ARGS_MAX"));
        if (getenv("SAVAGEWIND_FNPROF_DEREF")) sscanf(getenv("SAVAGEWIND_FNPROF_DEREF"), "%d:%d", &deref_reg, &deref_n);
        if (deref_reg < 0 || deref_reg > 30) deref_reg = 1;
        if (deref_n < 0 || deref_n > 64) deref_n = 4;
        for (const char *p = e; p && *p && args_n < 64;) {
            args_addrs[args_n++] = strtoull(p, (char **)&p, 16);
            while (*p == ',' || *p == ' ') p++;
        }
    }
    for (int i = 0; i < args_n; i++)
        if (args_addrs[i] == a && atomic_fetch_add(&args_count[i], 1) < args_max) {
            Cpu *c = kernel_current_cpu();
            if (!c) return;
            char line[1600];
            int n = snprintf(line, sizeof line, "[fnprof-args] %llx from %llx: %llx %llx %llx %llx %llx %llx [x%d:",
                             (unsigned long long)a, (unsigned long long)lr, (unsigned long long)c->x[0],
                             (unsigned long long)c->x[1], (unsigned long long)c->x[2], (unsigned long long)c->x[3],
                             (unsigned long long)c->x[4], (unsigned long long)c->x[5], deref_reg);
            uint64_t p = c->x[deref_reg];
            for (int k = 0; k < deref_n && p >= GUEST_LO && p + 8 * (k + 1) <= GUEST_HI; k++) {
                uint64_t v = ((uint64_t *)(uintptr_t)p)[k];
                n += snprintf(line + n, sizeof line - n, " %llx", (unsigned long long)v);
                const char *s = (const char *)(uintptr_t)v;  // a pointer to text (a name): show it
                int len = 0;
                if (v >= GUEST_LO && v + 64 <= GUEST_HI)
                    while (len < 64 && s[len] >= 0x20 && s[len] < 0x7f) len++;
                if (len >= 4 && (len == 64 || !s[len]) && n < (int)sizeof line - 80)
                    n += snprintf(line + n, sizeof line - n, "=\"%.*s\"", len, s);
            }
            fprintf(stderr, "%s]\n", line);
        }
}

// `fnprof start <x> <y> <z> <r>` (reverse engineering): also which functions get, in x0..x7, a pointer to three floats
// within r of that point (at most 0x60 bytes in): (function, register, offset) counted, written to <file>.near
static float near_p[3], near_r;
static volatile int near_on;
static struct { _Atomic uint64_t key; _Atomic uint64_t count; } nslots[1 << 14];

static void near_check(uint64_t a) {
    Cpu *c = kernel_current_cpu();
    if (!c) return;
    for (int r = 0; r < 8; r++) {
        uint64_t p = c->x[r];
        extern bool kernel_guest_mapped(uint64_t a, uint64_t n);
        if (p < GUEST_LO || p > GUEST_HI - 0x6c || (p & 3) || !kernel_guest_mapped(p, 0x6c)) continue;   // (no wrap)
        const float *f = (const float *)(uintptr_t)p;
        for (int k = 0; k < 0x60 / 4; k++) {
            float dx = f[k] - near_p[0], dy = f[k + 1] - near_p[1], dz = f[k + 2] - near_p[2];
            if (!(dx * dx + dy * dy + dz * dz <= near_r * near_r)) continue;
            uint64_t key = a | ((uint64_t)(r * 256 + k * 4) << 48);
            uint32_t h = (uint32_t)((key >> 2) * 0x9E3779B1u) & ((1 << 14) - 1);
            for (int n = 0; n < (1 << 14); n++, h = (h + 1) & ((1 << 14) - 1)) {
                uint64_t cur = atomic_load(&nslots[h].key);
                if (cur == key || (cur == 0 && atomic_compare_exchange_strong(&nslots[h].key, &cur, key)) || cur == key) {
                    atomic_fetch_add(&nslots[h].count, 1);
                    break;
                }
            }
            break;
        }
    }
}

void fnprof_hit(uint64_t a, uint64_t lr, uint64_t x0) {
    if (near_on) near_check(a);
    if (bt_n) bt_check(a, lr, x0);
    if (args_n) args_check(a, lr);
    uint32_t h = (uint32_t)((a >> 2) * 0x9E3779B1u) & (NSLOT - 1);
    for (int n = 0; n < NSLOT; n++, h = (h + 1) & (NSLOT - 1)) {
        uint64_t cur = atomic_load_explicit(&slots[h].addr, memory_order_relaxed);
        if (cur == a || (cur == 0 && atomic_compare_exchange_strong(&slots[h].addr, &cur, a)) || cur == a) {
            atomic_fetch_add_explicit(&slots[h].count, 1, memory_order_relaxed);
            return;
        }
    }
}

static int cmp(const void *x, const void *y) {
    uint64_t a = ((const uint64_t *)x)[1], b = ((const uint64_t *)y)[1];
    return a < b ? 1 : a > b ? -1 : 0;
}

void fnprof_frame(uint64_t frame) {
    static long from = -2, to;
    if (from == -2) {
        from = -1;
        const char *e = getenv("SAVAGEWIND_FNPROF");
        if (e && sscanf(e, "%ld:%ld", &from, &to) != 2) from = -1;
    }
    if (from < 0) return;
    if ((long)frame == from) { g_fnprof = 1; LOG("[fnprof] on at frame %llu\n", (unsigned long long)frame); }
    if ((long)frame != to) return;
    g_fnprof = 0;
    uint64_t (*rows)[2] = malloc(sizeof(uint64_t[2]) * NSLOT);
    int n = 0;
    for (int i = 0; i < NSLOT; i++)
        if (slots[i].addr) { rows[n][0] = slots[i].addr; rows[n][1] = slots[i].count; n++; }
    qsort(rows, n, sizeof rows[0], cmp);
    const char *out = getenv("SAVAGEWIND_FNPROF_OUT") ? getenv("SAVAGEWIND_FNPROF_OUT") : "build/fnprof.txt";  // (parallel runs)
    FILE *f = fopen(out, "w");
    for (int i = 0; f && i < n; i++) fprintf(f, "%llx %llu\n", (unsigned long long)rows[i][0], (unsigned long long)rows[i][1]);
    if (f) fclose(f);
    free(rows);
    LOG("[fnprof] %d functions, written %s (frames %ld-%ld)\n", n, out, from, to);
    from = -1;
}

// the `fnprof` command (control_prof.c): counting turned on (the counts cleared) and off (written to path) at any time
void fnprof_start(const float *near, float r) {
    for (int i = 0; i < NSLOT; i++) atomic_store(&slots[i].addr, 0), atomic_store(&slots[i].count, 0);
    for (int i = 0; i < (1 << 14); i++) atomic_store(&nslots[i].key, 0), atomic_store(&nslots[i].count, 0);
    near_on = near != NULL;
    if (near) memcpy(near_p, near, sizeof near_p), near_r = r;
    g_fnprof = 1;
}

int fnprof_stop(const char *path) {
    g_fnprof = 0;
    if (near_on) {
        near_on = 0;
        char np[600];
        snprintf(np, sizeof np, "%s.near", path);
        FILE *nf = fopen(np, "w");
        for (int i = 0; nf && i < (1 << 14); i++)
            if (nslots[i].key) {
                uint64_t k = nslots[i].key;
                fprintf(nf, "%llx x%d +%#x %llu\n", (unsigned long long)(k & 0xffffffffffffull), (int)(k >> 56),
                        (unsigned)((k >> 48) & 0xff), (unsigned long long)nslots[i].count);
            }
        if (nf) fclose(nf);
    }
    uint64_t (*rows)[2] = malloc(sizeof(uint64_t[2]) * NSLOT);
    int n = 0;
    for (int i = 0; i < NSLOT; i++)
        if (slots[i].addr) { rows[n][0] = slots[i].addr; rows[n][1] = slots[i].count; n++; }
    qsort(rows, n, sizeof rows[0], cmp);
    FILE *f = fopen(path, "w");
    for (int i = 0; f && i < n; i++) fprintf(f, "%llx %llu\n", (unsigned long long)rows[i][0], (unsigned long long)rows[i][1]);
    if (f) fclose(f);
    free(rows);
    return f ? n : -1;
}

// SAVAGEWIND_FNPROF=0:<to>: profile from process start (threads the game starts before the first frame)
__attribute__((constructor)) static void fnprof_early(void) {
    const char *e = getenv("SAVAGEWIND_FNPROF");
    if (e && e[0] == '0' && e[1] == ':') g_fnprof = 1;
}

// (reverse engineering, the `vr fnprof` control command) a window opened and closed at run time: counts reset at
// start, written to path at stop (addr count, most called first)
void fnprof_live_start(void) {
    for (int i = 0; i < NSLOT; i++) slots[i].addr = 0, slots[i].count = 0;
    g_fnprof = 1;
}

int fnprof_live_stop(const char *path) {
    g_fnprof = 0;
    uint64_t (*rows)[2] = malloc(sizeof(uint64_t[2]) * NSLOT);
    int n = 0;
    for (int i = 0; i < NSLOT; i++)
        if (slots[i].addr) { rows[n][0] = slots[i].addr; rows[n][1] = slots[i].count; n++; }
    qsort(rows, n, sizeof rows[0], cmp);
    FILE *f = fopen(path, "w");
    for (int i = 0; f && i < n; i++) fprintf(f, "%llx %llu\n", (unsigned long long)rows[i][0], (unsigned long long)rows[i][1]);
    if (f) fclose(f);
    free(rows);
    return n;
}
