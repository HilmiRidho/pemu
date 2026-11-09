/* target/aarch64/step.inc.h - interpreter step. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ main step */
static void undef(CPU*c, u32 i) {
    fprintf(stderr, "pemu: unsupported instruction %08x at pc=%#llx\n", i, (unsigned long long) c->pc);
    g_os->exception(c, EXCP_UDEF);                        /* the OS layer ends the process */
    __builtin_trap();
}
static u64 rev_n(u64 v, int bytes) { u64 r = 0; for (int k = 0; k < bytes; k++) { r = (r << 8) | (v & 0xff); v >>= 8; } return r; }

#ifdef PEMU_EMBED
static void embed_hostcall(CPU *c);
#endif
static void step(CPU*c) {
    u64 pc = c->pc, npc = pc + 4;
    u32 i = *(u32*) pc;
    int sf = i >> 31, rd = i & 31, rn = (i >> 5) & 31, rm = (i >> 16) & 31;
    switch ((i >> 25) & 15) {
    case 8: case 9: {                                          /* data processing: immediate */
        if ((i & 0x1F000000) == 0x10000000) {                  /* ADR / ADRP */
            s64 imm = sext((((i >> 5) & 0x7ffff) << 2) | ((i >> 29) & 3), 21);
            SET(rd, (i >> 31) ? ((pc & ~0xfffULL) + (u64)(imm << 12)) : pc + (u64) imm);
        } else if ((i & 0x1F000000) == 0x11000000) {           /* ADD/SUB imm */
            u64 imm = (i >> 10) & 0xfff; if ((i >> 22) & 1) imm <<= 12;
            int sub = (i >> 30) & 1, S = (i >> 29) & 1;
            u64 r = addsub(c, sf, XS(rn), imm, sub, S);
            if (S) SET(rd, r); else SETS(rd, r);
        } else if ((i & 0x1F800000) == 0x12000000) {           /* logical imm */
            u64 wm, tm;
            if (decode_bm((i >> 22) & 1, (i >> 10) & 0x3f, (i >> 16) & 0x3f, sf, &wm, &tm) < 0) undef(c, i);
            int opc = (i >> 29) & 3; u64 a = X(rn), r;
            r = opc == 1 ? a | wm : opc == 2 ? a ^ wm : a & wm;
            r &= msk(sf);
            if (opc == 3) { c->nzcv = (u32)(((r >> (sf ? 63 : 31)) & 1) << 3 | (r == 0) << 2); SET(rd, r); } else SETS(rd, r);
        } else if ((i & 0x1F800000) == 0x12800000) {           /* move wide */
            int opc = (i >> 29) & 3, hw = (i >> 21) & 3; u64 imm = (u64)((i >> 5) & 0xffff) << (hw * 16);
            if (!sf && hw > 1) undef(c, i);
            if (opc == 0) SET(rd, ~imm & msk(sf));
            else if (opc == 2) SET(rd, imm);
            else if (opc == 3) SET(rd, ((X(rd) & ~(0xffffULL << (hw * 16))) | imm) & msk(sf));   /* Wd form zero-extends */
            else undef(c, i);
        } else if ((i & 0x1F800000) == 0x13000000) {           /* bitfield */
            u64 wm, tm; int opc = (i >> 29) & 3, immr = (i >> 16) & 0x3f, imms = (i >> 10) & 0x3f;
            if (opc == 3 || decode_bm((i >> 22) & 1, imms, immr, sf, &wm, &tm) < 0) undef(c, i);
            u64 src = X(rn) & msk(sf), dst = opc == 1 ? X(rd) & msk(sf) : 0;
            u64 bot = (dst & ~wm) | (ror_n(src, immr, sf ? 64 : 32) & wm);
            u64 top = opc == 0 ? ((src >> imms) & 1 ? msk(sf) : 0) : opc == 2 ? 0 : dst;
            SET(rd, ((top & ~tm) | (bot & tm)) & msk(sf));
        } else if ((i & 0x1F800000) == 0x13800000) {           /* EXTR */
            int imms = (i >> 10) & 0x3f;
            u64 hi = X(rn) & msk(sf), lo = X(rm) & msk(sf), r;
            int n = sf ? 64 : 32;
            r = imms ? ((lo >> imms) | (hi << (n - imms))) & msk(sf) : lo;
            SET(rd, r);
        } else undef(c, i);
        break;
    }
    case 10: case 11: {                                        /* branches, system */
        if ((i & 0x7C000000) == 0x14000000) {                  /* B / BL */
            if (i >> 31) c->x[30] = pc + 4;
            npc = pc + (u64)(sext(i & 0x3ffffff, 26) << 2);
        } else if ((i & 0x7E000000) == 0x34000000) {           /* CBZ / CBNZ */
            u64 v = X(rd) & msk(sf);
            if ((v == 0) == !((i >> 24) & 1)) npc = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        } else if ((i & 0x7E000000) == 0x36000000) {           /* TBZ / TBNZ */
            int bit = ((i >> 31) << 5) | ((i >> 19) & 31);
            if ((((X(rd) >> bit) & 1) == 0) == !((i >> 24) & 1)) npc = pc + (u64)(sext((i >> 5) & 0x3fff, 14) << 2);
        } else if ((i & 0xFF000010) == 0x54000000) {           /* B.cond */
            if (cond(c, i & 15)) npc = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        } else if ((i & 0xFE000000) == 0xD6000000) {           /* BR / BLR / RET */
            int opc = (i >> 21) & 15; u64 t = X(rn);
            if (opc > 2) undef(c, i);
            if (opc == 1) c->x[30] = pc + 4;
            npc = t;
        } else if ((i & 0xFFE0001F) == 0xD4000001) {           /* SVC */
#ifdef PEMU_EMBED
            if (((i >> 5) & 0xffff) == 0x7001) { c->pc = npc; embed_hostcall(c); return; }
#endif
            c->pc = npc; g_os->syscall(c); return;
        } else if ((i & 0xFF000000) == 0xD4000000) {           /* BRK etc. */
            c->pc = pc; g_os->exception(c, EXCP_BRK); return;
        } else if ((i & 0xFFFFFFE0) == 0xD50B7420) {           /* DC ZVA */
            { u64 za = X(rd) & ~63ULL; memset((void*) za, 0, 64); }
        } else if ((i & 0xFFF80000) == 0xD5080000) {           /* SYS (cache maintenance) */
        } else if ((i & 0xFFFFF000) == 0xD5033000 || (i & 0xFFFFF01F) == 0xD503201F || (i & 0xFFFFF000) == 0xD5032000) {
            /* barriers / hints (NOP, YIELD, PAC/BTI hints) */
        } else if ((i & 0xFFF00000) == 0xD5300000) {           /* MRS */
            u32 key = (i >> 5) & 0x7fff; u64 v = 0;
            switch (key) {
            case 0x5E82: v = c->tpidr; break;                  /* TPIDR_EL0 */
            case 0x5A10: v = (u64) c->nzcv << 28; break;       /* NZCV */
            case 0x5A20: v = c->fpcr; break;                   /* FPCR */
            case 0x5A21: v = c->fpsr; break;                   /* FPSR */
            case 0x5F02: { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); v = (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; break; }   /* CNTVCT_EL0 */
            case 0x5F00: v = 1000000000ULL; break;             /* CNTFRQ_EL0 */
            case 0x5807: v = 4; break;                         /* DCZID_EL0: 64-byte blocks */
            case 0x5801: v = 0x84448004ULL; break;             /* CTR_EL0 */
            default: undef(c, i);
            }
            SET(rd, v);
        } else if ((i & 0xFFF00000) == 0xD5100000) {           /* MSR */
            u32 key = (i >> 5) & 0x7fff; u64 v = X(rd);
            switch (key) {
            case 0x5E82: c->tpidr = v; break;
            case 0x5A10: c->nzcv = (u32)(v >> 28) & 15; break;
            case 0x5A20: c->fpcr = v; break;
            case 0x5A21: c->fpsr = v; break;
            default: undef(c, i);
            }
        } else if ((i & 0xFFF8F01F) == 0xD500401F) {           /* MSR (immediate) - ignore */
        } else undef(c, i);
        break;
    }
    case 4: case 6: case 12: case 14: {                        /* loads and stores */
        c->pc = pc;
        if (ldst(c, i) < 0) undef(c, i);
        break;
    }
    case 5: case 13: {                                         /* data processing: register */
        if ((i & 0x1F000000) == 0x0A000000) {                  /* logical shifted */
            int opc = (i >> 29) & 3, n = (i >> 21) & 1;
            u64 b = shiftv(X(rm), (i >> 22) & 3, (i >> 10) & 0x3f, sf); if (n) b = ~b;
            u64 a = X(rn) & msk(sf), r;
            if (!sf && ((i >> 10) & 0x3f) >= 32) undef(c, i);
            r = opc == 1 ? a | b : opc == 2 ? a ^ b : a & b;
            r &= msk(sf);
            if (opc == 3) c->nzcv = (u32)(((r >> (sf ? 63 : 31)) & 1) << 3 | (r == 0) << 2);
            SET(rd, r);
        } else if ((i & 0x1F200000) == 0x0B000000) {           /* add/sub shifted */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, ty = (i >> 22) & 3;
            if (ty == 3 || (!sf && ((i >> 10) & 0x3f) >= 32)) undef(c, i);
            u64 b = shiftv(X(rm), ty, (i >> 10) & 0x3f, sf);
            SET(rd, addsub(c, sf, X(rn), b, sub, S));
        } else if ((i & 0x1F200000) == 0x0B200000) {           /* add/sub extended */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, sh = (i >> 10) & 7;
            u64 b = extendv(X(rm), (i >> 13) & 7, sh);
            u64 r = addsub(c, sf, XS(rn), b, sub, S);
            if (S) SET(rd, r); else SETS(rd, r);
        } else if ((i & 0x1FE00000) == 0x1A000000) {           /* ADC / SBC */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, cin = (c->nzcv >> 1) & 1;
            u64 b = X(rm); if (sub) b = ~b;
            SET(rd, adc(c, sf, X(rn), b, cin, S));
        } else if ((i & 0x1FE00000) == 0x1A400000) {           /* CCMP / CCMN */
            int sub = (i >> 30) & 1;
            if (cond(c, (i >> 12) & 15)) {
                u64 b = ((i >> 11) & 1) ? (u64)((i >> 16) & 31) : X(rm);
                addsub(c, sf, X(rn), b, sub, 1);
            } else c->nzcv = i & 15;
        } else if ((i & 0x1FE00000) == 0x1A800000) {           /* CSEL family */
            int op = (i >> 30) & 1, o2 = (i >> 10) & 1;
            u64 r;
            if (cond(c, (i >> 12) & 15)) r = X(rn);
            else { u64 b = X(rm); r = op ? (o2 ? -b : ~b) : (o2 ? b + 1 : b); }
            SET(rd, r & msk(sf));
        } else if ((i & 0x5FE00000) == 0x1AC00000) {           /* 2-source */
            int op = (i >> 10) & 0x3f; u64 a = X(rn) & msk(sf), b = X(rm) & msk(sf), r;
            switch (op) {
            case 2: if (!b) r = 0; else r = a / b; break;
            case 3: if (!b) r = 0; else if (sf) r = (s64) b == -1 ? -a : (u64)((s64) a / (s64) b); else r = (u64)(s32) ((s32) b == -1 ? (s32)(0u - (u32) a) : (s32) a / (s32) b) & M32; break;
            case 8: r = shiftv(a, 0, (int) b, sf); break;
            case 9: r = shiftv(a, 1, (int) b, sf); break;
            case 10: r = shiftv(a, 2, (int) b, sf); break;
            case 11: r = shiftv(a, 3, (int) b, sf); break;
            default: undef(c, i); r = 0; }
            SET(rd, r & msk(sf));
        } else if ((i & 0x5FE00000) == 0x5AC00000) {          /* 1-source */
            int op = (i >> 10) & 0x3f; u64 a = X(rn) & msk(sf), r = 0; int n = sf ? 64 : 32;
            switch (op) {
            case 0: for (int k = 0; k < n; k++) if (a >> k & 1) r |= 1ULL << (n - 1 - k); break;
            case 1: for (int k = 0; k < n / 16; k++) r |= (rev_n((a >> (16 * k)) & 0xffff, 2)) << (16 * k); break;
            case 2: if (sf) { for (int k = 0; k < 2; k++) r |= rev_n((a >> (32 * k)) & M32, 4) << (32 * k); } else r = rev_n(a, 4); break;
            case 3: if (!sf) undef(c, i); r = rev_n(a, 8); break;
            case 4: r = a ? (u64)(sf ? __builtin_clzll(a) : __builtin_clz((u32) a)) : (u64) n; break;
            case 5: { u64 sg = (a >> (n - 1)) & 1; int k = 1; while (k < n && ((a >> (n - 1 - k)) & 1) == sg) k++; r = (u64)(k - 1); break; }
            default: undef(c, i);
            }
            SET(rd, r & msk(sf));
        } else if ((i & 0x1F000000) == 0x1B000000) {           /* 3-source */
            int o0 = (i >> 15) & 1, op31 = (i >> 21) & 7, ra = (i >> 10) & 31; u64 r;
            u64 a = X(ra), n = X(rn), m = X(rm);
            switch (op31) {
            case 0: { u64 p = (n & msk(sf)) * (m & msk(sf)); r = o0 ? a - p : a + p; break; }
            case 1: { u64 p = (u64)((s64)(s32) n * (s64)(s32) m); r = o0 ? a - p : a + p; sf = 1; break; }
            case 5: { u64 p = (n & M32) * (m & M32); r = o0 ? a - p : a + p; sf = 1; break; }
            case 2: r = (u64)(((__int128)(s64) n * (s64) m) >> 64); sf = 1; break;
            case 6: r = (u64)(((unsigned __int128) n * m) >> 64); sf = 1; break;
            default: undef(c, i); r = 0; }
            SET(rd, r & msk(sf));
        } else undef(c, i);
        break;
    }
    case 7: case 15: {                                         /* SIMD & FP */
        c->pc = pc;
        if ((i & 0x5F000000) == 0x1E000000 || (i & 0xFF000000) == 0x1F000000) { if (fp_insn(c, i) < 0) undef(c, i); }
        else if (simd_insn(c, i) < 0) undef(c, i);
        break;
    }
    default: undef(c, i);
    }
    c->pc = npc;
}

