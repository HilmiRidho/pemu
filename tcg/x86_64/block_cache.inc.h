/* tcg/x86_64/block_cache.inc.h - block cache tables and cache-coherence hooks. SPDX-License-Identifier: Apache-2.0 */
/* ---- tables */
static u32 hpc(u64 pc, int bits) { return (u32)(((pc >> 2) * 0x9E3779B97F4A7C15ULL) >> (64 - bits)); }
static u8 *jfind(u64 pc) {
    u32 h = hpc(pc, JH_BITS);
    for (;;) { if (jh[h].pc == pc) return jh[h].code; if (!jh[h].pc) return NULL; h = (h + 1) & ((1u << JH_BITS) - 1); }
}
static void jins(u64 pc, u8 *code) {
    u32 h = hpc(pc, JH_BITS);
    while (jh[h].pc) h = (h + 1) & ((1u << JH_BITS) - 1);
    jh[h].pc = pc; jh[h].code = code; jhn++;
}
static void jpg_add(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 2, JPG_BITS);
    for (;;) { if (jpg[h] == k) return; if (!jpg[h]) { jpg[h] = k; jpgn++; return; } h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static int jpg_has(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 2, JPG_BITS);
    for (;;) { if (jpg[h] == k) return 1; if (!jpg[h]) return 0; h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static void jflush(void) {
    jcur = jbase + JTRAMP_SZ;
    memset(jh, 0, sizeof(JEnt) << JH_BITS); memset(jic, 0, sizeof(JEnt) << JIC_BITS); memset(jpg, 0, sizeof(u64) << JPG_BITS);
    jhn = jmapn = jpgn = 0; cpu.patch = 0; jstat_flush++;
}
static int jit_has_code(u64 addr, u64 len) {
    if (!g_jit || !jpgn || !len) return 0;
    u64 p0 = addr >> 12, p1 = (addr + len - 1) >> 12;
    if (p1 < p0) p1 = ~0ULL >> 12;
    if (p1 - p0 < 64) { for (u64 p = p0; p <= p1; p++) if (jpg_has(p)) return 1; return 0; }
    for (u32 k = 0; k < (1u << JPG_BITS); k++) if (jpg[k]) { u64 p = jpg[k] & ~(1ULL << 63); if (p >= p0 && p <= p1) return 1; }
    return 0;
}
static void jit_syscall_hook(u64 n, u64 a0, u64 a1, u64 a2, u64 a3) {
    int fl = 0;
    switch (n) {
    case 215: fl = jit_has_code(a0, a1); break;                       /* munmap */
    case 216: fl = jit_has_code(a0, a1); break;                       /* mremap */
    case 226: fl = jit_has_code(a0, a1); break;                       /* mprotect */
    case 222: fl = (a3 & MAP_FIXED) && jit_has_code(a0, a1); (void) a2; break;   /* mmap */
    default: break;
    }
    if (fl) jflush();
}
static u64 jit_rip2pc(u64 rip) {
    if (!g_jit || rip < (u64) jbase || rip >= (u64) jcur || !jmapn) return 0;
    u32 off = (u32)(rip - (u64) jbase), lo = 0, hi = jmapn;
    while (hi - lo > 1) { u32 mid = (lo + hi) / 2; if (jm_off[mid] <= off) lo = mid; else hi = mid; }
    return jm_off[lo] <= off ? jm_pc[lo] : 0;
}
