/* target/aarch64/load_store.inc.h - load / store. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ load / store */
static int ldst(CPU*c, u32 i) {
    int rn = (i >> 5) & 31, rt = i & 31;
    if ((i & 0x3B000000) == 0x18000000) {                      /* literal */
        int opc = i >> 30, V = (i >> 26) & 1;
        u64 a = c->pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        if (!V) {
            if (opc == 3) return 0;
            SET(rt, opc == 0 ? mrd(a, 4) : opc == 1 ? mrd(a, 8) : (u64) sext(mrd(a, 4), 32));
        } else {
            int nb = 4 << opc; if (opc > 2) return -1;
            c->v[rt][0] = c->v[rt][1] = 0; memcpy(&c->v[rt][0], (void*) a, (size_t) nb);
        }
        return 0;
    }
    if ((i & 0x3A000000) == 0x28000000) {                      /* pair */
        int opc = i >> 30, V = (i >> 26) & 1, L = (i >> 22) & 1, idx = (i >> 23) & 3, rt2 = (i >> 10) & 31;
        int sc, nb;
        if (!V) { if (opc == 3) return -1; sc = opc == 2 ? 3 : 2; } else { if (opc == 3) return -1; sc = 2 + opc; }
        nb = 1 << sc;
        s64 off = sext((i >> 15) & 0x7f, 7) << sc;
        u64 base = XS(rn), a = base, nbase = base;
        if (idx == 1) nbase = base + (u64) off; else if (idx == 3) { a = base + (u64) off; nbase = a; } else a = base + (u64) off;
        if (!V) {
            if (L) {
                u64 v1 = mrd(a, nb), v2 = mrd(a + (u64) nb, nb);
                if (opc == 1) { v1 = (u64) sext(v1, 32); v2 = (u64) sext(v2, 32); }
                SET(rt, v1); SET(rt2, v2);
            } else { u64 v1 = X(rt), v2 = X(rt2); mwr(a, v1, nb); mwr(a + (u64) nb, v2, nb); }
        } else {
            if (L) {
                u64 t1[2] = {0, 0}, t2[2] = {0, 0};
                memcpy(t1, (void*) a, (size_t) nb); memcpy(t2, (void*)(a + (u64) nb), (size_t) nb);
                memcpy(c->v[rt], t1, 16); memcpy(c->v[rt2], t2, 16);
            } else { u64 t1[2], t2[2]; memcpy(t1, c->v[rt], 16); memcpy(t2, c->v[rt2], 16); memcpy((void*) a, t1, (size_t) nb); memcpy((void*)(a + (u64) nb), t2, (size_t) nb); }
        }
        if (idx == 1 || idx == 3) SETS(rn, nbase);
        return 0;
    }
    if ((i & 0x3F200C00) == 0x19000000) {                      /* LDAPUR / STLUR / LDAPURS* (FEAT_LRCPC2): unscaled offset, acquire-PC / release */
        int size = i >> 30, opc = (i >> 22) & 3, nb = 1 << size;
        u64 a = XS(rn) + (u64) sext((i >> 12) & 0x1ff, 9);
#ifdef PEMU_EMBED
        /* strict on purpose: without FEAT_LSE2 the hardware raises an alignment fault for these (with it, when the access crosses
         * a 16-byte boundary), and pemu_x64's JIT must never emit one for an address that is not naturally aligned */
        if (a & (u64)(nb - 1)) { fprintf(stderr, "pemu: misaligned LDAPUR/STLUR at %#llx (size %d)\n", (unsigned long long) a, nb); abort(); }
#endif
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (opc == 0) mwr(a, X(rt), nb);
        else if (opc == 1) SET(rt, mrd(a, nb));
        else {
            if (size == 3 || (opc == 3 && size == 2)) return -1;
            u64 v = (u64) sext(mrd(a, nb), nb * 8); if (opc == 3) v &= M32;
            SET(rt, v);
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        return 0;
    }
    if ((i & 0x3B000000) == 0x38000000 || (i & 0x3B000000) == 0x39000000) {   /* register */
        int size = i >> 30, V = (i >> 26) & 1, opc = (i >> 22) & 3, sc, nb, isld, sgn = 0, to64 = 0;
        if (!V) {
            sc = size; nb = 1 << size;
            if (opc == 0) isld = 0; else if (opc == 1) isld = 1;
            else { if (size == 3) { if (opc == 2) return 0; return -1; } if (opc == 3 && size == 2) return -1; isld = 1; sgn = 1; to64 = opc == 2; }
        } else {
            if (opc >= 2 && size == 0) { nb = 16; sc = 4; isld = opc & 1; }
            else if (opc < 2) { nb = 1 << size; sc = size; isld = opc & 1; }
            else return -1;
        }
        u64 base = XS(rn), a, nbase = 0; int wb = 0;
        if ((i >> 24) & 1) a = base + ((u64)((i >> 10) & 0xfff) << sc);
        else if (!((i >> 21) & 1)) {
            s64 imm = sext((i >> 12) & 0x1ff, 9); int m = (i >> 10) & 3;
            if (m == 1) { a = base; nbase = base + (u64) imm; wb = 1; }
            else if (m == 3) { a = base + (u64) imm; nbase = a; wb = 1; }
            else a = base + (u64) imm;
        } else if (((i >> 10) & 3) == 2) {
            u64 off = extendv(X((i >> 16) & 31), (i >> 13) & 7, ((i >> 12) & 1) ? sc : 0);
            a = base + off;
        } else return -1;
        if (!V) {
            if (isld) {
                u64 v = mrd(a, nb);
                if (sgn) { v = (u64) sext(v, nb * 8); if (!to64) v &= M32; }
                SET(rt, v);
            } else mwr(a, X(rt), nb);
        } else {
            if (isld) { u64 t[2] = {0, 0}; memcpy(t, (void*) a, (size_t) nb); memcpy(c->v[rt], t, 16); }
            else { u64 t[2]; memcpy(t, c->v[rt], 16); memcpy((void*) a, t, (size_t) nb); }
        }
        if (wb) SETS(rn, nbase);
        return 0;
    }
    if ((i & 0x3F000000) == 0x08000000) {                      /* exclusive / acquire-release */
        int size = i >> 30, o2 = (i >> 23) & 1, L = (i >> 22) & 1, o1 = (i >> 21) & 1, rs = (i >> 16) & 31, nb = 1 << size;
        if (o1) return -1;
        u64 a = XS(rn);
        if (o2) {                                              /* LDAR / STLR (and LORegion variants) */
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if (L) SET(rt, mrd(a, nb)); else mwr(a, X(rt), nb);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            return 0;
        }
        if (L) { c->xaddr = a; c->xsz = nb; c->xval = mrd(a, nb); SET(rt, c->xval); return 0; }
        int ok = 0;
        if (c->xaddr == a && c->xsz == nb) {
            u64 nv = X(rt);
            switch (nb) {
            case 1: { u8 e = (u8) c->xval; ok = __atomic_compare_exchange_n((u8*) a, &e, (u8) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            case 2: { u16 e = (u16) c->xval; ok = __atomic_compare_exchange_n((u16*) a, &e, (u16) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            case 4: { u32 e = (u32) c->xval; ok = __atomic_compare_exchange_n((u32*) a, &e, (u32) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            default: { u64 e = c->xval; ok = __atomic_compare_exchange_n((u64*) a, &e, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
            }
        }
        c->xaddr = 0;
        SET(rs, ok ? 0 : 1);
        return 0;
    }
    if ((i & 0xBFA00000) == 0x0C000000 || (i & 0xBFA00000) == 0x0C800000) {   /* LD1/ST1 multiple structures */
        int q = (i >> 30) & 1, L = (i >> 22) & 1, op = (i >> 12) & 15, post = (i >> 23) & 1, nr;
        switch (op) { case 7: nr = 1; break; case 10: nr = 2; break; case 6: nr = 3; break; case 2: nr = 4; break; default: return -1; }
        int nb = q ? 16 : 8;
        u64 a = XS(rn);
        for (int k = 0; k < nr; k++) {
            int r = (rt + k) & 31;
            if (L) { c->v[r][0] = c->v[r][1] = 0; memcpy(c->v[r], (void*)(a + (u64)(k * nb)), (size_t) nb); }
            else memcpy((void*)(a + (u64)(k * nb)), c->v[r], (size_t) nb);
        }
        if (post) { int rm = (i >> 16) & 31; SETS(rn, a + (rm == 31 ? (u64)(nr * nb) : X(rm))); }
        return 0;
    }
    return -1;
}

