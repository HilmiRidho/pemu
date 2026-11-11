/* tcg/aarch64/block_cache.inc.h - JIT block cache. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ block cache */
static inline u32 hpc(u64 pc, int bits) { return (u32)((pc * 0x9E3779B97F4A7C15ULL) >> (64 - bits)); }
static inline u32 icidx(u64 pc) { return (u32)((pc ^ (pc >> 13)) & ((1u << JIC_BITS) - 1)); }
static u32 *jfind(u64 pc) {
    u32 h = hpc(pc, JH_BITS);
    for (;;) {
        u64 k = __atomic_load_n(&jh[h].pc, __ATOMIC_ACQUIRE);
        if (k == pc) return jh[h].code;
        if (!k) return NULL;
        h = (h + 1) & ((1u << JH_BITS) - 1);
    }
}
static u32 *jfind_emit(u64 pc) { return pc == jt_pc && jt_start ? jt_start : jfind(pc); }      /* while translating: the block under construction is a valid target */
static void jins(u64 pc, u32 *code) {                           /* jmu held; the block is complete */
    u32 h = hpc(pc, JH_BITS);
    while (jh[h].pc) h = (h + 1) & ((1u << JH_BITS) - 1);
    jh[h].code = code; __atomic_store_n(&jh[h].pc, pc, __ATOMIC_RELEASE); jhn++;
}
static void jpg_add(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 12, JPG_BITS);
    for (;;) { if (jpg[h] == k) return; if (!jpg[h]) { jpg[h] = k; jpgn++; return; } h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static int jpg_has(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 12, JPG_BITS);
    for (;;) { if (jpg[h] == k) return 1; if (!jpg[h]) return 0; h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static int jit_has_code(u64 addr, u64 len) {
    if (!jpgn || !len) return 0;
    u64 p0 = addr >> 12, p1 = (addr + len - 1) >> 12;
    if (p1 < p0) p1 = ~0ULL >> 12;
    if (p1 - p0 < 64) { for (u64 p = p0; p <= p1; p++) if (jpg_has(p)) return 1; return 0; }
    for (u32 k = 0; k < (1u << JPG_BITS); k++) if (jpg[k]) { u64 p = jpg[k] & ~(1ULL << 63); if (p >= p0 && p <= p1) return 1; }
    return 0;
}
static void jflush(void) {                                      /* only through jit_stw_flush(): no thread is in the cache */
    jcur = jbase + JTRAMP;
    memset(jh, 0, sizeof(JEnt) << JH_BITS); memset(jpg, 0, sizeof(u64) << JPG_BITS);
    jhn = jmapn = jpgn = 0; jst_flush++;
}
static int jit_is_rwx(u64 pc) {
    if (jrwx_over) return 1;
    u64 p = pc & ~4095ULL; u32 nr = __atomic_load_n(&jrwxn, __ATOMIC_ACQUIRE);
    for (u32 k = 0; k < nr; k++) if (p < jrwx[k][1] && p + 4096 > jrwx[k][0]) return 1;
    return 0;
}
static void jit_note_rwx(u64 start, u64 len) {                  /* jmu held */
    if (jrwxn < JRWX_MAX) { jrwx[jrwxn][0] = start; jrwx[jrwxn][1] = start + len; __atomic_store_n(&jrwxn, jrwxn + 1, __ATOMIC_RELEASE); } else jrwx_over = 1;
    if (jbase && jit_has_code(start, len)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
}

