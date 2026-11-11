/* target/x86_64/interp.inc.h - interpreter step, cpuid, div. SPDX-License-Identifier: Apache-2.0 */
static void do_div(CPU*c, int sz, u64 src, int sgd, const u8*ip) {
    int bits = sz * 8;
    if (sz == 1) {                                  /* AX / src8 */
        u64 ax = c->r[RAX] & 0xffff;
        if (!(src & 0xff)) { g_os->exception(c, EXCP_DE); return; }
        if (sgd) { s16 n = (s16) ax; s8 dv = (s8) src; if (dv == -1 && n == -32768) { g_os->exception(c, EXCP_DE); return; } s32 q = n / dv, r = n % dv; if (q > 127 || q < -128) { g_os->exception(c, EXCP_DE); return; } c->r[RAX] = (c->r[RAX] & ~0xffffULL) | (u8) q | ((u64)(u8) r << 8); }
        else { u32 q = (u32) ax / (u8) src, r = (u32) ax % (u8) src; if (q > 255) { g_os->exception(c, EXCP_DE); return; } c->r[RAX] = (c->r[RAX] & ~0xffffULL) | q | (r << 8); }
        return;
    }
    u64 m = mskz(sz); src &= m;
    if (!src) { g_os->exception(c, EXCP_DE); return; }
    unsigned __int128 un = ((unsigned __int128)(c->r[RDX] & m) << bits) | (c->r[RAX] & m);
    if (sgd) {
        __int128 n = (__int128) un; if (sz < 8) n = (__int128)((s64)(un << (64 - 2 * bits)) >> (64 - 2 * bits)); else n = (__int128) un;
        __int128 dv = sx(src, sz), q, r;
        if (dv == -1 && n == -((__int128) 1 << (bits - 1))) { g_os->exception(c, EXCP_DE); return; }
        q = n / dv; r = n % dv;
        if (q > (((__int128) 1 << (bits - 1)) - 1) || q < -((__int128) 1 << (bits - 1))) { g_os->exception(c, EXCP_DE); return; }
        setr(c, RAX, sz, 0, (u64) q); setr(c, RDX, sz, 0, (u64) r);
    } else {
        unsigned __int128 q = un / src, r = un % src;
        if (q > m) { g_os->exception(c, EXCP_DE); return; }
        setr(c, RAX, sz, 0, (u64) q); setr(c, RDX, sz, 0, (u64) r);
    }
    (void) ip;
}

static u64 tsc_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; }

/* 128-bit compare-and-swap on a 16-byte aligned location (cmpxchg16b). exp[0..1] = expected (rax, rdx); on failure it receives
 * the value found. Inline assembly because gcc would call libatomic for a 16-byte __atomic builtin on aarch64. */
static int cas16(u64*p, u64*exp, u64 n0, u64 n1) {
#if defined(__aarch64__)
    u64 o0, o1; u32 st; int ok;
    __asm__ __volatile__(
        "1: ldaxp %[o0], %[o1], [%[p]]\n"
        "   cmp %[o0], %[e0]\n"
        "   ccmp %[o1], %[e1], #0, eq\n"
        "   b.ne 2f\n"
        "   stlxp %w[st], %[n0], %[n1], [%[p]]\n"
        "   cbnz %w[st], 1b\n"
        "   mov %w[ok], #1\n"
        "   dmb ish\n"
        "   b 3f\n"
        "2: clrex\n"
        "   mov %w[ok], #0\n"
        "3:\n"
        : [o0] "=&r"(o0), [o1] "=&r"(o1), [st] "=&r"(st), [ok] "=&r"(ok)
        : [p] "r"(p), [e0] "r"(exp[0]), [e1] "r"(exp[1]), [n0] "r"(n0), [n1] "r"(n1)
        : "cc", "memory");
    exp[0] = o0; exp[1] = o1;
    return ok;
#elif defined(__x86_64__)
    unsigned char ok; u64 a = exp[0], d = exp[1];
    __asm__ __volatile__("lock cmpxchg16b %[m]\n\tsetz %[ok]"
        : [m] "+m"(*(unsigned __int128*) p), "+a"(a), "+d"(d), [ok] "=q"(ok) : "b"(n0), "c"(n1) : "cc", "memory");
    exp[0] = a; exp[1] = d;
    return ok;
#else
#error "cas16: unsupported host"
#endif
}

static void cpuid(CPU*c) {
    u32 leaf = (u32) c->r[RAX], sub = (u32) c->r[RCX], a = 0, b = 0, cc = 0, d = 0;
    if (leaf == 0) { a = 4; b = 0x756e6547; d = 0x49656e69; cc = 0x6c65746e; }          /* "GenuineIntel": glibc only reads leaf 1 for known vendors */
    else if (leaf == 1) { a = 0x000106a5; b = 0x00010800;      /* family 6 model 0x1a (Nehalem): the one Intel core with exactly SSE4.2 and no AVX; glibc then picks its SSE2 unaligned memmove */
           cc = (1 << 0) | (1 << 9) | (1 << 13) | (1 << 19) | (1 << 20) | (1 << 23);     /* SSE3 SSSE3 CX16 SSE4.1 SSE4.2 POPCNT = x86-64-v2; no XSAVE/AVX */
        d = (1 << 0) | (1 << 4) | (1 << 8) | (1 << 15) | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26); }
    else if (leaf == 2) { a = 1; }                                                      /* one round, no cache descriptors */
    else if (leaf == 4) {                                                               /* deterministic cache parameters */
        static const u32 e[4][4] = {
            {(1 << 5) | 1, (7u << 22) | 63, 63, 0},          /* L1d 32K 8-way */
            {(1 << 5) | 2, (7u << 22) | 63, 63, 0},          /* L1i 32K 8-way */
            {(2 << 5) | 3, (7u << 22) | 63, 511, 0},         /* L2 256K 8-way */
            {(3 << 5) | 3 | (15u << 14), (15u << 22) | 63, 8191, 0}, /* L3 8M 16-way */
        };
        if (sub < 4) { a = e[sub][0]; b = e[sub][1]; cc = e[sub][2]; d = e[sub][3]; }
    }
    else if (leaf == 0x80000000u) { a = 0x80000008u; }
    else if (leaf == 0x80000001u) { cc = 1; d = (1 << 11) | (1 << 20) | (1 << 29); }
    else if (leaf >= 0x80000002u && leaf <= 0x80000004u) {                              /* processor brand string, 16 bytes per leaf */
        char br[48] = PEMU_BRAND; u32 w[4]; memcpy(w, br + 16 * (leaf - 0x80000002u), 16);
        a = w[0]; b = w[1]; cc = w[2]; d = w[3]; }
    else if (leaf == 0x80000006u) { cc = 0x01006040u; }                                 /* L2: 256 KB, 8-way, 64-byte lines (as leaf 4) */
    else if (leaf == 0x80000008u) { a = 0x00003028u; }                                  /* 40 bits physical, 48 virtual (as /proc/cpuinfo) */
    c->r[RAX] = a; c->r[RBX] = b; c->r[RCX] = cc; c->r[RDX] = d;
}

/* in/out/cli/sti/hlt are privileged: a real CPU raises #GP and Linux delivers SIGSEGV
 * (si_code SI_KERNEL, rip = the instruction). Programs such as lscpu probe the VMware
 * backdoor port this way and catch the signal. */
static void gp_fault(CPU*c) {
    g_os->exception(c, EXCP_GP);
}
static void step(CPU*c) {
    I in; memset(&in, 0, sizeof in);
    u8*start = (u8*) c->rip, *ip = start;
    if (__builtin_expect(g_trace, 0)) g_ring[g_ringi++ & 63] = c->rip;
    M m;
    for (;;) {
        u8 b = *ip;
        if (b == 0x66) in.p66 = 1; else if (b == 0xF3) in.rep = 1; else if (b == 0xF2) in.repne = 1;
        else if (b == 0x64) in.seg = c->fs; else if (b == 0x65) in.seg = c->gs; else if (b == 0x67) in.addr32 = 1;
        else if (b == 0xF0) in.lock = 1;
        else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36) { }
        else break;
        ip++;
    }
    if ((*ip & 0xF0) == 0x40) in.rex = *ip++;
    in.w = (in.rex >> 3) & 1; in.ip = ip;
    int opsz = in.w ? 8 : in.p66 ? 2 : 4;
    int rexB = (in.rex & 1) << 3;
    u8 op = *in.ip++;

    if (op < 0x40 && (op & 7) < 6) {
        int t = op >> 3, f = op & 7;
        if (f < 4) {
            int sz = (f & 1) ? opsz : 1;
            modrm(c, &in, &m, 0);
            if (f < 2 && t != 7 && ATOMIC_OK(&in, &m, sz)) { u64 s_ = getr(c, m.reg, sz, in.rex), rf0 = RF(c); ARMW(m.ea, sz, o_, (RF(c) = rf0, alu(c, t, o_, s_, sz))); }
            else if (f < 2) { u64 r = alu(c, t, rdm(c, &in, &m, sz), getr(c, m.reg, sz, in.rex), sz); if (t != 7) wrm(c, &in, &m, sz, r); }
            else { u64 r = alu(c, t, getr(c, m.reg, sz, in.rex), rdm(c, &in, &m, sz), sz); if (t != 7) setr(c, m.reg, sz, in.rex, r); }
        } else {
            int sz = f == 4 ? 1 : opsz;
            u64 imm = sz == 1 ? (u64)(s64)(s8) *in.ip++ : immz(&in, opsz);
            u64 r = alu(c, t, getr(c, RAX, sz, 0), imm, sz);
            if (t != 7) setr(c, RAX, sz, 0, r);
        }
        c->rip = (u64) in.ip; return;
    }
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        { u64 v = c->r[(op & 7) | rexB]; push(c, v); break; }
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        { u64 v = pop(c); c->r[(op & 7) | rexB] = v; break; }
    case 0x63: modrm(c, &in, &m, 0); { u64 v = rdm(c, &in, &m, 4); setr(c, m.reg, opsz, in.rex, opsz == 8 ? (u64) sx(v, 4) : v); } break;
    case 0x68: push(c, (u64)(s64) imm32(&in)); break;
    case 0x6A: push(c, (u64)(s64)(s8) *in.ip++); break;
    case 0x69: case 0x6B: {
        modrm(c, &in, &m, op == 0x69 ? (opsz == 2 ? 2 : 4) : 1);
        s64 a = sx(rdm(c, &in, &m, opsz), opsz), b = op == 0x69 ? (s64) immz(&in, opsz) : (s64)(s8) *in.ip++;
        __int128 p = (__int128) a * b; u64 r = (u64) p & mskz(opsz);
        int ov = p != (__int128) sx(r, opsz);
        RF(c) &= ~(u64)(CF_ | OF_); if (ov) RF(c) |= CF_ | OF_;
        setr(c, m.reg, opsz, in.rex, r); break; }
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        s8 rel = (s8) *in.ip++;
        c->rip = (u64) in.ip + (u64)(s64) (cond(c, op & 15) ? rel : 0); return; }
    case 0x80: case 0x81: case 0x83: {
        int sz = op == 0x80 ? 1 : opsz;
        modrm(c, &in, &m, op == 0x81 ? (opsz == 2 ? 2 : 4) : 1);
        u64 imm = op == 0x81 ? immz(&in, opsz) : (u64)(s64)(s8) *in.ip++;
        if ((m.reg & 7) != 7 && ATOMIC_OK(&in, &m, sz)) { int t_ = m.reg & 7; u64 rf0 = RF(c); ARMW(m.ea, sz, o_, (RF(c) = rf0, alu(c, t_, o_, imm, sz))); break; }
        u64 r = alu(c, m.reg & 7, rdm(c, &in, &m, sz), imm, sz);
        if ((m.reg & 7) != 7) wrm(c, &in, &m, sz, r);
        break; }
    case 0x84: case 0x85: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); f_logic(c, rdm(c, &in, &m, sz) & getr(c, m.reg, sz, in.rex), sz); break; }
    case 0x86: case 0x87: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0);
        if (m.ismem && !(m.ea & ((u64) sz - 1))) { u64 b = getr(c, m.reg, sz, in.rex), a = 0; ARMW(m.ea, sz, o_, (a = o_, b)); setr(c, m.reg, sz, in.rex, a); break; }
        u64 a = rdm(c, &in, &m, sz), b = getr(c, m.reg, sz, in.rex); wrm(c, &in, &m, sz, b); setr(c, m.reg, sz, in.rex, a); break; }
    case 0x88: case 0x89: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); wrm(c, &in, &m, sz, getr(c, m.reg, sz, in.rex)); break; }
    case 0x8A: case 0x8B: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); setr(c, m.reg, sz, in.rex, rdm(c, &in, &m, sz)); break; }
    case 0x8D: { in.seg = 0; modrm(c, &in, &m, 0); if (!m.ismem) goto bad; setr(c, m.reg, opsz, in.rex, m.ea); break; }
    case 0x8C: {                                    /* mov r/m16, Sreg: the selectors a Linux x86_64 user process sees (Wine reads them at startup) */
        static const u16 sel[6] = { 0 /*es*/, 0x33 /*cs*/, 0x2b /*ss*/, 0 /*ds*/, 0 /*fs*/, 0 /*gs*/ };
        modrm(c, &in, &m, 0);
        int sr = m.reg & 7; if (sr > 5) goto bad;
        wrm(c, &in, &m, m.ismem ? 2 : opsz, sel[sr]);   /* a register destination is zero-extended to the operand size */
        break; }
    case 0x8E: {                                    /* mov Sreg, r/m16: flat model, so loading es/ss/ds/fs/gs is accepted and ignored (fs/gs bases come from arch_prctl) */
        modrm(c, &in, &m, 0);
        int sr = m.reg & 7; if (sr == 1 || sr > 5) goto bad;      /* cs cannot be loaded */
        (void) rdm(c, &in, &m, 2);
        break; }
    case 0x8F: { u64 sp0 = c->r[RSP]; c->r[RSP] = sp0 + 8; modrm(c, &in, &m, 0); c->r[RSP] = sp0; u64 v = pop(c); wrm(c, &in, &m, 8, v); break; }
    case 0x90:
        if (rexB) { u64 t = c->r[8]; c->r[8] = c->r[RAX]; c->r[RAX] = t; }
        break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        int r = (op & 7) | rexB; u64 a = getr(c, RAX, opsz, 0), b = getr(c, r, opsz, 0);
        setr(c, RAX, opsz, 0, b); setr(c, r, opsz, 0, a); break; }
    case 0x98: if (opsz == 8) c->r[RAX] = (u64)(s64)(s32) c->r[RAX]; else if (opsz == 4) c->r[RAX] = (u32)(s32)(s16) c->r[RAX]; else setr(c, RAX, 2, 0, (u64)(s64)(s8) c->r[RAX]); break;
    case 0x99: if (opsz == 8) c->r[RDX] = (s64) c->r[RAX] < 0 ? ~0ULL : 0; else if (opsz == 4) c->r[RDX] = (s32) c->r[RAX] < 0 ? 0xffffffffULL : 0; else setr(c, RDX, 2, 0, (s16) c->r[RAX] < 0 ? 0xffff : 0); break;
    case 0x9B: break;                                         /* fwait */
    case 0x9C: push(c, RF(c) | 2); break;
    case 0x9D: { u64 v = pop(c); RF(c) = (v & (CF_ | PF_ | AF_ | ZF_ | SF_ | DF_ | OF_)) | 0x202; break; }
    case 0x9E: { u64 ah = (c->r[RAX] >> 8) & 0xff; RF(c) = (RF(c) & ~(u64)(CF_ | PF_ | AF_ | ZF_ | SF_)) | (ah & (CF_ | PF_ | AF_ | ZF_ | SF_)); break; }
    case 0x9F: setr(c, 4, 1, 0, (RF(c) & (CF_ | PF_ | AF_ | ZF_ | SF_)) | 2); break;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {                /* mov al/ax/eax/rax <-> [moffs]: the address is an immediate (4 bytes with a 0x67 prefix) */
        int sz = (op & 1) ? opsz : 1; u64 off;
        if (in.addr32) { off = *(u32u*) in.ip; in.ip += 4; } else { off = *(u64u*) in.ip; in.ip += 8; }
        if (op < 0xA2) setr(c, RAX, sz, 0, mrd(off + in.seg, sz)); else mwr(off + in.seg, getr(c, RAX, sz, 0), sz);
        break; }
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        int sz = (op & 1) ? opsz : 1; s64 dl = (RF(c) & DF_) ? -sz : sz;
        int isrep = in.rep || in.repne, kind = op >= 0xAE ? 4 : op >= 0xAC ? 3 : op >= 0xAA ? 2 : op >= 0xA6 ? 1 : 0;
        u64 cnt = isrep ? c->r[RCX] : 1;
        if (kind == 0 && isrep && !(RF(c) & DF_) && cnt) {
            u64 n = cnt * (u64) sz, dst = c->r[RDI], src = c->r[RSI];
            if (dst > src && dst < src + n) { for (u64 i = 0; i < n; i++) *(u8*)(dst + i) = *(u8*)(src + i); } else memmove((void*) dst, (void*) src, n);
            c->r[RDI] += n; c->r[RSI] += n; c->r[RCX] = 0; break;
        }
        if (kind == 2 && isrep && !(RF(c) & DF_) && cnt) {          /* rep stos: bulk fill (a fault restarts the whole instruction, which is harmless for a fill) */
            u64 v = c->r[RAX] & mskz(sz), d = c->r[RDI];
            if (sz == 1 || !v || v == (v & 0xff) * (mskz(sz) / 0xff)) memset((void*) d, (int)(v & 0xff), cnt * (u64) sz);
            else if (sz == 2) { for (u64 i = 0; i < cnt; i++) ((u16u*) d)[i] = (u16) v; }
            else if (sz == 4) { for (u64 i = 0; i < cnt; i++) ((u32u*) d)[i] = (u32) v; }
            else { for (u64 i = 0; i < cnt; i++) ((u64u*) d)[i] = v; }
            if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_RELEASE);
            c->r[RDI] += cnt * (u64) sz; c->r[RCX] = 0; break;
        }
        if (kind == 4 && in.repne && !in.rep && !(RF(c) & DF_) && sz == 1 && cnt) {      /* repne scasb (strlen/memchr idiom) */
            u8 al = (u8) c->r[RAX]; const u8 *p0 = (const u8*) c->r[RDI];
            const u8 *q = memchr(p0, al, cnt);
            u64 used = q ? (u64)(q - p0) + 1 : cnt;
            f_sub(c, al, p0[used - 1], 0, 1);                    /* flags of the last comparison, as the loop would leave them */
            c->r[RDI] += used; c->r[RCX] -= used; break;
        }
        while (cnt) {
            switch (kind) {
            case 0: mwr(c->r[RDI], mrd(c->r[RSI], sz), sz); c->r[RSI] += (u64) dl; c->r[RDI] += (u64) dl; break;
            case 1: f_sub(c, mrd(c->r[RSI], sz), mrd(c->r[RDI], sz), 0, sz); c->r[RSI] += (u64) dl; c->r[RDI] += (u64) dl; break;
            case 2: mwr(c->r[RDI], c->r[RAX], sz); c->r[RDI] += (u64) dl; break;
            case 3: setr(c, RAX, sz, 0, mrd(c->r[RSI], sz)); c->r[RSI] += (u64) dl; break;
            default: f_sub(c, getr(c, RAX, sz, 0), mrd(c->r[RDI], sz), 0, sz); c->r[RDI] += (u64) dl;
            }
            cnt--; if (isrep) c->r[RCX]--;
            if (isrep && (kind == 1 || kind == 4)) { int zf = !!(RF(c) & ZF_); if ((in.rep && !zf) || (in.repne && zf)) break; }
        }
        break; }
    case 0xA8: { u64 imm = (u64)(u8) *in.ip++; f_logic(c, c->r[RAX] & imm, 1); break; }
    case 0xA9: { u64 imm = immz(&in, opsz); f_logic(c, getr(c, RAX, opsz, 0) & imm, opsz); break; }
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        setr(c, (op & 7) | rexB, 1, in.rex, *in.ip++); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        int r = (op & 7) | rexB; u64 v;
        if (in.w) { v = *(u64u*) in.ip; in.ip += 8; } else if (in.p66) { v = *(u16u*) in.ip; in.ip += 2; } else { v = *(u32u*) in.ip; in.ip += 4; }
        setr(c, r, opsz, 0, v); break; }
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int sz = (op & 1) ? opsz : 1;
        modrm(c, &in, &m, (op <= 0xC1) ? 1 : 0);
        unsigned n = op <= 0xC1 ? *in.ip++ : op <= 0xD1 ? 1 : (unsigned)(c->r[RCX] & 0xff);
        u64 r = shiftop(c, m.reg & 7, rdm(c, &in, &m, sz), n, sz);
        wrm(c, &in, &m, sz, r); break; }
    case 0xC2: { u16 n = *(u16u*) in.ip; u64 ra = pop(c); c->r[RSP] += n; c->rip = ra; return; }
    case 0xC3: c->rip = pop(c); return;
    case 0xC6: case 0xC7: {
        int sz = op & 1 ? opsz : 1;
        modrm(c, &in, &m, sz == 1 ? 1 : (opsz == 2 ? 2 : 4));
        if ((m.reg & 7) != 0) goto bad;
        u64 imm = sz == 1 ? *in.ip++ : immz(&in, opsz);
        wrm(c, &in, &m, sz, imm); break; }
    case 0xC9: c->r[RSP] = c->r[RBP]; c->r[RBP] = pop(c); break;
    case 0xCC: c->rip = (u64) in.ip; g_os->exception(c, EXCP_BP); return;
    case 0xCF: {                                     /* iretq (REX.W): Wine's syscall dispatcher returns to user code this way. Flat user-mode model: cs/ss are popped and ignored */
        if (opsz != 8) goto bad;
        u64 nip = pop(c); (void) pop(c); u64 fl = pop(c); u64 nsp = pop(c); (void) pop(c);
        RF(c) = (fl & (CF_ | PF_ | AF_ | ZF_ | SF_ | DF_ | OF_)) | 0x202;
        c->r[RSP] = nsp; c->rip = nip; return; }
    case 0xE8: { s32 rel = imm32(&in); push(c, (u64) in.ip); c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xE9: { s32 rel = imm32(&in); c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xEB: { s8 rel = (s8) *in.ip++; c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xE3: { s8 rel = (s8) *in.ip++; c->rip = (u64) in.ip + (u64)(s64)(c->r[RCX] == 0 ? rel : 0); return; }
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0xFA: case 0xFB: case 0xF4: gp_fault(c); return;
    case 0xF5: RF(c) ^= CF_; break;
    case 0xF8: RF(c) &= ~(u64)CF_; break;
    case 0xF9: RF(c) |= CF_; break;
    case 0xFC: RF(c) &= ~(u64)DF_; break;
    case 0xFD: RF(c) |= DF_; break;
    case 0xF6: case 0xF7: {
        int sz = op & 1 ? opsz : 1, ext = (*in.ip >> 3) & 7;
        modrm(c, &in, &m, ext < 2 ? (sz == 1 ? 1 : (opsz == 2 ? 2 : 4)) : 0);
        u64 v = rdm(c, &in, &m, sz), mk = mskz(sz);
        switch (ext) {
        case 0: case 1: { u64 imm = sz == 1 ? *in.ip++ : immz(&in, opsz); f_logic(c, v & imm, sz); break; }
        case 2: if (ATOMIC_OK(&in, &m, sz)) { ARMW(m.ea, sz, o_, ~o_); break; } wrm(c, &in, &m, sz, ~v & mk); break;
        case 3: if (ATOMIC_OK(&in, &m, sz)) { u64 ov = 0; ARMW(m.ea, sz, o_, (ov = o_, f_sub(c, 0, o_, 0, sz))); RF(c) &= ~(u64)CF_; if (ov & mk) RF(c) |= CF_; break; }
                { u64 r = f_sub(c, 0, v, 0, sz); RF(c) &= ~(u64)CF_; if (v & mk) RF(c) |= CF_; wrm(c, &in, &m, sz, r); break; }
        case 4: case 5: {
            unsigned __int128 p; int ov;
            if (ext == 4) { p = (unsigned __int128)(getr(c, RAX, sz, 0)) * v; ov = (p >> (sz * 8)) != 0; }
            else { __int128 sp = (__int128) sx(getr(c, RAX, sz, 0), sz) * sx(v, sz); p = (unsigned __int128) sp; ov = sp != (__int128) sx((u64) sp & mk, sz); }
            if (sz == 1) setr(c, RAX, 2, 0, (u64) p);
            else { setr(c, RAX, sz, 0, (u64) p); setr(c, RDX, sz, 0, (u64)(p >> (sz * 8))); }
            RF(c) &= ~(u64)(CF_ | OF_); if (ov) RF(c) |= CF_ | OF_;
            break; }
        default: c->rip = (u64) in.ip; do_div(c, sz, v, ext == 7, start);
        }
        break; }
    case 0xFE: case 0xFF: {
        int sz = op & 1 ? opsz : 1, ext = (*in.ip >> 3) & 7;
        modrm(c, &in, &m, 0);
        if (ext < 2 && ATOMIC_OK(&in, &m, sz)) {
            u64 keep = RF(c) & CF_;
            ARMW(m.ea, sz, o_, (ext == 0 ? f_add(c, o_, 1, 0, sz) : f_sub(c, o_, 1, 0, sz)));
            RF(c) = (RF(c) & ~(u64)CF_) | keep;
        } else if (ext < 2) {
            u64 v = rdm(c, &in, &m, sz), keep = RF(c) & CF_, r = ext == 0 ? f_add(c, v, 1, 0, sz) : f_sub(c, v, 1, 0, sz);
            RF(c) = (RF(c) & ~(u64)CF_) | keep; wrm(c, &in, &m, sz, r);
        } else if (op == 0xFF && (ext == 2 || ext == 4)) {
            u64 t = rdm(c, &in, &m, 8);
            if (ext == 2) push(c, (u64) in.ip);
            c->rip = t; return;
        } else if (op == 0xFF && ext == 6) { u64 v = rdm(c, &in, &m, 8); push(c, v); }
        else goto bad;
        break; }
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        if (x87(c, &in, op) < 0) undef(c, start, "x87");
        break;
    case 0x0F: {
        u8 o2 = *in.ip++;
        switch (o2) {
        case 0x05: { c->rip = (u64) in.ip; c->r[RCX] = c->rip; c->r[11] = RF(c); g_os->syscall(c); return; }
        case 0x0B: goto bad;
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            modrm(c, &in, &m, 0); break;
        case 0x31: { u64 t = tsc_now(); c->r[RAX] = (u32) t; c->r[RDX] = t >> 32; break; }
        case 0x01: {                                          /* the register forms xgetbv (0F 01 D0) and rdtscp (0F 01 F9); the rest is privileged */
            u8 b = *in.ip++;
            if (b == 0xD0) { c->r[RAX] = 3; c->r[RDX] = 0; }                                   /* XCR0 = x87 | SSE (no AVX state is emulated) */
            else if (b == 0xF9) { u64 t = tsc_now(); c->r[RAX] = (u32) t; c->r[RDX] = t >> 32; c->r[RCX] = 0; }
            else goto bad;
            break; }
        case 0xA0: case 0xA8: push(c, 0); break;              /* push fs / gs: the selector reads as 0 */
        case 0xA1: case 0xA9: (void) pop(c); break;           /* pop fs / gs: ignored (the base is set with arch_prctl) */
        case 0xC3: {                                          /* movnti */
            modrm(c, &in, &m, 0);
            if (!m.ismem) goto bad;
            mwr(m.ea, getr(c, m.reg, opsz, in.rex), opsz); break; }
        case 0xC7: {                                          /* cmpxchg8b / cmpxchg16b (REX.W) */
            modrm(c, &in, &m, 0);
            if (!m.ismem || (m.reg & 7) != 1) goto bad;       /* rdrand/rdseed are not advertised */
            int ok;
            if (in.w) {
                u64 e[2] = { c->r[RAX], c->r[RDX] };
                if (!(m.ea & 15)) ok = cas16((u64*) m.ea, e, c->r[RBX], c->r[RCX]);
                else {                                        /* real hardware raises #GP; do it non-atomically instead */
                    u64 p0 = *(u64u*) m.ea, p1 = *(u64u*)(m.ea + 8);
                    ok = p0 == e[0] && p1 == e[1];
                    if (ok) { *(u64u*) m.ea = c->r[RBX]; *(u64u*)(m.ea + 8) = c->r[RCX]; } else { e[0] = p0; e[1] = p1; }
                }
                if (!ok) { c->r[RAX] = e[0]; c->r[RDX] = e[1]; }
            } else {
                u64 old = ((u64)(u32) c->r[RDX] << 32) | (u32) c->r[RAX], nv = ((u64)(u32) c->r[RCX] << 32) | (u32) c->r[RBX];
                if (!(m.ea & 7)) ok = __atomic_compare_exchange_n((u64*) m.ea, &old, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                else { u64 cur = *(u64u*) m.ea; ok = cur == old; if (ok) *(u64u*) m.ea = nv; else old = cur; }
                if (!ok) { c->r[RAX] = (u32) old; c->r[RDX] = old >> 32; }
            }
            if (ok) RF(c) |= ZF_; else RF(c) &= ~(u64) ZF_;
            break; }
        case 0x38: case 0x3A: {                               /* SSSE3 / SSE4.1 / SSE4.2 / crc32 */
            u8 o3 = *in.ip++;
            if (sse38(c, &in, o3, o2 == 0x3A) < 0) undef(c, start, o2 == 0x3A ? "0F3A" : "0F38");
            break; }
        case 0xA2: cpuid(c); break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz);
            if (!cond(c, o2 & 15)) v = getr(c, m.reg, opsz, 0);
            setr(c, m.reg, opsz, 0, v); break; }
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
            s32 rel = imm32(&in); c->rip = (u64) in.ip + (u64)(s64)(cond(c, o2 & 15) ? rel : 0); return; }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            modrm(c, &in, &m, 0); wrm(c, &in, &m, 1, cond(c, o2 & 15)); break;
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {
            int imm = o2 == 0xBA, kind;
            modrm(c, &in, &m, imm ? 1 : 0);
            kind = imm ? (m.reg & 7) - 4 : o2 == 0xA3 ? 0 : o2 == 0xAB ? 1 : o2 == 0xB3 ? 2 : 3;
            if (kind < 0 || kind > 3) goto bad;
            int bits = opsz * 8, bit; u64 ea = m.ea, val;
            if (imm) { bit = *in.ip++ & (bits - 1); val = rdm(c, &in, &m, opsz); }
            else if (m.ismem) {
                s64 off = sx(getr(c, m.reg, opsz, in.rex), opsz);
                s64 q = off >= 0 ? off / bits : -((-off + bits - 1) / bits);
                ea = m.ea + (u64)(q * opsz); bit = (int)(off - q * bits); val = mrd(ea, opsz);
            } else { bit = (int)(getr(c, m.reg, opsz, in.rex) & (u64)(bits - 1)); val = rdm(c, &in, &m, opsz); }
            RF(c) &= ~(u64)CF_; if ((val >> bit) & 1) RF(c) |= CF_;
            if (kind && in.lock && m.ismem && !(ea & ((u64) opsz - 1))) {
                u64 bm = 1ULL << bit, ov = 0;
                ARMW(ea, opsz, o_, (ov = o_, kind == 1 ? (o_ | bm) : kind == 2 ? (o_ & ~bm) : (o_ ^ bm)));
                RF(c) &= ~(u64)CF_; if ((ov >> bit) & 1) RF(c) |= CF_;
            } else if (kind) {
                val = kind == 1 ? val | (1ULL << bit) : kind == 2 ? val & ~(1ULL << bit) : val ^ (1ULL << bit);
                if (m.ismem) mwr(ea, val, opsz); else wrm(c, &in, &m, opsz, val);
            }
            break; }
        case 0xA4: case 0xA5: case 0xAC: case 0xAD: {
            modrm(c, &in, &m, (o2 & 1) ? 0 : 1);
            unsigned n = (o2 & 1) ? (unsigned)(c->r[RCX] & 0xff) : *in.ip++;
            n &= opsz == 8 ? 63 : 31;
            u64 dv = rdm(c, &in, &m, opsz), sr = getr(c, m.reg, opsz, in.rex);
            if (n && opsz >= 4) {
                int bits = opsz * 8; u64 r, mk = mskz(opsz); int cf;
                if (o2 < 0xA8) { r = ((dv << n) | (sr >> (bits - n))) & mk; cf = (int)((dv >> (bits - n)) & 1); }
                else { r = ((dv >> n) | (sr << (bits - n))) & mk; cf = (int)((dv >> (n - 1)) & 1); }
                RF(c) &= ~(u64)(CF_ | OF_); if (cf) RF(c) |= CF_;
                if (sgn(r, opsz) != sgn(dv, opsz)) RF(c) |= OF_;
                szp(c, r, opsz); wrm(c, &in, &m, opsz, r);
            }
            break; }
        case 0xAE: {
            modrm(c, &in, &m, 0);
            int ext = m.reg & 7;
            if (!m.ismem) { if (ext >= 5) { __atomic_thread_fence(__ATOMIC_SEQ_CST); break; } goto bad; }   /* lfence/mfence/sfence */
            if (ext == 0) fx_save(c, (u8*) m.ea);
            else if (ext == 1) fx_restore(c, (const u8*) m.ea);
            else if (ext == 2) c->mxcsr = *(u32u*) m.ea;
            else if (ext == 3) *(u32u*) m.ea = c->mxcsr;
            else if (ext == 7) { }
            else goto bad;
            break; }
        case 0xAF: {
            modrm(c, &in, &m, 0);
            s64 a = sx(getr(c, m.reg, opsz, in.rex), opsz), b = sx(rdm(c, &in, &m, opsz), opsz);
            __int128 p = (__int128) a * b; u64 r = (u64) p & mskz(opsz);
            RF(c) &= ~(u64)(CF_ | OF_); if (p != (__int128) sx(r, opsz)) RF(c) |= CF_ | OF_;
            setr(c, m.reg, opsz, in.rex, r); break; }
        case 0xB0: case 0xB1: {
            int sz = o2 & 1 ? opsz : 1; modrm(c, &in, &m, 0);
            if (ATOMIC_OK(&in, &m, sz)) {
                u64 acc = getr(c, RAX, sz, 0), nv = getr(c, m.reg, sz, in.rex), seen = 0, mk = mskz(sz);
                ARMW(m.ea, sz, o_, (seen = o_, o_ == (acc & mk) ? nv : o_));     /* a failed compare rewrites the old value, like the hardware */
                f_sub(c, acc, seen, 0, sz);
                if ((acc & mk) != (seen & mk)) setr(c, RAX, sz, 0, seen);
                break;
            }
            u64 dv = rdm(c, &in, &m, sz), acc = getr(c, RAX, sz, 0);
            f_sub(c, acc, dv, 0, sz);
            if ((acc & mskz(sz)) == (dv & mskz(sz))) wrm(c, &in, &m, sz, getr(c, m.reg, sz, in.rex)); else setr(c, RAX, sz, 0, dv);
            break; }
        case 0xC0: case 0xC1: {
            int sz = o2 & 1 ? opsz : 1; modrm(c, &in, &m, 0);
            if (ATOMIC_OK(&in, &m, sz)) { u64 sr = getr(c, m.reg, sz, in.rex), ov = 0; ARMW(m.ea, sz, o_, (ov = o_, f_add(c, o_, sr, 0, sz))); setr(c, m.reg, sz, in.rex, ov); break; }
            u64 dv = rdm(c, &in, &m, sz), sr = getr(c, m.reg, sz, in.rex), r = f_add(c, dv, sr, 0, sz);
            setr(c, m.reg, sz, in.rex, dv); wrm(c, &in, &m, sz, r); break; }
        case 0xB6: case 0xB7: case 0xBE: case 0xBF: {
            int ssz = (o2 & 1) ? 2 : 1; modrm(c, &in, &m, 0);
            u64 v = rdm(c, &in, &m, ssz);
            if (o2 >= 0xBE) v = (u64) sx(v, ssz);
            setr(c, m.reg, opsz, in.rex, v); break; }
        case 0xB8: if (!in.rep) goto bad; { modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz); u64 r = (u64) __builtin_popcountll(v & mskz(opsz));
            RF(c) &= ~(u64)(CF_ | OF_ | AF_ | SF_ | PF_ | ZF_); if (!v) RF(c) |= ZF_; setr(c, m.reg, opsz, in.rex, r); } break;
        case 0xBC: case 0xBD: {
            modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz) & mskz(opsz); int bits = opsz * 8;
            if (in.rep) {                                   /* tzcnt / lzcnt */
                u64 r = !v ? (u64) bits : o2 == 0xBC ? (u64) __builtin_ctzll(v) : (u64)(__builtin_clzll(v) - (64 - bits));
                RF(c) &= ~(u64)(CF_ | ZF_); if (!v) RF(c) |= CF_; if (!r) RF(c) |= ZF_;
                setr(c, m.reg, opsz, in.rex, r);
            } else if (!v) RF(c) |= ZF_;
            else { RF(c) &= ~(u64)ZF_; setr(c, m.reg, opsz, in.rex, o2 == 0xBC ? (u64) __builtin_ctzll(v) : (u64)(63 - __builtin_clzll(v))); }
            break; }
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
            int r = (o2 & 7) | rexB;
            if (in.w) c->r[r] = __builtin_bswap64(c->r[r]); else c->r[r] = __builtin_bswap32((u32) c->r[r]);
            break; }
        default:
            if (sse(c, &in, o2) < 0) { undef(c, start, "0F/SSE"); }
        }
        break; }
    default:
    bad:
        undef(c, start, "opcode");
    }
    c->rip = (u64) in.ip;
}

