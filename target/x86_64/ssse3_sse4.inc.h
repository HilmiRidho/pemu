/* target/x86_64/ssse3_sse4.inc.h - SSSE3 / SSE4.1 / SSE4.2. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ SSSE3 / SSE4.1 / SSE4.2 (0F 38 xx, 0F 3A xx)
 * The XMM forms are 66-prefixed; the SSSE3 ones also exist without a prefix on mm registers (mmx38), plus crc32 (F2 0F 38 F0/F1). */
static inline s16 sat16(s32 v) { return (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static inline u16 satu16(s32 v) { return (u16)(v > 65535 ? 65535 : v < 0 ? 0 : v); }
static inline float rnd_f(float v, int md) { switch (md & 3) { case 0: return nearbyintf(v); case 1: return floorf(v); case 2: return ceilf(v); default: return truncf(v); } }
static inline double rnd_d(double v, int md) { switch (md & 3) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); } }

/* PCMP{E,I}STR{I,M} (SDM vol. 2B "Intel SSE4.2 string and text instructions"). a = first operand (xmm1, its length is la),
 * b = second operand (xmm2/m128, length lb). Returns IntRes2 and sets CF/ZF/SF/OF. */
static int pc_elem(const V*r, int imm, int i) {
    switch (imm & 3) { case 0: return r->b[i]; case 1: return r->w[i]; case 2: return r->sb[i]; default: return r->sw[i]; }
}
static int pc_bool(const V*a, const V*b, int imm, int la, int lb, int agg, int j, int i) {     /* BoolRes[j][i]: b[j] against a[i] */
    int va = i < la, vb = j < lb;
    if (agg == 2) { if (!va && !vb) return 1; if (!va || !vb) return 0; }          /* equal each: both invalid -> true, one invalid -> false */
    else if (agg == 3) { if (!va) return 1; if (!vb) return 0; }                    /* equal ordered: invalid needle element -> true */
    else if (!va || !vb) return 0;                                                  /* equal any, ranges */
    int x = pc_elem(a, imm, i), y = pc_elem(b, imm, j);
    if (agg == 1) return (i & 1) ? y <= x : y >= x;                                 /* ranges: even = lower bound, odd = upper bound */
    return x == y;
}
static unsigned pcmpstr(CPU*c, const V*a, const V*b, int imm, int la, int lb) {
    int n = (imm & 1) ? 8 : 16, agg = (imm >> 2) & 3, pol = (imm >> 4) & 3;
    unsigned r1 = 0, all = (1u << n) - 1, r2;
    for (int j = 0; j < n; j++) {
        int v = 0;
        switch (agg) {
        case 0: for (int i = 0; i < n && !v; i++) v = pc_bool(a, b, imm, la, lb, agg, j, i); break;
        case 1: for (int i = 0; i < n && !v; i += 2) v = pc_bool(a, b, imm, la, lb, agg, j, i) && pc_bool(a, b, imm, la, lb, agg, j, i + 1); break;
        case 2: v = pc_bool(a, b, imm, la, lb, agg, j, j); break;
        default: v = 1; for (int k = 0; j + k < n && v; k++) v = pc_bool(a, b, imm, la, lb, agg, j + k, k); break;
        }
        r1 |= (unsigned) v << j;
    }
    r2 = r1;
    if (pol == 1) r2 = ~r1 & all;
    else if (pol == 3) for (int j = 0; j < lb && j < n; j++) r2 ^= 1u << j;           /* masked negative: only valid elements of b are inverted */
    RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
    if (r2) RF(c) |= CF_;
    if (lb < n) RF(c) |= ZF_;
    if (la < n) RF(c) |= SF_;
    if (r2 & 1) RF(c) |= OF_;
    return r2;
}
static int pc_ilen(const V*r, int imm) { int n = (imm & 1) ? 8 : 16, k = 0; while (k < n && ((imm & 1) ? r->w[k] : r->b[k])) k++; return k; }
static int pc_elen(CPU*c, int reg, int w, int imm) {
    int n = (imm & 1) ? 8 : 16; s64 v = w ? (s64) c->r[reg] : (s64)(s32) c->r[reg];
    if (v < 0) v = v == INT64_MIN ? INT64_MAX : -v;
    return v > n ? n : (int) v;
}

static int mmx38(CPU*c, I*in, int op, int is3a) {               /* SSSE3 on mm registers: pshufb, phadd*, phsub*, pmaddubsw, psign*, pmulhrsw, pabs*, palignr */
    if (is3a ? op != 0x0F : !((op <= 0x0B) || (op >= 0x1C && op <= 0x1E))) return -1;
    M m; int imm8 = 0;
    modrm(c, in, &m, is3a);
    if (is3a) imm8 = *in->ip++;
    u64*d = &c->mm[m.reg & 7];
    V t = {{0}}, s = {{0}}, r = {{0}}; t.q[0] = *d; s.q[0] = m.ismem ? *(u64u*) m.ea : c->mm[m.rm & 7];
    if (is3a) {                                                  /* palignr: (dest:src) >> imm8 bytes */
        u8 cat[16]; memcpy(cat, s.b, 8); memcpy(cat + 8, t.b, 8);
        for (int i = 0; i < 8; i++) { int j = i + imm8; r.b[i] = j < 16 ? cat[j] : 0; }
        *d = r.q[0]; return 0;
    }
    switch (op) {
    case 0x00: for (int i = 0; i < 8; i++) r.b[i] = (s.b[i] & 0x80) ? 0 : t.b[s.b[i] & 7]; break;                                     /* pshufb */
    case 0x01: case 0x03: case 0x05: case 0x07: {                                                                                     /* phaddw/phaddsw/phsubw/phsubsw */
        int sub = op >= 5; s16 a[4] = {t.sw[0], t.sw[2], s.sw[0], s.sw[2]}, b[4] = {t.sw[1], t.sw[3], s.sw[1], s.sw[3]};
        for (int i = 0; i < 4; i++) { s32 v = sub ? a[i] - b[i] : a[i] + b[i]; r.sw[i] = (op == 0x03 || op == 0x07) ? sat16(v) : (s16) v; }
        break; }
    case 0x02: case 0x06: { int sub = op >= 5; u32 a[2] = {t.d[0], s.d[0]}, b[2] = {t.d[1], s.d[1]};                                   /* phaddd/phsubd */
        for (int i = 0; i < 2; i++) r.d[i] = sub ? a[i] - b[i] : a[i] + b[i];
        break; }
    case 0x04: for (int i = 0; i < 4; i++) r.sw[i] = sat16((s32) t.b[2 * i] * s.sb[2 * i] + (s32) t.b[2 * i + 1] * s.sb[2 * i + 1]); break;   /* pmaddubsw */
    case 0x08: for (int i = 0; i < 8; i++) r.sb[i] = s.sb[i] < 0 ? (s8)(0 - t.sb[i]) : s.sb[i] == 0 ? 0 : t.sb[i]; break;             /* psignb/w/d */
    case 0x09: for (int i = 0; i < 4; i++) r.sw[i] = s.sw[i] < 0 ? (s16)(0 - t.sw[i]) : s.sw[i] == 0 ? 0 : t.sw[i]; break;
    case 0x0A: for (int i = 0; i < 2; i++) r.sd[i] = s.sd[i] < 0 ? (s32)(0u - t.d[i]) : s.sd[i] == 0 ? 0 : t.sd[i]; break;
    case 0x0B: for (int i = 0; i < 4; i++) r.sw[i] = (s16)(((((s32) t.sw[i] * s.sw[i]) >> 14) + 1) >> 1); break;                      /* pmulhrsw */
    case 0x1C: for (int i = 0; i < 8; i++) r.b[i] = (u8)(s.sb[i] < 0 ? 0 - s.sb[i] : s.sb[i]); break;                                 /* pabsb/w/d */
    case 0x1D: for (int i = 0; i < 4; i++) r.w[i] = (u16)(s.sw[i] < 0 ? 0 - s.sw[i] : s.sw[i]); break;
    default:   for (int i = 0; i < 2; i++) r.d[i] = s.sd[i] < 0 ? 0u - s.d[i] : s.d[i]; break;
    }
    *d = r.q[0]; return 0;
}
static int sse38(CPU*c, I*in, int op, int is3a) {
    M m; V sv, *s = NULL; int imm8 = 0;
    if (!is3a && in->repne) {                                       /* crc32 (CRC-32C), F2 [66|REX.W] 0F 38 F0/F1 */
        if (op != 0xF0 && op != 0xF1) return -1;
        modrm(c, in, &m, 0);
        int sz = op == 0xF0 ? 1 : in->w ? 8 : in->p66 ? 2 : 4;
        u64 v = rdm(c, in, &m, sz); u32 crc = (u32) c->r[m.reg];
        for (int i = 0; i < sz; i++) { crc ^= (u8)(v >> (8 * i)); for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1))); }
        c->r[m.reg] = crc;
        return 0;
    }
    if (!in->p66) return in->rep ? -1 : mmx38(c, in, op, is3a);
    modrm(c, in, &m, is3a ? 1 : 0);
    if (is3a) imm8 = *in->ip++;
    V*d = &c->x[m.reg], t = *d;
    if (!is3a) {
        int pm = (op >= 0x20 && op <= 0x25) || (op >= 0x30 && op <= 0x35);
        if (!pm) SRC(16);
        switch (op) {
        case 0x00: for (int i = 0; i < 16; i++) d->b[i] = (s->b[i] & 0x80) ? 0 : t.b[s->b[i] & 15]; break;                      /* pshufb */
        case 0x01: case 0x02: case 0x03: case 0x05: case 0x06: case 0x07: {                                                    /* phadd/phsub w, d, sw */
            int sub = op >= 5;
            if (op == 0x02 || op == 0x06) {
                u32 a[4] = {t.d[0], t.d[2], s->d[0], s->d[2]}, b[4] = {t.d[1], t.d[3], s->d[1], s->d[3]};
                for (int i = 0; i < 4; i++) d->d[i] = sub ? a[i] - b[i] : a[i] + b[i];
            } else {
                s16 a[8] = {t.sw[0], t.sw[2], t.sw[4], t.sw[6], s->sw[0], s->sw[2], s->sw[4], s->sw[6]};
                s16 b[8] = {t.sw[1], t.sw[3], t.sw[5], t.sw[7], s->sw[1], s->sw[3], s->sw[5], s->sw[7]};
                for (int i = 0; i < 8; i++) { s32 r = sub ? a[i] - b[i] : a[i] + b[i]; d->sw[i] = (op == 0x03 || op == 0x07) ? sat16(r) : (s16) r; }
            }
            break; }
        case 0x04: for (int i = 0; i < 8; i++) d->sw[i] = sat16((s32) t.b[2 * i] * s->sb[2 * i] + (s32) t.b[2 * i + 1] * s->sb[2 * i + 1]); break;   /* pmaddubsw */
        case 0x08: for (int i = 0; i < 16; i++) d->sb[i] = s->sb[i] < 0 ? (s8)(0 - t.sb[i]) : s->sb[i] == 0 ? 0 : t.sb[i]; break;                /* psignb/w/d */
        case 0x09: for (int i = 0; i < 8; i++) d->sw[i] = s->sw[i] < 0 ? (s16)(0 - t.sw[i]) : s->sw[i] == 0 ? 0 : t.sw[i]; break;
        case 0x0A: for (int i = 0; i < 4; i++) d->sd[i] = s->sd[i] < 0 ? (s32)(0u - t.d[i]) : s->sd[i] == 0 ? 0 : t.sd[i]; break;
        case 0x0B: for (int i = 0; i < 8; i++) d->sw[i] = (s16)(((((s32) t.sw[i] * s->sw[i]) >> 14) + 1) >> 1); break;                              /* pmulhrsw */
        case 0x10: for (int i = 0; i < 16; i++) if (c->x[0].b[i] & 0x80) d->b[i] = s->b[i]; break;                                                /* pblendvb */
        case 0x14: for (int i = 0; i < 4; i++) if (c->x[0].d[i] >> 31) d->d[i] = s->d[i]; break;                                                  /* blendvps */
        case 0x15: for (int i = 0; i < 2; i++) if (c->x[0].q[i] >> 63) d->q[i] = s->q[i]; break;                                                  /* blendvpd */
        case 0x17: {                                                                                                                              /* ptest */
            int zf = !((t.q[0] & s->q[0]) | (t.q[1] & s->q[1])), cf = !((~t.q[0] & s->q[0]) | (~t.q[1] & s->q[1]));
            RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
            if (zf) RF(c) |= ZF_;
            if (cf) RF(c) |= CF_;
            break; }
        case 0x1C: for (int i = 0; i < 16; i++) d->b[i] = (u8)(s->sb[i] < 0 ? 0 - s->sb[i] : s->sb[i]); break;                                     /* pabsb/w/d */
        case 0x1D: for (int i = 0; i < 8; i++) d->w[i] = (u16)(s->sw[i] < 0 ? 0 - s->sw[i] : s->sw[i]); break;
        case 0x1E: for (int i = 0; i < 4; i++) d->d[i] = s->sd[i] < 0 ? 0u - s->d[i] : s->d[i]; break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:                                                                         /* pmovsx*, pmovzx* */
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
            static const u8 esz[6] = {1, 1, 1, 2, 2, 4}, dsz[6] = {2, 4, 8, 4, 8, 8};
            int k = op & 15, es = esz[k], ds = dsz[k], cnt = 16 / ds, zx = op >= 0x30;
            SRC(cnt * es);
            for (int i = 0; i < cnt; i++) {
                u64 v = 0; memcpy(&v, s->b + i * es, (size_t) es);
                if (!zx) v = (u64) sx(v, es);
                memcpy(d->b + i * ds, &v, (size_t) ds);
            }
            break; }
        case 0x28: d->sq[0] = (s64) t.sd[0] * s->sd[0]; d->sq[1] = (s64) t.sd[2] * s->sd[2]; break;                                               /* pmuldq */
        case 0x29: for (int i = 0; i < 2; i++) d->q[i] = t.q[i] == s->q[i] ? ~0ULL : 0; break;                                                   /* pcmpeqq */
        case 0x37: for (int i = 0; i < 2; i++) d->q[i] = t.sq[i] > s->sq[i] ? ~0ULL : 0; break;                                                   /* pcmpgtq */
        case 0x2A: if (!m.ismem) return -1; *d = *s; break;                                                                                       /* movntdqa */
        case 0x2B: for (int i = 0; i < 4; i++) { d->w[i] = satu16(t.sd[i]); d->w[4 + i] = satu16(s->sd[i]); } break;                              /* packusdw */
        case 0x38: for (int i = 0; i < 16; i++) d->sb[i] = t.sb[i] < s->sb[i] ? t.sb[i] : s->sb[i]; break;                                        /* pmins/u b,w,d */
        case 0x39: for (int i = 0; i < 4; i++) d->sd[i] = t.sd[i] < s->sd[i] ? t.sd[i] : s->sd[i]; break;
        case 0x3A: for (int i = 0; i < 8; i++) d->w[i] = t.w[i] < s->w[i] ? t.w[i] : s->w[i]; break;
        case 0x3B: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] < s->d[i] ? t.d[i] : s->d[i]; break;
        case 0x3C: for (int i = 0; i < 16; i++) d->sb[i] = t.sb[i] > s->sb[i] ? t.sb[i] : s->sb[i]; break;                                        /* pmaxs/u b,w,d */
        case 0x3D: for (int i = 0; i < 4; i++) d->sd[i] = t.sd[i] > s->sd[i] ? t.sd[i] : s->sd[i]; break;
        case 0x3E: for (int i = 0; i < 8; i++) d->w[i] = t.w[i] > s->w[i] ? t.w[i] : s->w[i]; break;
        case 0x3F: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] > s->d[i] ? t.d[i] : s->d[i]; break;
        case 0x40: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] * s->d[i]; break;                                                                 /* pmulld */
        case 0x41: {                                                                                                                              /* phminposuw */
            u16 mn = s->w[0]; int ix = 0;
            for (int i = 1; i < 8; i++) if (s->w[i] < mn) { mn = s->w[i]; ix = i; }
            memset(d, 0, 16); d->w[0] = mn; d->w[1] = (u16) ix; break; }
        default: return -1;
        }
        return 0;
    }
    int md = (imm8 & 4) ? (int)((c->mxcsr >> 13) & 3) : (imm8 & 3);       /* round*: MXCSR.RC when imm8 bit 2 is set */
    switch (op) {
    case 0x08: SRC(16); for (int i = 0; i < 4; i++) d->f[i] = rnd_f(s->f[i], md); break;                                       /* roundps/pd/ss/sd */
    case 0x09: SRC(16); for (int i = 0; i < 2; i++) d->df[i] = rnd_d(s->df[i], md); break;
    case 0x0A: SRC(4); d->f[0] = rnd_f(s->f[0], md); break;
    case 0x0B: SRC(8); d->df[0] = rnd_d(s->df[0], md); break;
    case 0x0C: SRC(16); for (int i = 0; i < 4; i++) if ((imm8 >> i) & 1) d->d[i] = s->d[i]; break;                               /* blendps/pd, pblendw */
    case 0x0D: SRC(16); for (int i = 0; i < 2; i++) if ((imm8 >> i) & 1) d->q[i] = s->q[i]; break;
    case 0x0E: SRC(16); for (int i = 0; i < 8; i++) if ((imm8 >> i) & 1) d->w[i] = s->w[i]; break;
    case 0x0F: {                                                                                                                /* palignr: (dest:src) >> imm8 bytes */
        SRC(16); u8 cat[32]; memcpy(cat, s->b, 16); memcpy(cat + 16, t.b, 16);
        for (int i = 0; i < 16; i++) { int j = i + imm8; d->b[i] = j < 32 ? cat[j] : 0; }
        break; }
    case 0x14: case 0x15: case 0x16: case 0x17: {                                                                               /* pextrb/w/d/q, extractps */
        int sz = op == 0x14 ? 1 : op == 0x15 ? 2 : (op == 0x16 && in->w) ? 8 : 4;
        u64 v = sz == 1 ? t.b[imm8 & 15] : sz == 2 ? t.w[imm8 & 7] : sz == 8 ? t.q[imm8 & 1] : t.d[imm8 & 3];
        if (m.ismem) mwr(m.ea, v, sz); else setr(c, m.rm, sz == 8 ? 8 : 4, 0, v);
        break; }
    case 0x20: d->b[imm8 & 15] = (u8)(m.ismem ? mrd(m.ea, 1) : getr(c, m.rm, 4, 0)); break;                                      /* pinsrb */
    case 0x21: {                                                                                                                /* insertps */
        int cd = (imm8 >> 4) & 3, cs; if (m.ismem) { SRC(4); cs = 0; } else { SRC(16); cs = (imm8 >> 6) & 3; }
        d->d[cd] = s->d[cs];
        for (int i = 0; i < 4; i++) if ((imm8 >> i) & 1) d->d[i] = 0;
        break; }
    case 0x22: if (in->w) d->q[imm8 & 1] = rdm(c, in, &m, 8); else d->d[imm8 & 3] = (u32) rdm(c, in, &m, 4); break;             /* pinsrd/q */
    case 0x40: {                                                                                                                /* dpps */
        SRC(16); volatile float p[4];                                       /* volatile: no fused multiply-add, the sums must round like the hardware's */
        for (int i = 0; i < 4; i++) p[i] = (imm8 & (0x10 << i)) ? d->f[i] * s->f[i] : 0.0f;
        float lo = p[0] + p[1], hi = p[2] + p[3], sum = lo + hi;
        for (int i = 0; i < 4; i++) d->f[i] = ((imm8 >> i) & 1) ? sum : 0.0f;
        break; }
    case 0x41: {                                                                                                                /* dppd */
        SRC(16); volatile double p[2];
        for (int i = 0; i < 2; i++) p[i] = (imm8 & (0x10 << i)) ? d->df[i] * s->df[i] : 0.0;
        double sum = p[0] + p[1];
        for (int i = 0; i < 2; i++) d->df[i] = ((imm8 >> i) & 1) ? sum : 0.0;
        break; }
    case 0x42: {                                                                                                                /* mpsadbw */
        SRC(16); int so = (imm8 & 3) * 4, dof = ((imm8 >> 2) & 1) * 4;
        for (int i = 0; i < 8; i++) { int sum = 0; for (int k = 0; k < 4; k++) { int df = (int) t.b[dof + i + k] - (int) s->b[so + k]; sum += df < 0 ? -df : df; } d->w[i] = (u16) sum; }
        break; }
    case 0x60: case 0x61: case 0x62: case 0x63: {                                                                               /* pcmpestrm/i, pcmpistrm/i */
        SRC(16);
        int n = (imm8 & 1) ? 8 : 16, la, lb;
        if (op < 0x62) { la = pc_elen(c, RAX, in->w, imm8); lb = pc_elen(c, RDX, in->w, imm8); }
        else { la = pc_ilen(&t, imm8); lb = pc_ilen(s, imm8); }
        unsigned r = pcmpstr(c, &t, s, imm8, la, lb);
        if (op & 1) c->r[RCX] = !r ? (u32) n : (imm8 & 0x40) ? (u32)(31 - __builtin_clz(r)) : (u32) __builtin_ctz(r);
        else {
            V*x0 = &c->x[0]; memset(x0, 0, 16);
            if (imm8 & 0x40) { for (int j = 0; j < n; j++) if ((r >> j) & 1) { if (n == 8) x0->w[j] = 0xffff; else x0->b[j] = 0xff; } }
            else x0->q[0] = r;
        }
        break; }
    default: return -1;
    }
    return 0;
}

