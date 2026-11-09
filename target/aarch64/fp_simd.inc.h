/* target/aarch64/fp_simd.inc.h - FP / SIMD. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ FP / SIMD */
static double fget(CPU*c, int r, int type) {
    if (type == 0) { float f; memcpy(&f, &c->v[r][0], 4); return f; }
    double d; memcpy(&d, &c->v[r][0], 8); return d;
}
static void fput(CPU*c, int r, int type, double v) {
    c->v[r][0] = 0; c->v[r][1] = 0;
    if (type == 0) { float f = (float) v; memcpy(&c->v[r][0], &f, 4); } else memcpy(&c->v[r][0], &v, 8);
}
static u64 fcvt_int(double v, int sf, int uns, int mode) {
    if (isnan(v)) return 0;
    switch (mode) { case 0: v = nearbyint(v); break; case 1: v = ceil(v); break; case 2: v = floor(v); break; case 3: v = trunc(v); break; default: v = round(v); }
    if (uns) {
        double mx = sf ? 18446744073709551616.0 : 4294967296.0;
        if (v <= 0) return 0;
        if (v >= mx) return msk(sf);
        return (u64) v;
    }
    double hi = sf ? 9223372036854775808.0 : 2147483648.0;
    if (v >= hi) return sf ? 0x7fffffffffffffffULL : 0x7fffffffULL;
    if (v < -hi) return sf ? 0x8000000000000000ULL : 0x80000000ULL;
    return (u64)(s64) v & msk(sf);
}
static int fp_insn(CPU*c, u32 i) {
    int type = (i >> 22) & 3, rn = (i >> 5) & 31, rd = i & 31, rm = (i >> 16) & 31;
    int sf = i >> 31;
    if ((i & 0x7F20FC00) == 0x1E200000) {                      /* FP <-> integer */
        int rmode = (i >> 19) & 3, opc = (i >> 16) & 7;
        if (opc == 6 || opc == 7) {                            /* FMOV general */
            if (sf && type == 1 && rmode == 0) { if (opc == 6) SET(rd, c->v[rn][0]); else { c->v[rd][0] = X(rn); c->v[rd][1] = 0; } return 0; }
            if (!sf && type == 0 && rmode == 0) { if (opc == 6) SET(rd, c->v[rn][0] & M32); else { c->v[rd][0] = X(rn) & M32; c->v[rd][1] = 0; } return 0; }
            if (sf && type == 2 && rmode == 1) { if (opc == 6) SET(rd, c->v[rn][1]); else c->v[rd][1] = X(rn); return 0; }
            return -1;
        }
        if (type > 1) return -1;
        if (opc == 2 || opc == 3) {                            /* SCVTF / UCVTF */
            double v = opc == 2 ? (double)(sf ? (s64) X(rn) : (s64)(s32) X(rn)) : (double)(sf ? X(rn) : X(rn) & M32);
            if (type == 0) { float f = opc == 2 ? (float)(sf ? (s64) X(rn) : (s64)(s32) X(rn)) : (float)(sf ? X(rn) : X(rn) & M32); c->v[rd][0] = 0; c->v[rd][1] = 0; memcpy(&c->v[rd][0], &f, 4); }
            else fput(c, rd, 1, v);
            return 0;
        }
        if (opc <= 1 || opc == 4 || opc == 5) {                /* FCVT[NPMZA][SU] */
            int mode = opc >= 4 ? 4 : rmode, uns = opc & 1;
            SET(rd, fcvt_int(fget(c, rn, type), sf, uns, mode));
            return 0;
        }
        return -1;
    }
    if (type > 1) return -1;
    if ((i & 0xFF207C00) == 0x1E204000) {                      /* 1-source */
        int op = (i >> 15) & 0x3f;
        double v = fget(c, rn, type);
        switch (op) {
        case 0: fput(c, rd, type, v); if (type == 1) { u64 t = c->v[rn][0]; c->v[rd][0] = t; } else { u64 t = c->v[rn][0] & M32; c->v[rd][0] = t; } return 0;
        case 1: fput(c, rd, type, fabs(v)); return 0;
        case 2: { u64 t = c->v[rn][0]; if (type) t ^= 1ULL << 63; else t = (t ^ (1ULL << 31)) & M32; c->v[rd][0] = t; c->v[rd][1] = 0; return 0; }
        case 3: fput(c, rd, type, sqrt(v)); return 0;
        case 4: fput(c, rd, 0, v); return 0;
        case 5: fput(c, rd, 1, v); return 0;
        case 8: fput(c, rd, type, nearbyint(v)); return 0;
        case 9: fput(c, rd, type, ceil(v)); return 0;
        case 10: fput(c, rd, type, floor(v)); return 0;
        case 11: fput(c, rd, type, trunc(v)); return 0;
        case 12: fput(c, rd, type, round(v)); return 0;
        case 14: case 15: fput(c, rd, type, nearbyint(v)); return 0;
        }
        return -1;
    }
    if ((i & 0xFF20FC07) == 0x1E202000) {                      /* FCMP / FCMPE */
        double a = fget(c, rn, type), b = (i & 8) ? 0.0 : fget(c, rm, type);
        c->nzcv = (isnan(a) || isnan(b)) ? 3 : a == b ? 6 : a < b ? 8 : 2;
        return 0;
    }
    if ((i & 0xFF201FE0) == 0x1E201000) {                      /* FMOV imm */
        u32 imm = (i >> 13) & 0xff, b6 = (imm >> 6) & 1, sg = imm >> 7;
        c->v[rd][1] = 0;
        if (type == 0) c->v[rd][0] = ((u64) sg << 31) | ((u64)(!b6) << 30) | ((u64)(b6 ? 0x1f : 0) << 25) | ((u64)(imm & 0x3f) << 19);
        else c->v[rd][0] = ((u64) sg << 63) | ((u64)(!b6) << 62) | ((u64)(b6 ? 0xff : 0) << 54) | ((u64)(imm & 0x3f) << 48);
        return 0;
    }
    if ((i & 0xFF200C00) == 0x1E200800) {                      /* 2-source */
        double a = fget(c, rn, type), b = fget(c, rm, type), r;
        switch ((i >> 12) & 15) {
        case 0: r = a * b; break; case 1: r = a / b; break; case 2: r = a + b; break; case 3: r = a - b; break;
        case 4: case 6: r = isnan(a) || isnan(b) ? NAN : fmax(a, b); break;
        case 5: case 7: r = isnan(a) || isnan(b) ? NAN : fmin(a, b); break;
        case 8: r = -(a * b); break;
        default: return -1; }
        fput(c, rd, type, r);
        return 0;
    }
    if ((i & 0xFF200C00) == 0x1E200C00) {                      /* FCSEL */
        int src = cond(c, (i >> 12) & 15) ? rn : rm;
        u64 t = c->v[src][0]; if (type == 0) t &= M32;
        c->v[rd][0] = t; c->v[rd][1] = 0;
        return 0;
    }
    if ((i & 0xFF000000) == 0x1F000000) {                      /* FMADD family */
        int ra = (i >> 10) & 31, o1 = (i >> 21) & 1, o0 = (i >> 15) & 1;
        double a = fget(c, rn, type), b = fget(c, rm, type), d = fget(c, ra, type);
        if (o1) a = -a;
        if (o0) d = -d;
        double r = type == 0 ? (double) fmaf((float) a, (float) b, (float) d) : fma(a, b, d);
        fput(c, rd, type, r);
        return 0;
    }
    return -1;
}
static int simd_insn(CPU*c, u32 i) {
    int rd = i & 31, rn = (i >> 5) & 31, rm = (i >> 16) & 31, q = (i >> 30) & 1;
    if ((i & 0x9F200400) == 0x0E200400 && ((i >> 11) & 31) == 3) {  /* 3-same logical */
        int u = (i >> 29) & 1, sz = (i >> 22) & 3;
        for (int k = 0; k < (q ? 2 : 1); k++) {
            u64 a = c->v[rn][k], b = c->v[rm][k], d = c->v[rd][k], r;
            if (!u) r = sz == 0 ? a & b : sz == 1 ? a & ~b : sz == 2 ? a | b : a | ~b;
            else r = sz == 0 ? a ^ b : sz == 1 ? (d & a) | (~d & b) : sz == 2 ? (b & a) | (~b & d) : (~b & a) | (b & d);
            c->v[rd][k] = r;
        }
        if (!q) c->v[rd][1] = 0;
        return 0;
    }
    if ((i & 0x9F200400) == 0x0E200400 && (((i >> 11) & 31) == 16 || (((i >> 11) & 31) == 17 && ((i >> 29) & 1)))) {   /* 3-same: ADD, SUB, CMEQ */
        int u = (i >> 29) & 1, sz = (i >> 22) & 3, op = (i >> 11) & 31, esz = 1 << sz;
        if (sz == 3 && !q) return -1;
        u8 *d = (u8*) c->v[rd], *n = (u8*) c->v[rn], *m = (u8*) c->v[rm], t[16] = {0};
        for (int k = 0; k < (q ? 16 : 8) / esz; k++) {
            u64 a = 0, b = 0, r; memcpy(&a, n + k * esz, (size_t) esz); memcpy(&b, m + k * esz, (size_t) esz);
            r = op == 17 ? (a == b ? ~0ULL : 0) : u ? a - b : a + b;
            memcpy(t + k * esz, &r, (size_t) esz);
        }
        memcpy(d, t, 16); return 0;
    }
    if ((i & 0x9F200400) == 0x0E200400) {                      /* 3-same, the other integer ops the x86-64 JIT emits */
        int u = (i >> 29) & 1, sz = (i >> 22) & 3, op = (i >> 11) & 31, esz = 1 << sz, bits = esz * 8;
        if (!(op == 1 || op == 2 || op == 5 || op == 6 || op == 12 || op == 13 || (op == 19 && !u))) return -1;
        if (sz == 3 && (!q || op == 19)) return -1;
        typedef __int128 i128;
        i128 umaxv = ((i128) 1 << bits) - 1, smaxv = ((i128) 1 << (bits - 1)) - 1, sminv = -((i128) 1 << (bits - 1));
        u8 *d = (u8*) c->v[rd], *n = (u8*) c->v[rn], *m = (u8*) c->v[rm], t[16] = {0};
        for (int k = 0; k < (q ? 16 : 8) / esz; k++) {
            u64 a = 0, b = 0; memcpy(&a, n + k * esz, (size_t) esz); memcpy(&b, m + k * esz, (size_t) esz);
            i128 x = u ? (i128) a : (i128)((s64)(a << (64 - bits)) >> (64 - bits));      /* unsigned or sign-extended lanes */
            i128 y = u ? (i128) b : (i128)((s64)(b << (64 - bits)) >> (64 - bits));
            i128 lo = u ? 0 : sminv, hi = u ? umaxv : smaxv, r = 0;
            switch (op) {
            case 1: r = x + y; r = r < lo ? lo : r > hi ? hi : r; break;                  /* SQADD / UQADD */
            case 2: r = (x + y + 1) >> 1; break;                                          /* SRHADD / URHADD */
            case 5: r = x - y; r = r < lo ? lo : r > hi ? hi : r; break;                  /* SQSUB / UQSUB */
            case 6: r = x > y ? -1 : 0; break;                                            /* CMGT / CMHI */
            case 12: r = x > y ? x : y; break;                                            /* SMAX / UMAX */
            case 13: r = x < y ? x : y; break;                                            /* SMIN / UMIN */
            default: r = (i128)(a * b); break;                                            /* MUL (low half) */
            }
            u64 w = (u64) r; memcpy(t + k * esz, &w, (size_t) esz);
        }
        memcpy(d, t, 16); return 0;
    }
    if ((i & 0x9F800400) == 0x0F000400 && ((i >> 19) & 15) && (((i >> 11) & 31) == 0 || ((i >> 11) & 31) == 10)) {   /* USHR / SSHR / SHL (vector, immediate) */
        int u = (i >> 29) & 1, immh = (i >> 19) & 15, sh = (i >> 16) & 127, op = (i >> 11) & 31;
        int sz = 31 - __builtin_clz((unsigned) immh), esz = 1 << sz, bits = esz * 8;
        if (sz == 3 && !q) return -1;
        if (op == 0 || op == 10) {
            int n = op == 10 ? sh - bits : 2 * bits - sh;
            u8 *d = (u8*) c->v[rd], *nn = (u8*) c->v[rn], t[16] = {0};
            for (int k = 0; k < (q ? 16 : 8) / esz; k++) {
                u64 a = 0, r; memcpy(&a, nn + k * esz, (size_t) esz);
                if (op == 10) r = a << n;
                else if (u) r = a >> n;
                else r = (u64)(((s64)(a << (64 - bits))) >> (64 - bits) >> n);
                memcpy(t + k * esz, &r, (size_t) esz);
            }
            memcpy(d, t, 16); return 0;
        }
    }
    if ((i & 0xBF3FFC00) == 0x2E212800) {                      /* SQXTUN / SQXTUN2: signed lanes narrowed to unsigned half-width with saturation */
        int sz = (i >> 22) & 3; if (sz == 3) return -1;
        int esz = 1 << sz, wide = esz * 2, lanes = 8 / esz;
        u8 *d = (u8*) c->v[rd], *n = (u8*) c->v[rn], t[16] = {0};
        if (q) memcpy(t, d, 8);
        for (int k = 0; k < lanes; k++) {
            u64 a = 0; memcpy(&a, n + k * wide, (size_t) wide);
            s64 v = wide == 2 ? (s64)(int16_t) a : wide == 4 ? (s64)(int32_t) a : (s64) a;
            s64 mx = (1LL << (esz * 8)) - 1; u64 r = (u64)(v < 0 ? 0 : v > mx ? mx : v);
            memcpy(t + (q ? 8 : 0) + k * esz, &r, (size_t) esz);
        }
        memcpy(d, t, 16); return 0;
    }
    if ((i & 0xBF20FC00) == 0x0E003800 || (i & 0xBF20FC00) == 0x0E007800) {   /* ZIP1 / ZIP2 */
        int sz = (i >> 22) & 3, esz = 1 << sz, part = (i >> 14) & 1, lanes = (q ? 16 : 8) / esz, half = lanes / 2;
        if (sz == 3 && !q) return -1;
        u8 *d = (u8*) c->v[rd], *n = (u8*) c->v[rn], *m = (u8*) c->v[rm], t[16] = {0};
        for (int k = 0; k < half; k++) {
            memcpy(t + (2 * k) * esz, n + ((part ? half : 0) + k) * esz, (size_t) esz);
            memcpy(t + (2 * k + 1) * esz, m + ((part ? half : 0) + k) * esz, (size_t) esz);
        }
        memcpy(d, t, 16); return 0;
    }
    if ((i & 0x9FE08400) == 0x0E000400) {                      /* copy: DUP (element/general), INS, SMOV, UMOV */
        int imm5 = (i >> 16) & 31, imm4 = (i >> 11) & 15, op = (i >> 29) & 1;
        if (!(imm5 & 15)) return -1;
        int sz = __builtin_ctz((unsigned) imm5), idx = imm5 >> (sz + 1), esz = 1 << sz;
        u8*d = (u8*) c->v[rd], *n = (u8*) c->v[rn];
        if (op) {                                              /* INS (element) */
            if (!q) return -1;
            int idx2 = imm4 >> sz; u8 t[8];
            memcpy(t, n + idx2 * esz, (size_t) esz); memcpy(d + idx * esz, t, (size_t) esz);
            return 0;
        }
        switch (imm4) {
        case 0: case 1: {                                      /* DUP */
            if (sz == 3 && !q) return -1;
            u64 e = 0; u8 t[16] = {0};
            if (imm4 == 0) memcpy(&e, n + idx * esz, (size_t) esz); else e = X(rn);
            for (int k = 0; k < (q ? 16 : 8) / esz; k++) memcpy(t + k * esz, &e, (size_t) esz);
            memcpy(d, t, 16); return 0; }
        case 3: {                                              /* INS (general) */
            if (!q) return -1;
            u64 e = X(rn); memcpy(d + idx * esz, &e, (size_t) esz); return 0; }
        case 5: {                                              /* SMOV */
            if (sz == 3 || (!q && sz == 2)) return -1;
            u64 e = 0; memcpy(&e, n + idx * esz, (size_t) esz);
            s64 v = sz == 0 ? (s64)(int8_t) e : sz == 1 ? (s64)(int16_t) e : (s64)(int32_t) e;
            SET(rd, q ? (u64) v : (u64) v & M32); return 0; }
        case 7: {                                              /* UMOV */
            if (q ? sz != 3 : sz == 3) return -1;
            u64 e = 0; memcpy(&e, n + idx * esz, (size_t) esz);
            SET(rd, e); return 0; }
        default: return -1;
        }
    }
    if ((i & 0x9FF80400) == 0x0F000400) {                      /* modified immediate: MOVI / MVNI / ORR / BIC / FMOV (vector) */
        int cm = (i >> 12) & 15, op = (i >> 29) & 1;
        u64 imm8 = (((i >> 16) & 7) << 5) | ((i >> 5) & 31), v = 0, w;
        int mod = 0;                                           /* 1: ORR (op=0) / BIC (op=1) on the old value */
        if ((i >> 11) & 1) return -1;                          /* o2: half-precision FMOV, not supported */
        if (cm < 8) { u64 x = imm8 << (8 * (cm >> 1)); v = x | x << 32; mod = cm & 1; }
        else if (cm < 12) { u64 x = imm8 << (8 * ((cm >> 1) & 1)); v = x | x << 16 | x << 32 | x << 48; mod = cm & 1; }
        else if (cm < 14) { u64 x = cm == 12 ? (imm8 << 8) | 0xff : (imm8 << 16) | 0xffff; v = x | x << 32; }
        else if (cm == 14) {
            if (!op) v = imm8 * 0x0101010101010101ULL;
            else for (int b = 0; b < 8; b++) if (imm8 >> b & 1) v |= 0xffULL << (8 * b);
        } else if (!op) {                                      /* FMOV Vd.2S/4S, #imm */
            u64 f = ((imm8 & 0x80) << 24) | ((imm8 & 0x40) ? 0x3e000000ULL : 0x40000000ULL) | ((imm8 & 0x3f) << 19);
            v = f | f << 32;
        } else {                                               /* FMOV Vd.2D, #imm (Q=1 only) */
            if (!q) return -1;
            v = ((imm8 & 0x80) << 56) | ((imm8 & 0x40) ? 0x3fc0000000000000ULL : 0x4000000000000000ULL) | ((imm8 & 0x3f) << 48);
        }
        for (int k = 0; k < 2; k++) {
            if (k && !q) { c->v[rd][1] = 0; break; }
            if (cm < 14 && mod) w = op ? c->v[rd][k] & ~v : c->v[rd][k] | v;
            else w = (cm < 14 && op) ? ~v : v;
            c->v[rd][k] = w;
        }
        return 0;
    }
    return -1;
}

