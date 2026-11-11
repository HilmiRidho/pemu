/* target/x86_64/mmx.inc.h - MMX. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ MMX (no prefix 0F xx, and the SSSE3 forms of 0F 38 / 0F 3A)
 * mm0-mm7 live in c->mm. On real hardware they alias the x87 registers; here they are separate, which is only visible to code
 * that mixes MMX and x87 without emms (and emms is then a no-op). The lane operations of the SSE2 integer set (pint) work on
 * the low 64 bits unchanged; pack, punpckh*, pshufw, pextrw/pinsrw, pmovmskb and the shifts by immediate are done here. */
static int mmx_op(int op) {
    return (op >= 0x60 && op <= 0x6B) || op == 0x6E || op == 0x6F || (op >= 0x70 && op <= 0x77) || op == 0x7E || op == 0x7F ||
           op == 0xC4 || op == 0xC5 || (op >= 0xD1 && op <= 0xD5) || (op >= 0xD7 && op <= 0xDF) || (op >= 0xE0 && op <= 0xE5) ||
           (op >= 0xE7 && op <= 0xEF) || (op >= 0xF1 && op <= 0xFE);
}
static int mmx(CPU*c, I*in, int op) {
    if (op == 0x77) return 0;                                   /* emms */
    M m; int imm8 = 0, ok = 1;
    int immb = op == 0x70 || op == 0xC4 || op == 0xC5 || (op >= 0x71 && op <= 0x73);
    modrm(c, in, &m, immb);
    if (immb) imm8 = *in->ip++;
    u64*d = &c->mm[m.reg & 7], sv = 0;
#define MSRC() do { sv = m.ismem ? *(u64u*) m.ea : c->mm[m.rm & 7]; } while (0)
    switch (op) {
    case 0x6E: *d = rdm(c, in, &m, in->w ? 8 : 4); return 0;                                  /* movd/movq mm, r/m */
    case 0x7E: wrm(c, in, &m, in->w ? 8 : 4, *d); return 0;                                  /* movd/movq r/m, mm */
    case 0x6F: MSRC(); *d = sv; return 0;                                                     /* movq mm, mm/m64 */
    case 0x7F: if (m.ismem) *(u64u*) m.ea = *d; else c->mm[m.rm & 7] = *d; return 0;
    case 0xE7: if (!m.ismem) return -1; *(u64u*) m.ea = *d; return 0;                        /* movntq */
    case 0x70: { MSRC(); u64 r = 0;                                                           /* pshufw */
        for (int i = 0; i < 4; i++) r |= ((sv >> (16 * ((imm8 >> (2 * i)) & 3))) & 0xffff) << (16 * i);
        *d = r; return 0; }
    case 0x71: case 0x72: case 0x73: {                                                        /* shift groups by imm8 */
        if (m.ismem) return -1;
        int ext = m.reg & 7, sop = -1;
        if (op == 0x71) sop = ext == 2 ? 0xD1 : ext == 4 ? 0xE1 : ext == 6 ? 0xF1 : -1;
        else if (op == 0x72) sop = ext == 2 ? 0xD2 : ext == 4 ? 0xE2 : ext == 6 ? 0xF2 : -1;
        else sop = ext == 2 ? 0xD3 : ext == 6 ? 0xF3 : -1;
        if (sop < 0) return -1;
        V a = {{0}}, n = {{0}}; a.q[0] = c->mm[m.rm & 7]; n.q[0] = (u8) imm8;
        pint(sop, &a, &n, &ok); c->mm[m.rm & 7] = a.q[0]; return 0; }
    case 0xC4: { u64 v = m.ismem ? mrd(m.ea, 2) : getr(c, m.rm, 4, 0); int sh = 16 * (imm8 & 3);          /* pinsrw */
        *d = (*d & ~(0xffffULL << sh)) | ((v & 0xffff) << sh); return 0; }
    case 0xC5: if (m.ismem) return -1; setr(c, m.reg, 4, 0, (c->mm[m.rm & 7] >> (16 * (imm8 & 3))) & 0xffff); return 0;       /* pextrw */
    case 0xD7: { if (m.ismem) return -1; u64 r = c->mm[m.rm & 7], v = 0;                                 /* pmovmskb */
        for (int i = 0; i < 8; i++) v |= ((r >> (8 * i + 7)) & 1) << i;
        setr(c, m.reg, 4, 0, v); return 0; }
    case 0xF7: { if (m.ismem) return -1; u64 mk = c->mm[m.rm & 7], dv = *d; u8*dst = (u8*)(c->r[RDI] + in->seg);   /* maskmovq */
        for (int i = 0; i < 8; i++) if ((mk >> (8 * i + 7)) & 1) dst[i] = (u8)(dv >> (8 * i));
        return 0; }
    case 0x63: case 0x67: case 0x6B: case 0x68: case 0x69: case 0x6A: {
        MSRC(); V a = {{0}}, b = {{0}}, r = {{0}}; a.q[0] = *d; b.q[0] = sv;
        switch (op) {
        case 0x63: for (int i = 0; i < 4; i++) { int v = a.sw[i], w = b.sw[i]; r.sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); r.sb[4 + i] = (s8)(w > 127 ? 127 : w < -128 ? -128 : w); } break;       /* packsswb */
        case 0x67: for (int i = 0; i < 4; i++) { int v = a.sw[i], w = b.sw[i]; r.b[i] = (u8)(v > 255 ? 255 : v < 0 ? 0 : v); r.b[4 + i] = (u8)(w > 255 ? 255 : w < 0 ? 0 : w); } break;             /* packuswb */
        case 0x6B: for (int i = 0; i < 2; i++) { s64 v = a.sd[i], w = b.sd[i]; r.sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); r.sw[2 + i] = (s16)(w > 32767 ? 32767 : w < -32768 ? -32768 : w); } break;   /* packssdw */
        case 0x68: for (int i = 0; i < 4; i++) { r.b[2 * i] = a.b[4 + i]; r.b[2 * i + 1] = b.b[4 + i]; } break;       /* punpckhbw */
        case 0x69: for (int i = 0; i < 2; i++) { r.w[2 * i] = a.w[2 + i]; r.w[2 * i + 1] = b.w[2 + i]; } break;       /* punpckhwd */
        default: r.d[0] = a.d[1]; r.d[1] = b.d[1]; break;                                                            /* punpckhdq */
        }
        *d = r.q[0]; return 0; }
    default: {                                                                                /* the rest is lane-wise: the low half of the SSE2 operation */
        MSRC(); V a = {{0}}, b = {{0}}; a.q[0] = *d; b.q[0] = sv;
        pint(op, &a, &b, &ok);
        if (!ok) return -1;
        *d = a.q[0]; return 0; }
    }
#undef MSRC
}

static int sse(CPU*c, I*in, int op) {
    int pf = in->rep ? 3 : in->repne ? 2 : in->p66 ? 1 : 0, imm8 = 0, ok = 1;
    M m; V sv, *s = NULL;
    if (pf == 0 && mmx_op(op)) return mmx(c, in, op);                  /* no prefix and one of the MMX opcodes: mm registers, not xmm */
    int immb = (op == 0x70 || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6 || (op >= 0x71 && op <= 0x73)) ? 1 : 0;
    modrm(c, in, &m, immb);
    if (immb) imm8 = *in->ip++;
    V*d = &c->x[m.reg];
    switch (op) {
    case 0x10:
        if (pf <= 1) { SRC(16); *d = *s; }
        else if (m.ismem) { d->q[0] = d->q[1] = 0; memcpy(d, (void*) m.ea, pf == 3 ? 4 : 8); }
        else memcpy(d, &c->x[m.rm], pf == 3 ? 4 : 8);
        break;
    case 0x11:
        if (pf <= 1) { if (m.ismem) memcpy((void*) m.ea, d, 16); else c->x[m.rm] = *d; }
        else if (m.ismem) memcpy((void*) m.ea, d, pf == 3 ? 4 : 8);
        else memcpy(&c->x[m.rm], d, pf == 3 ? 4 : 8);
        break;
    case 0x12:
        if (pf == 3) { SRC(16); d->d[0] = d->d[1] = s->d[0]; d->d[2] = d->d[3] = s->d[2]; }       /* movsldup */
        else if (pf == 2) { SRC(8); d->q[0] = d->q[1] = s->q[0]; }                                 /* movddup */
        else if (m.ismem) d->q[0] = *(u64u*) m.ea; else d->q[0] = c->x[m.rm].q[1];
        break;
    case 0x13: if (!m.ismem) return -1; *(u64u*) m.ea = d->q[0]; break;
    case 0x16:
        if (pf == 3) { SRC(16); d->d[0] = d->d[1] = s->d[1]; d->d[2] = d->d[3] = s->d[3]; }       /* movshdup */
        else if (m.ismem) d->q[1] = *(u64u*) m.ea; else d->q[1] = c->x[m.rm].q[0];
        break;
    case 0x17: if (!m.ismem) return -1; *(u64u*) m.ea = d->q[1]; break;
    case 0x14: SRC(16); if (pf == 0) { V t = *d; d->f[0] = t.f[0]; d->f[1] = s->f[0]; d->f[2] = t.f[1]; d->f[3] = s->f[1]; } else { d->q[1] = s->q[0]; } break;
    case 0x15: SRC(16); if (pf == 0) { V t = *d; d->f[0] = t.f[2]; d->f[1] = s->f[2]; d->f[2] = t.f[3]; d->f[3] = s->f[3]; } else { d->q[0] = d->q[1]; d->q[1] = s->q[1]; } break;
    case 0x28: case 0x6F: if (op == 0x6F && pf == 0) return -1; SRC(16); *d = *s; break;
    case 0x29: case 0x7F: case 0x2B: case 0xE7:
        if (m.ismem) memcpy((void*) m.ea, d, 16); else c->x[m.rm] = *d;
        break;
    case 0x2A: {
        u64 v = rdm(c, in, &m, in->w ? 8 : 4);
        if (pf == 3) d->f[0] = (float)(in->w ? (s64) v : (s64)(s32) v);
        else if (pf == 2) d->df[0] = (double)(in->w ? (s64) v : (s64)(s32) v);
        else return -1;
        break; }
    case 0x2C: case 0x2D: {
        int sz = in->w ? 8 : 4;
        if (pf == 3) { SRC(4); setr(c, m.reg, sz, 0, cvt_i((double) s->f[0], sz, op == 0x2C)); }
        else if (pf == 2) { SRC(8); setr(c, m.reg, sz, 0, cvt_i(s->df[0], sz, op == 0x2C)); }
        else return -1;
        break; }
    case 0x2E: case 0x2F: {
        double a, b;
        if (pf == 0) { SRC(4); a = d->f[0]; b = s->f[0]; } else if (pf == 1) { SRC(8); a = d->df[0]; b = s->df[0]; } else return -1;
        RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
        if (isnan(a) || isnan(b)) RF(c) |= ZF_ | PF_ | CF_; else if (a < b) RF(c) |= CF_; else if (a == b) RF(c) |= ZF_;
        break; }
    case 0x50: { if (m.ismem) return -1; V*r = &c->x[m.rm]; u64 v = 0;
        if (pf == 0) { for (int i = 0; i < 4; i++) v |= (u64)(r->d[i] >> 31) << i; } else { for (int i = 0; i < 2; i++) v |= (r->q[i] >> 63) << i; }
        setr(c, m.reg, 4, 0, v); break; }
    case 0x51: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        if (pf == 0) { SRC(16); FARITH(float, sqrtf, f, 4) }
        else if (pf == 1) { SRC(16); FARITH(double, sqrt, df, 2) }
        else if (pf == 3) { SRC(4); FARITH(float, sqrtf, f, 1) }
        else { SRC(8); FARITH(double, sqrt, df, 1) }
        break;
    case 0x54: case 0x55: case 0x56: case 0x57:
        SRC(16);
        for (int i = 0; i < 2; i++) { u64 a = d->q[i], b = s->q[i]; d->q[i] = op == 0x54 ? a & b : op == 0x55 ? ~a & b : op == 0x56 ? a | b : a ^ b; }
        break;
    case 0x5A:
        if (pf == 0) { SRC(8); double a = s->f[0], b = s->f[1]; d->df[0] = a; d->df[1] = b; }
        else if (pf == 1) { SRC(16); float a = (float) s->df[0], b = (float) s->df[1]; d->f[0] = a; d->f[1] = b; d->q[1] = 0; }
        else if (pf == 3) { SRC(4); d->df[0] = (double) s->f[0]; }
        else { SRC(8); d->f[0] = (float) s->df[0]; }
        break;
    case 0x5B:
        SRC(16);
        if (pf == 0) for (int i = 0; i < 4; i++) d->f[i] = (float) s->sd[i];
        else if (pf == 1 || pf == 3) for (int i = 0; i < 4; i++) d->sd[i] = (s32) cvt_i((double) s->f[i], 4, pf == 3);
        else return -1;
        break;
    case 0xE6:
        if (pf == 3) { SRC(8); double a = s->sd[0], b = s->sd[1]; d->df[0] = a; d->df[1] = b; }
        else if (pf == 2 || pf == 1) { SRC(16); s32 a = (s32) cvt_i(s->df[0], 4, pf == 1), b = (s32) cvt_i(s->df[1], 4, pf == 1); d->sd[0] = a; d->sd[1] = b; d->q[1] = 0; }
        else return -1;
        break;
    case 0xC2: {
        if (pf == 0) { SRC(16); for (int i = 0; i < 4; i++) d->d[i] = cmp_pred(imm8, d->f[i], s->f[i]) ? ~0u : 0; }
        else if (pf == 1) { SRC(16); for (int i = 0; i < 2; i++) d->q[i] = cmp_pred(imm8, d->df[i], s->df[i]) ? ~0ULL : 0; }
        else if (pf == 3) { SRC(4); d->d[0] = cmp_pred(imm8, d->f[0], s->f[0]) ? ~0u : 0; }
        else { SRC(8); d->q[0] = cmp_pred(imm8, d->df[0], s->df[0]) ? ~0ULL : 0; }
        break; }
    case 0xC6: SRC(16);
        if (pf == 0) { V t = *d; d->f[0] = t.f[imm8 & 3]; d->f[1] = t.f[(imm8 >> 2) & 3]; d->f[2] = s->f[(imm8 >> 4) & 3]; d->f[3] = s->f[(imm8 >> 6) & 3]; }
        else { V t = *d; d->q[0] = t.q[imm8 & 1]; d->q[1] = s->q[(imm8 >> 1) & 1]; }
        break;
    case 0x70: SRC(16);
        if (pf == 1) { for (int i = 0; i < 4; i++) d->d[i] = s->d[(imm8 >> (2 * i)) & 3]; }
        else if (pf == 2) { for (int i = 0; i < 4; i++) d->w[i] = s->w[(imm8 >> (2 * i)) & 3]; d->q[1] = s->q[1]; }
        else if (pf == 3) { for (int i = 0; i < 4; i++) d->w[4 + i] = s->w[4 + ((imm8 >> (2 * i)) & 3)]; d->q[0] = s->q[0]; }
        else return -1;
        break;
    case 0x71: case 0x72: case 0x73: {
        if (m.ismem || pf != 1) return -1;
        V*r = &c->x[m.rm]; int ext = m.reg & 7; unsigned n = (unsigned) imm8;
        if (op == 0x71) { for (int i = 0; i < 8; i++) { if (ext == 2) r->w[i] = n > 15 ? 0 : (u16)(r->w[i] >> n); else if (ext == 4) r->sw[i] = (s16)(r->sw[i] >> (n > 15 ? 15 : n)); else if (ext == 6) r->w[i] = n > 15 ? 0 : (u16)(r->w[i] << n); else return -1; } }
        else if (op == 0x72) { for (int i = 0; i < 4; i++) { if (ext == 2) r->d[i] = n > 31 ? 0 : r->d[i] >> n; else if (ext == 4) r->sd[i] = r->sd[i] >> (n > 31 ? 31 : n); else if (ext == 6) r->d[i] = n > 31 ? 0 : r->d[i] << n; else return -1; } }
        else {
            if (ext == 2 || ext == 6) { for (int i = 0; i < 2; i++) r->q[i] = n > 63 ? 0 : (ext == 2 ? r->q[i] >> n : r->q[i] << n); }
            else if (ext == 3 || ext == 7) { V t = *r; memset(r, 0, 16); if (n < 16) for (int i = 0; i < 16; i++) { int j = ext == 3 ? i + (int) n : i - (int) n; if (j >= 0 && j < 16) r->b[i] = t.b[j]; } }
            else return -1;
        }
        break; }
    case 0xC4: d->w[imm8 & 7] = (u16)(m.ismem ? mrd(m.ea, 2) : getr(c, m.rm, 4, 0)); break;
    case 0xC5: if (m.ismem) return -1; setr(c, m.reg, 4, 0, c->x[m.rm].w[imm8 & 7]); break;
    case 0xD7: { if (m.ismem) return -1; V*r = &c->x[m.rm]; u64 v = 0; for (int i = 0; i < 16; i++) v |= (u64)(r->b[i] >> 7) << i; setr(c, m.reg, 4, 0, v); break; }
    case 0xD6:
        if (pf == 3) { if (m.ismem) return -1; d->q[0] = c->mm[m.rm & 7]; d->q[1] = 0; break; }       /* movq2dq xmm, mm */
        if (pf == 2) { if (m.ismem) return -1; c->mm[m.reg & 7] = c->x[m.rm].q[0]; break; }         /* movdq2q mm, xmm */
        if (pf != 1) return -1;
        if (m.ismem) *(u64u*) m.ea = d->q[0]; else { c->x[m.rm].q[0] = d->q[0]; c->x[m.rm].q[1] = 0; }
        break;
    case 0x6E: if (pf != 1) return -1; d->q[0] = rdm(c, in, &m, in->w ? 8 : 4); d->q[1] = 0; break;
    case 0x7E:
        if (pf == 1) wrm(c, in, &m, in->w ? 8 : 4, d->q[0]);
        else if (pf == 3) { SRC(8); d->q[0] = s->q[0]; d->q[1] = 0; }
        else return -1;
        break;
    case 0x52: case 0x53:                                         /* rsqrtps/ss, rcpps/ss: computed exactly (hardware gives a 12-bit estimate) */
        if (pf == 0) { SRC(16); for (int i = 0; i < 4; i++) d->f[i] = op == 0x52 ? 1.0f / sqrtf(s->f[i]) : 1.0f / s->f[i]; }
        else if (pf == 3) { SRC(4); d->f[0] = op == 0x52 ? 1.0f / sqrtf(s->f[0]) : 1.0f / s->f[0]; }
        else return -1;
        break;
    case 0x7C: case 0x7D: {                                       /* SSE3 haddpd/hsubpd (66), haddps/hsubps (F2) */
        if (pf != 1 && pf != 2) return -1;
        SRC(16); V t = *d;
        if (pf == 2) { float a[4] = {t.f[0], t.f[2], s->f[0], s->f[2]}, b[4] = {t.f[1], t.f[3], s->f[1], s->f[3]};
                       for (int i = 0; i < 4; i++) d->f[i] = op == 0x7C ? a[i] + b[i] : a[i] - b[i]; }
        else { double a[2] = {t.df[0], s->df[0]}, b[2] = {t.df[1], s->df[1]};
               for (int i = 0; i < 2; i++) d->df[i] = op == 0x7C ? a[i] + b[i] : a[i] - b[i]; }
        break; }
    case 0xD0: {                                                  /* SSE3 addsubpd (66), addsubps (F2) */
        if (pf != 1 && pf != 2) return -1;
        SRC(16); V t = *d;
        if (pf == 2) { for (int i = 0; i < 4; i++) d->f[i] = (i & 1) ? t.f[i] + s->f[i] : t.f[i] - s->f[i]; }
        else { d->df[0] = t.df[0] - s->df[0]; d->df[1] = t.df[1] + s->df[1]; }
        break; }
    case 0xF0: if (pf != 2 || !m.ismem) return -1; SRC(16); *d = *s; break;       /* lddqu */
    case 0xF7: {                                                  /* maskmovdqu: byte-masked store to [rdi] */
        if (pf != 1 || m.ismem) return -1;
        V*mk = &c->x[m.rm]; u8*dst = (u8*)(c->r[RDI] + in->seg);
        for (int i = 0; i < 16; i++) if (mk->b[i] & 0x80) dst[i] = d->b[i];
        break; }
    case 0x77: break;
    default:
        if (pf != 1) return -1;
        SRC(16);
        pint(op, d, s, &ok);
        if (!ok) return -1;
    }
    return 0;
}


