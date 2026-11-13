/* tests/jit_prof.inc.h - AArch64 instructions executed per x86 guest opcode (test builds only: -DPEMU_JIT_EMBED -DPEMU_JIT_PROF).
 * Every AArch64 instruction the embedded interpreter executes is mapped back to the guest instruction it was generated for.
 * PEMU_JIT_PROF=1 prints, at exit, the guest opcodes that cost the most AArch64 instructions, with the average per execution. */
#ifdef PEMU_JIT_EMBED
extern void (*pemu_embed_cb)(u64 pc, int host);
#define JP_N (1 << 19)
static unsigned long long jp_words[JP_N], jp_exec[JP_N], jp_host[JP_N], jp_total, jp_tramp, jp_hostcalls, jp_stub;
static int jp_has_modrm(unsigned idx) {
    if (idx < 0x100) return (idx < 0x40 && (idx & 7) < 4) || idx == 0x63 || idx == 0x69 || idx == 0x6b || (idx >= 0x80 && idx <= 0x8f) || idx == 0xc0 || idx == 0xc1 || idx == 0xc6 || idx == 0xc7 || (idx >= 0xd0 && idx <= 0xd3) || idx == 0xf6 || idx == 0xf7 || idx == 0xfe || idx == 0xff;
    idx &= 255; return idx != 0x05 && idx != 0x0b && idx != 0x31 && idx != 0xa2 && !(idx >= 0x80 && idx <= 0x8f) && !(idx >= 0xc8 && idx <= 0xcf) && idx != 0xa0 && idx != 0xa1 && idx != 0xa8 && idx != 0xa9;
}
static unsigned jp_key(const u8 *q) {
    unsigned k = jfb_key(q);
    const u8 *p = q; while (*p == 0x66 || *p == 0xF3 || *p == 0xF2 || *p == 0x64 || *p == 0x65 || *p == 0x67 || *p == 0xF0 || (*p & 0xE7) == 0x26) p++;
    unsigned rep = (*q == 0xF3 || q[1] == 0xF3) ? 1 : 0; (void) rep;
    if ((*p & 0xF0) == 0x40) p++;
    unsigned idx = *p == 0x0F ? 256u + p[1] : *p;
    const u8 *mp = p + (*p == 0x0F ? 2 : 1);
    if (*p == 0x0F && (p[1] == 0x38 || p[1] == 0x3A)) { idx = 256u + p[1]; mp = p + 3; k = (k & ~0x1ffu) | idx; }
    if (!((k >> 17) & 1) && jp_has_modrm(idx) && (*mp >> 6) != 3) k |= 1u << 16;     /* memory form */
    if (!((k >> 17) & 1) && jp_has_modrm(idx)) k |= 1u << 17;
    k &= (1u << 18) - 1;
    return k;
}
static u64 jp_ipk[1 << 16], jp_ipw[1 << 16];
static void jp_cb(u64 pc, int host) {
    if (!jbase || pc < (u64) jbase || pc >= (u64) (jbase + JCACHE_SZ / 4)) { jp_tramp++; jp_total++; return; }
    if (host) { jp_hostcalls++; }
    u32 off = (u32)(pc - (u64) jbase); jp_total++;
    if (off < JTRAMP * 4) { jp_tramp++; return; }
    u32 lo = 0, hi = __atomic_load_n(&jmapn, __ATOMIC_ACQUIRE);
    if (!hi) return;
    while (hi - lo > 1) { u32 mid = (lo + hi) / 2; if (jm_off[mid] <= off) lo = mid; else hi = mid; }
    if (jm_off[lo] > off) return;
    { u64 ip = jm_pc[lo]; u32 h = (u32)((ip * 0x9E3779B97F4A7C15ULL) >> 48); while (jp_ipk[h] && jp_ipk[h] != ip) h = (h + 1) & 0xffff; jp_ipk[h] = ip; jp_ipw[h]++; }
    unsigned k = jp_key((const u8*) jm_pc[lo]);
    jp_words[k]++; if (jm_off[lo] == off) jp_exec[k]++; if (host) jp_host[k]++;
}
static __attribute__((constructor)) void jp_init(void) { if (getenv("PEMU_JIT_PROF")) pemu_embed_cb = jp_cb; }
static void jit_prof_report(void) {
    if (!pemu_embed_cb) return;
    pemu_embed_cb = NULL;
    unsigned long long tw = 0, te = 0;
    for (unsigned k = 0; k < JP_N; k++) { tw += jp_words[k]; te += jp_exec[k]; }
    fprintf(stderr, "prof: %llu AArch64 instructions executed (%llu in translated blocks for %llu guest instructions = %.2f each; %llu in entry/exit stubs, %llu host calls)\n",
            jp_total, tw, te, te ? (double) tw / (double) te : 0.0, jp_tramp, jp_hostcalls);
    fprintf(stderr, "prof: %-22s %12s %12s %8s %10s\n", "guest opcode", "a64 total", "guest exec", "avg", "hostcalls");
    if (getenv("PEMU_JIT_PROF_IP")) for (int n = 0; n < 25; n++) {
        u32 bh = 0; u64 bw = 0; for (u32 h = 0; h < 65536; h++) if (jp_ipw[h] > bw) { bw = jp_ipw[h]; bh = h; }
        if (!bw) break;
        fprintf(stderr, "prof-ip: %#lx %12llu  bytes:", (unsigned long) jp_ipk[bh], bw); for (int q = 0; q < 8; q++) fprintf(stderr, " %02x", ((u8*) jp_ipk[bh])[q]); fprintf(stderr, "\n"); jp_ipw[bh] = 0;
    }
    for (int n = 0; n < 40; n++) {
        unsigned b = 0; unsigned long long best = 0;
        for (unsigned k = 0; k < JP_N; k++) if (jp_words[k] > best) { best = jp_words[k]; b = k; }
        if (!best) break;
        char nm[48]; jfb_keyname(b, nm);
        if (!((b >> 17) & 1) && ((b >> 16) & 1)) strcat(nm, " m");
        fprintf(stderr, "prof: %-22s %12llu %12llu %8.2f %10llu\n", nm, jp_words[b], jp_exec[b], jp_exec[b] ? (double) jp_words[b] / (double) jp_exec[b] : 0.0, jp_host[b]);
        jp_words[b] = 0;
    }
}
#endif
