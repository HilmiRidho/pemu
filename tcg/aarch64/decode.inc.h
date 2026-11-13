/* tcg/aarch64/decode.inc.h - JIT static decoder. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ decoder (static, no guest state) */
enum { OT_NONE, OT_REG, OT_MEM, OT_IMM };
enum { JO_FB, JO_FBT, JO_NOP, JO_MOV, JO_MOVX, JO_LEA, JO_PUSH, JO_POP, JO_ALU, JO_NOT, JO_NEG, JO_SHIFT, JO_IMUL,
       JO_SETCC, JO_CMOV, JO_CWDE, JO_CDQ, JO_LEAVE, JO_XLD, JO_XST, JO_XLOG, JO_XMOVDLD, JO_XMOVDST, JO_XVEC, JO_PMOVMSKB, JO_XSHI, JO_FMOV, JO_FARITH, JO_CVTSI2F, JO_FCVTF, JO_FCMP, JO_DIV, JO_XDUP, JO_BSF, JO_MUL1, JO_BSWAP, JO_XSHN, JO_XSHUF, JO_MMSHUF, JO_XCHG, JO_XCHGA, JO_CMPXCHG, JO_XADD, JO_XMOVQ, JO_BT, JO_XRR, JO_DF, JO_XLH, JO_POPCNT, JO_PSHUFB, JO_XPALIGN, JO_PTEST, JO_XABS, JO_PMOVX, JO_CVTTF2SI, JO_JMP, JO_JCC, JO_CALL, JO_RET, JO_JMPI, JO_CALLI };
typedef struct {
    u64 ip, imm, target; s64 disp;
    u8 len, op, sz, sz2, t, fr, fw, at, bt, ar, br, rex, lva, lvb, scale, seg, sx, cnt, nost, incdec, mmx, lk;
    u8 dt, dn, fuse;                      /* dt / dn: the flags are dead at the taken / fall-through exit (see jflags_dead); fuse: flag producer whose only reader is the next jcc (j_alu_fused) */
    s8 base, index;
} JI;
typedef struct { int reg, rm, ismem; } JM;

static inline s32 rd32s(const u8*p) { s32 v; memcpy(&v, p, 4); return v; }
static inline s16 rd16s(const u8*p) { s16 v; memcpy(&v, p, 2); return v; }
/* JI.cnt on jcc/call/calli/nop: the superblock continues after this instruction (jcc: on the fall-through path, side exit when taken;
 * call/calli: after the callee returns, through the return stack buffer; nop: a followed backward jmp that must poll for signals) */
static inline int jterm(const JI*j) {
    if ((j->op == JO_JCC || j->op == JO_CALL || j->op == JO_CALLI) && j->cnt) return 0;
    return j->op >= JO_JMP || j->op == JO_FBT;
}

static void jmodrm(const u8 **pp, JI *j, JM *m, int immbytes) {      /* same semantics as modrm() but static */
    const u8 *p = *pp; u8 b = *p++; int rex = j->rex, ripr = 0, mod = b >> 6;
    m->reg = ((b >> 3) & 7) | ((rex & 4) << 1); m->rm = (b & 7) | ((rex & 1) << 3); m->ismem = mod != 3;
    j->base = j->index = -1; j->scale = 0; j->disp = 0;
    if (mod == 3) { *pp = p; return; }
    if ((b & 7) == 4) {
        u8 sib = *p++; int sc = sib >> 6, idx = ((sib >> 3) & 7) | ((rex & 2) << 2), base = (sib & 7) | ((rex & 1) << 3);
        if (idx != 4) { j->index = (s8) idx; j->scale = (u8) sc; }
        if ((sib & 7) == 5 && mod == 0) { j->disp = rd32s(p); p += 4; } else j->base = (s8) base;
    } else if ((b & 7) == 5 && mod == 0) { j->disp = rd32s(p); p += 4; ripr = 1; }
    else j->base = (s8) m->rm;
    if (mod == 1) { j->disp += (s8) *p; p += 1; } else if (mod == 2) { j->disp += rd32s(p); p += 4; }
    if (ripr) j->disp += (s64)(u64)(p + immbytes);                   /* rip-relative: absolute address */
    *pp = p;
}
static const u8 *jmodrm_skip(const u8 *p) {
    u8 b = *p++; int mod = b >> 6;
    if (mod == 3) return p;
    if ((b & 7) == 4) { u8 sib = *p++; if ((sib & 7) == 5 && mod == 0) p += 4; }
    else if ((b & 7) == 5 && mod == 0) p += 4;
    if (mod == 1) p += 1; else if (mod == 2) p += 4;
    return p;
}
static int jsse_ok(u8 o) {
    return (o >= 0x10 && o <= 0x17) || (o >= 0x28 && o <= 0x2F) || (o >= 0x50 && o <= 0x76) || (o >= 0x78 && o <= 0x7F) ||
           (o >= 0xC2 && o <= 0xC6) || o >= 0xD0;
}
/* End of an instruction that the translator does not handle itself. p = opcode byte. *neutral: touches no flags,
 * *term: ends the block. Returns NULL when the length is not known (the block then ends before the instruction). */
static const u8 *jfb_end(const u8 *p, int p66, int rexw, int *neutral, int *term) {
    u8 op = *p++; int zi = p66 ? 2 : 4;
    *neutral = 0; *term = 0;
    if (op < 0x40 && (op & 7) < 6) return (op & 7) < 4 ? jmodrm_skip(p) : p + ((op & 7) == 4 ? 1 : zi);
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: case 0x98: case 0x99:
    case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xC9: case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD: return p;
    case 0x63: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8D: case 0x8F:
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
    case 0xFE: return jmodrm_skip(p);
    case 0xFF: { int ext = (*p >> 3) & 7; if (ext == 2 || ext == 4) *term = 1; return jmodrm_skip(p); }
    case 0x68: return p + 4;
    case 0x6A: return p + 1;
    case 0x69: return jmodrm_skip(p) + zi;
    case 0x6B: case 0x80: case 0x83: case 0xC0: case 0xC1: case 0xC6: return jmodrm_skip(p) + 1;
    case 0x81: case 0xC7: return jmodrm_skip(p) + zi;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0xE3: case 0xEB: *term = 1; return p + 1;
    case 0xA8: return p + 1;
    case 0xA9: return p + zi;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: return p + 1;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: return p + (rexw ? 8 : p66 ? 2 : 4);
    case 0xC2: *term = 1; return p + 2;
    case 0xC3: case 0xCC: case 0xCF: case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0xF4: case 0xFA: case 0xFB: *term = 1; return p;
    case 0xE8: case 0xE9: *term = 1; return p + 4;
    case 0xF6: case 0xF7: { int ext = (*p >> 3) & 7; const u8 *e = jmodrm_skip(p); if (ext < 2) e += op == 0xF6 ? 1 : zi; return e; }
    case 0x0F: {
        u8 o = *p++;
        switch (o) {
        case 0x05: case 0x0B: *term = 1; return p;
        case 0x31: case 0xA2: case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: return p;
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: *neutral = 1; return jmodrm_skip(p);
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: *term = 1; return p + 4;
        case 0x38: { u8 o3 = *p++; *neutral = !(o3 == 0x17 || o3 == 0xF6); return jmodrm_skip(p); }       /* SSSE3/SSE4/AES/crc32/movbe: all flag-neutral except ptest and adcx/adox */
        case 0x3A: { u8 o3 = *p++; *neutral = !(o3 >= 0x60 && o3 <= 0x63); return jmodrm_skip(p) + 1; }  /* palignr, pblendw, round*, pextr*, pinsr*, pclmulqdq: imm8; pcmp[ie]str[im] write the flags */
        case 0xBA: case 0xA4: case 0xAC: return jmodrm_skip(p) + 1;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xA5: case 0xAD: case 0xAE: case 0xAF: case 0xB0: case 0xB1:
        case 0xC0: case 0xC1: case 0xB6: case 0xB7: case 0xBE: case 0xBF: case 0xB8: case 0xBC: case 0xBD: return jmodrm_skip(p);
        default:
            if (!jsse_ok(o)) return NULL;
            *neutral = !(o == 0x2E || o == 0x2F);
            { const u8 *e = jmodrm_skip(p); if ((o >= 0x70 && o <= 0x73) || o == 0xC2 || o == 0xC4 || o == 0xC5 || o == 0xC6) e++; return e; }
        }
    }
    default: return NULL;
    }
}

/* 66 0F 38 xx that map onto NEON. Fills j->op / t / sz (/ sz2) and returns 1, or returns 0 when the instruction stays with the interpreter.
 * JO_XVEC: t, sz as q_v3 (3 umin, 4 umax, 5 smin, 6 smax, 2 cmeq, 7 cmgt, 13 mul; sz = lane size log2), t 16 = packus* (sz 0: bw, 1: dw).
 * JO_XABS: sz = lane size log2.  JO_PMOVX: t 0 zero / 1 sign extension, sz2 = source and sz = destination element bytes.  JO_PTEST: flags only. */
static int jsse38(JI *j, u8 o3, int ismem) {
    static const struct { u8 o, t, sz; } mm[] = {                       /* pmins* pminu* pmaxs* pmaxu*, pcmpeqq, pcmpgtq, pmulld */
        { 0x38, 5, 0 }, { 0x39, 5, 2 }, { 0x3A, 3, 1 }, { 0x3B, 3, 2 }, { 0x3C, 6, 0 }, { 0x3D, 6, 2 }, { 0x3E, 4, 1 }, { 0x3F, 4, 2 },
        { 0x29, 2, 3 }, { 0x37, 7, 3 }, { 0x40, 13, 2 } };
    if (o3 == 0x00) { j->op = JO_PSHUFB; return 1; }
    if (!(g_jopt & JOPT_SSE4)) return 0;
    for (unsigned i = 0; i < sizeof mm / sizeof mm[0]; i++)
        if (mm[i].o == o3) { j->op = JO_XVEC; j->t = mm[i].t; j->sz = mm[i].sz; return 1; }
    if (o3 == 0x2B) { j->op = JO_XVEC; j->t = 16; j->sz = 1; return 1; }                      /* packusdw */
    if (o3 >= 0x1C && o3 <= 0x1E) { j->op = JO_XABS; j->sz = (u8)(o3 - 0x1C); return 1; }    /* pabsb/w/d */
    if (o3 == 0x17) { j->op = JO_PTEST; j->fw = 1; return 1; }                                /* ptest: writes all six flags, reads none */
    if ((o3 >= 0x20 && o3 <= 0x25) || (o3 >= 0x30 && o3 <= 0x35)) {                          /* pmovsx* / pmovzx*: bw bd bq wd wq dq */
        static const u8 es[6] = { 1, 1, 1, 2, 2, 4 }, ds[6] = { 2, 4, 8, 4, 8, 8 };
        int k = o3 & 15;
        if (ismem && es[k] * (16 / ds[k]) == 2) return 0;                                      /* pmov?xbq m16: no 2-byte vector load here */
        j->op = JO_PMOVX; j->t = o3 < 0x30; j->sz2 = es[k]; j->sz = ds[k]; return 1;
    }
    return 0;
}

/* Decode one instruction. Returns 0 if its length is unknown (the translator ends the block before it). */
static int jdecode(JI *j, u64 ip) {
    memset(j, 0, sizeof *j);
    const u8 *s = (const u8*) ip, *p = s;
    int p66 = 0, seg = 0, a32 = 0, lock = 0, pf3 = 0, pf2 = 0;
    for (int k = 0; k < 14; k++) {
        u8 b = *p;
        if (b == 0x66) p66 = 1; else if (b == 0xF3) pf3 = 1; else if (b == 0xF2) pf2 = 1; else if (b == 0x64) seg = 1; else if (b == 0x65) seg = 2;
        else if (b == 0x67) a32 = 1; else if (b == 0xF0) lock = 1; else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36) { } else break;
        p++;
    }
    int rex = 0; if ((*p & 0xF0) == 0x40) rex = *p++;
    int w = (rex >> 3) & 1, opsz = w ? 8 : p66 ? 2 : 4, rexB = (rex & 1) << 3;
    const u8 *op0 = p; u8 op = *p++;
    j->ip = ip; j->rex = (u8) rex; j->seg = (u8) seg;
#define FB() do { int nt_, tm_; const u8 *e_ = jfb_end(op0, p66, w, &nt_, &tm_); if (!e_ || e_ - s > 15) return 0; \
                  j->len = (u8)(e_ - s); j->op = tm_ ? JO_FBT : JO_FB; j->fr = j->fw = !nt_; return 1; } while (0)
#define BADM(m) (lock || (a32 && (m).ismem))
#define BADA(m) ((a32 && (m).ismem) || !(g_jopt & JOPT_ATOM))        /* like BADM, but a lock prefix is accepted (the caller checks it) */
#define SETRM(m, which) do { if ((which) == 0) { j->at = (m).ismem ? OT_MEM : OT_REG; j->ar = (u8)(m).rm; } else { j->bt = (m).ismem ? OT_MEM : OT_REG; j->br = (u8)(m).rm; } } while (0)
#define IMMZ(dst) do { if (opsz == 2) { dst = (u64)(s64) rd16s(p); p += 2; } else { dst = (u64)(s64) rd32s(p); p += 4; } } while (0)
    JM m;
    if (op < 0x40 && (op & 7) < 6) {
        int t = op >> 3, f = op & 7;
        int cy = t == 2 || t == 3;                                  /* adc / sbb: 32/64-bit only, and not locked */
        if (cy && (!(g_jopt & JOPT_ATOM) || lock || f == 0 || f == 2 || f == 4 || opsz == 2)) FB();
        j->op = JO_ALU; j->t = (u8) t; j->nost = t == 7; j->fw = 1; j->fr = (u8) cy;
        if (f < 4) {
            j->sz = (f & 1) ? (u8) opsz : 1;
            jmodrm(&p, j, &m, 0); if (BADA(m)) FB();
            if (lock) { if (!m.ismem || f >= 2 || t == 7) FB(); j->lk = 1; }
            if (f < 2) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
        } else {
            if (lock) FB();
            j->sz = f == 4 ? 1 : (u8) opsz; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM;
            if (j->sz == 1) j->imm = (u64)(s64)(s8) *p++; else IMMZ(j->imm);
        }
        j->len = (u8)(p - s); return 1;
    }
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        j->op = JO_PUSH; j->sz = 8; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        j->op = JO_POP; j->sz = 8; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); break;
    case 0x63:
        jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
        j->op = JO_MOVX; j->sz = (u8) opsz; j->sz2 = 4; j->sx = opsz == 8; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
    case 0x68: j->op = JO_PUSH; j->sz = 8; j->at = OT_IMM; j->imm = (u64)(s64) rd32s(p); p += 4; break;
    case 0x6A: j->op = JO_PUSH; j->sz = 8; j->at = OT_IMM; j->imm = (u64)(s64)(s8) *p++; break;
    case 0x69: case 0x6B:
        if (opsz == 2) FB();
        jmodrm(&p, j, &m, op == 0x69 ? 4 : 1); if (BADM(m)) FB();
        j->op = JO_IMUL; j->sz = (u8) opsz; j->sx = 1; j->at = OT_REG;      /* imul writes CF/OF only: the other flags pass through (live before = live after) */ j->ar = (u8) m.reg; SETRM(m, 1);
        if (op == 0x69) { j->imm = (u64)(s64) rd32s(p); p += 4; } else j->imm = (u64)(s64)(s8) *p++;
        break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        s8 rel = (s8) *p++; j->op = JO_JCC; j->t = op & 15; j->fr = 1; j->len = (u8)(p - s); j->target = ip + j->len + (u64)(s64) rel; return 1; }
    case 0x80: case 0x81: case 0x83: {
        j->sz = op == 0x80 ? 1 : (u8) opsz;
        jmodrm(&p, j, &m, op == 0x81 ? (opsz == 2 ? 2 : 4) : 1); if (BADA(m)) FB();
        int t = m.reg & 7;
        if (t == 2 || t == 3) { if (lock || j->sz < 4 || !(g_jopt & JOPT_ATOM)) FB(); }     /* adc / sbb: 32/64-bit, not locked */
        if (lock) { if (!m.ismem || t == 7) FB(); j->lk = 1; }
        j->op = JO_ALU; j->t = (u8) t; j->nost = t == 7; j->fw = 1; j->fr = (u8)(t == 2 || t == 3); SETRM(m, 0); j->bt = OT_IMM;
        if (op == 0x81) IMMZ(j->imm); else j->imm = (u64)(s64)(s8) *p++;
        break; }
    case 0x84: case 0x85:
        j->sz = (op & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
    case 0x88: case 0x89: case 0x8A: case 0x8B:
        j->sz = (op & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = JO_MOV;
        if (op < 0x8A) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
        break;
    case 0x8D:
        j->seg = 0; jmodrm(&p, j, &m, 0); if (BADM(m) || !m.ismem || opsz == 2) FB();
        j->op = JO_LEA; j->sz = (u8) opsz; j->at = OT_REG; j->ar = (u8) m.reg; break;
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        if (op == 0x90 && !rexB) { j->op = JO_NOP; break; }
        if (!(g_jopt & JOPT_ATOM) || lock) FB();
        j->op = JO_XCHGA; j->sz = (u8) opsz; j->ar = (u8)((op & 7) | rexB); break;                       /* xchg rax, r */
    case 0x86: case 0x87:                                                                                  /* xchg r, m: implicitly locked */
        j->sz = (op & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0);
        if (!m.ismem) {                                                                                    /* xchg r, r: no atomicity, no flags */
            if (lock || !(g_jopt & JOPT_MISC)) FB();
            j->op = JO_XRR; j->ar = (u8) m.rm; j->br = (u8) m.reg; break;
        }
        if (BADA(m)) FB();
        j->op = JO_XCHG; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
    case 0x98: if (opsz == 2) FB(); j->op = JO_CWDE; j->sz = (u8) opsz; break;
    case 0x99: if (opsz == 2) FB(); j->op = JO_CDQ; j->sz = (u8) opsz; break;
    case 0xA8: j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->sz = 1; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM; j->imm = (u8) *p++; break;
    case 0xA9: j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->sz = (u8) opsz; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM; IMMZ(j->imm); break;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        j->op = JO_MOV; j->sz = 1; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); j->bt = OT_IMM; j->imm = (u8) *p++; break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        j->op = JO_MOV; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); j->bt = OT_IMM;
        if (w) { memcpy(&j->imm, p, 8); p += 8; j->sz = 8; }
        else if (p66) { u16 v; memcpy(&v, p, 2); j->imm = v; p += 2; j->sz = 2; }
        else { u32 v; memcpy(&v, p, 4); j->imm = v; p += 4; j->sz = 4; }
        break;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int sz = (op & 1) ? opsz : 1;
        jmodrm(&p, j, &m, op <= 0xC1 ? 1 : 0); if (BADM(m)) FB();
        int t = m.reg & 7;
        int rot16 = sz < 4 && t < 2 && op <= 0xC1 && (g_jopt & JOPT_ATOM);                    /* rol/ror r/m8|16, imm8: translated when the flags are dead */
        if (t == 2 || t == 3 || (sz < 4 && !rot16 && (t < 4 || !(g_jopt & JOPT_INT)))) FB();
        j->op = JO_SHIFT; j->sz = (u8) sz; j->t = (u8) t; SETRM(m, 0);
        j->cnt = op <= 0xC1 ? 0 : op <= 0xD1 ? 1 : 2;
        if (j->cnt == 0) j->imm = (u8) *p++ & (sz == 8 ? 63 : 31); else if (j->cnt == 1) j->imm = 1;
        int nat8 = sz < 4 && t >= 4 && j->cnt != 2 && j->imm >= 1 && (int) j->imm < sz * 8 && (g_jopt & JOPT_SHFL);   /* 8/16-bit shl/shr/sar by 1..bits-1 keep their flags lazy too */
        if (j->cnt == 2 || !j->imm || t < 2 || (sz < 4 && !nat8)) {
            /* rol/ror, a count in cl, a zero count: the flags are only partly written or not at all, so the old flags pass through. Neither fr nor fw:
             * they are live before the instruction exactly when they are live after it (as inc/dec). With a memory operand fr is kept, so that the
             * flags are in memory before an access that can fault. The interpreter fallback sees the merged flags because they are live then. */
            if (j->at == OT_MEM) j->fr = 1;
        } else j->fw = 1;
        break; }
    case 0xC2: j->op = JO_RET; j->imm = (u16)(*p | p[1] << 8); p += 2; break;
    case 0xC3: j->op = JO_RET; break;
    case 0xC6: case 0xC7: {
        j->sz = (op & 1) ? (u8) opsz : 1;
        jmodrm(&p, j, &m, j->sz == 1 ? 1 : (opsz == 2 ? 2 : 4)); if (BADM(m) || (m.reg & 7)) FB();
        j->op = JO_MOV; SETRM(m, 0); j->bt = OT_IMM;
        if (j->sz == 1) j->imm = (u8) *p++; else IMMZ(j->imm);
        break; }
    case 0xC9: j->op = JO_LEAVE; break;
    case 0xFC: case 0xFD: if (!(g_jopt & JOPT_MISC) || lock || p66) FB(); j->op = JO_DF; j->t = op & 1; break;       /* cld / std: DF lives in c->rf, which lazy flags leave alone */
    case 0xE8: case 0xE9: {
        s32 rel = rd32s(p); p += 4; j->len = (u8)(p - s);
        j->op = op == 0xE8 ? JO_CALL : JO_JMP; j->target = ip + j->len + (u64)(s64) rel; j->imm = ip + j->len; return 1; }
    case 0xEB: { s8 rel = (s8) *p++; j->len = (u8)(p - s); j->op = JO_JMP; j->target = ip + j->len + (u64)(s64) rel; return 1; }
    case 0xF6: case 0xF7: {
        int ext = (*p >> 3) & 7, sz = (op & 1) ? opsz : 1;
        jmodrm(&p, j, &m, ext < 2 ? (sz == 1 ? 1 : (opsz == 2 ? 2 : 4)) : 0);
        if (BADM(m) || (ext > 3 && !(((ext >= 6 && (g_jopt & JOPT_DIV)) || (ext <= 5 && (g_jopt & JOPT_INT))) && (sz == 4 || sz == 8)))) FB();
        j->sz = (u8) sz; SETRM(m, 0);
        if (ext >= 6) { j->op = JO_DIV; j->t = (u8) ext; break; }
        if (ext >= 4) { j->op = JO_MUL1; j->t = (u8) ext; break; }       /* mul / imul r/m: rdx:rax = rax * r/m (translated only when the flags are dead; no fr/fw: the flags the interpreter leaves undefined must flow through unchanged) */
        if (ext < 2) { j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->bt = OT_IMM; if (sz == 1) j->imm = (u8) *p++; else IMMZ(j->imm); }
        else if (ext == 2) j->op = JO_NOT; else { j->op = JO_NEG; j->fw = 1; }
        break; }
    case 0xFE: case 0xFF: {
        int ext = (*p >> 3) & 7;
        if (ext < 2) {                                   /* inc / dec: add or sub 1 that leaves CF alone (JO_ALU; with live flags see the incdec case there) */
            jmodrm(&p, j, &m, 0); if (BADA(m)) FB();
            if (lock) { if (!m.ismem) FB(); j->lk = 1; }
            j->op = JO_ALU; j->t = ext ? 5 : 0; j->sz = op == 0xFE ? 1 : (u8) opsz; SETRM(m, 0);
            j->bt = OT_IMM; j->imm = 1; j->incdec = 1; j->len = (u8)(p - s); return 1;   /* no fr/fw: CF passes through, so the flags are live before it exactly when they are live after it */
        }
        if (op == 0xFF && ext == 6 && (g_jopt & JOPT_ATOM) && !p66) {                   /* push r/m64 */
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_PUSH; j->sz = 8; SETRM(m, 0); j->len = (u8)(p - s); return 1;
        }
        if (op == 0xFE || (ext != 2 && ext != 4)) FB();
        jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = ext == 2 ? JO_CALLI : JO_JMPI; j->sz = 8; SETRM(m, 0); j->len = (u8)(p - s); j->imm = ip + j->len; return 1; }
    case 0x0F: {
        u8 o2 = *p++;
        switch (o2) {
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            p = jmodrm_skip(p); j->op = JO_NOP; break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
            j->op = JO_CMOV; j->t = o2 & 15; j->sz = (u8) opsz; j->fr = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
            s32 rel = rd32s(p); p += 4; j->len = (u8)(p - s); j->op = JO_JCC; j->t = o2 & 15; j->fr = 1; j->target = ip + j->len + (u64)(s64) rel; return 1; }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_SETCC; j->t = o2 & 15; j->sz = 1; j->fr = 1; SETRM(m, 0); break;
        case 0xAF:
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
            j->op = JO_IMUL; j->sz = (u8) opsz; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;      /* flags pass through, see 69/6B */
        case 0xB6: case 0xB7: case 0xBE: case 0xBF:
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();                            /* opsz 2 (66 0f b6/be): the 16-bit merge is done by g_st */
            j->op = JO_MOVX; j->sz = (u8) opsz; j->sz2 = (o2 & 1) ? 2 : 1; j->sx = o2 >= 0xBE; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x10: case 0x28: case 0x6F: case 0x11: case 0x29: case 0x7F: {
            int pf = pf3 ? 3 : pf2 ? 2 : p66 ? 1 : 0, st = o2 == 0x11 || o2 == 0x29 || o2 == 0x7F;
            if ((o2 == 0x10 || o2 == 0x11) && pf > 1 && (g_jopt & JOPT_FP) && !(pf2 && pf3)) {      /* movss / movsd */
                jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
                j->op = JO_FMOV; j->sz2 = pf2 ? 8 : 4; j->t = o2 == 0x11;
                if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
                break;
            }
            int mm = (o2 == 0x6F || o2 == 0x7F) && pf == 0;                       /* movq mm, mm/m64 and its store form */
            if ((o2 == 0x10 || o2 == 0x11) ? pf > 1 : (o2 == 0x6F || o2 == 0x7F) ? (pf == 0 && !mm) : 0) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = st ? JO_XST : JO_XLD; j->mmx = (u8) mm;
            if (mm) { m.reg &= 7; if (!m.ismem) m.rm &= 7; }
            if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            break; }
        case 0x51: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F:             /* sqrt/add/mul/sub/min/div/max ss|sd */
            if (!(g_jopt & JOPT_FP) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_FARITH; j->sz2 = pf2 ? 8 : 4;
            j->t = o2 == 0x58 ? 0 : o2 == 0x59 ? 1 : o2 == 0x5C ? 2 : o2 == 0x5E ? 3 : o2 == 0x5D ? 5 : o2 == 0x5F ? 6 : 4;     /* 5 min, 6 max */
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x5A:                                           /* cvtss2sd (F3) / cvtsd2ss (F2): scalar precision change, the rest of the destination is kept */
            if (!(g_jopt & JOPT_FP) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_FCVTF; j->sz2 = pf2 ? 8 : 4;           /* source element size */
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x2A:                                           /* cvtsi2ss / cvtsi2sd */
            if (!(g_jopt & JOPT_FP) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_CVTSI2F; j->sz2 = pf2 ? 8 : 4; j->sz = w ? 8 : 4; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x2C:                                           /* cvttss2si / cvttsd2si r32|r64, xmm/m32|m64 (the helper is the interpreter's cvt_i: NaN and out of range give the "integer indefinite") */
            if (!(g_jopt & JOPT_ARITH) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_CVTTF2SI; j->sz2 = pf2 ? 8 : 4; j->sz = w ? 8 : 4; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x2E: case 0x2F:                                /* ucomiss/ucomisd/comiss/comisd */
            if (!(g_jopt & JOPT_FCMP) || pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_FCMP; j->sz2 = p66 ? 8 : 4; j->fw = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xEF: case 0xEB: case 0xDB: case 0xDF:
            if (pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->mmx = !p66; if (j->mmx) { m.reg &= 7; if (!m.ismem) m.rm &= 7; }
            j->op = JO_XLOG; j->t = o2 == 0xEF ? 0 : o2 == 0xEB ? 1 : o2 == 0xDB ? 2 : 3;
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x54: case 0x55: case 0x56: case 0x57:          /* andps/andnps/orps/xorps and the pd forms: the same bit operations as pand/pandn/por/pxor */
            if (pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_XLOG; j->t = o2 == 0x57 ? 0 : o2 == 0x56 ? 1 : o2 == 0x54 ? 2 : 3;
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xFC: case 0xFD: case 0xFE: case 0xD4: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0x74: case 0x75: case 0x76:
            if (pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->mmx = !p66; if (j->mmx) { m.reg &= 7; if (!m.ismem) m.rm &= 7; }
            j->op = JO_XVEC;
            j->t = (o2 >= 0xF8 && o2 <= 0xFB) ? 1 : (o2 >= 0xFC || o2 == 0xD4) ? 0 : 2;   /* sub / add / cmpeq */
            j->sz = (u8)(o2 == 0xD4 || o2 == 0xFB ? 3 : o2 == 0xFE || o2 == 0xFA || o2 == 0x76 ? 2 : o2 == 0xFD || o2 == 0xF9 || o2 == 0x75 ? 1 : 0);
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xD5: case 0xD8: case 0xD9: case 0xDA: case 0xDC: case 0xDD: case 0xDE: case 0xE0: case 0xE3: case 0xE8: case 0xE9: case 0xEA: case 0xEC: case 0xED: case 0xEE:
        case 0x60: case 0x61: case 0x62: case 0x64: case 0x65: case 0x66: case 0x68: case 0x69: case 0x6A: case 0x6C: case 0x6D: {
            if (pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->mmx = !p66; if (j->mmx) { m.reg &= 7; if (!m.ismem) m.rm &= 7; }
            int t, sz;                                       /* NEON kind (see q_v3) and lane size */
            switch (o2) {
            case 0xD5: t = 13; sz = 1; break;                                                                         /* pmullw */
            case 0xD8: case 0xD9: t = 10; sz = o2 - 0xD8; break;                                                      /* psubusb/w */
            case 0xDC: case 0xDD: t = 8; sz = o2 - 0xDC; break;                                                       /* paddusb/w */
            case 0xE8: case 0xE9: t = 11; sz = o2 - 0xE8; break;                                                      /* psubsb/w */
            case 0xEC: case 0xED: t = 9; sz = o2 - 0xEC; break;                                                       /* paddsb/w */
            case 0xDA: t = 3; sz = 0; break;                                                                          /* pminub */
            case 0xDE: t = 4; sz = 0; break;                                                                          /* pmaxub */
            case 0xEA: t = 5; sz = 1; break;                                                                          /* pminsw */
            case 0xEE: t = 6; sz = 1; break;                                                                          /* pmaxsw */
            case 0xE0: t = 12; sz = 0; break;                                                                         /* pavgb */
            case 0xE3: t = 12; sz = 1; break;                                                                         /* pavgw */
            case 0x64: case 0x65: case 0x66: t = 7; sz = o2 - 0x64; break;                                            /* pcmpgtb/w/d */
            case 0x60: case 0x61: case 0x62: t = 14; sz = o2 - 0x60; break;                                           /* punpcklbw/wd/dq */
            case 0x6C: t = 14; sz = 3; break;                                                                         /* punpcklqdq */
            case 0x68: case 0x69: case 0x6A: t = 15; sz = o2 - 0x68; break;                                           /* punpckhbw/wd/dq */
            default: t = 15; sz = 3; break;                                                                           /* punpckhqdq */
            }
            if (j->mmx && (t == 14 || t == 15)) FB();                                 /* MMX punpck* work on 32 bits: not a plain zip of 64 */
            j->op = JO_XVEC; j->t = (u8) t; j->sz = (u8) sz; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
        case 0x77: if (p66 || pf3 || pf2) FB(); j->op = JO_NOP; break;      /* emms: the emulator keeps MMX apart from x87, nothing to do */
        case 0x70: {                                         /* pshufd that copies one dword to all four lanes: strchr/memchr broadcast the byte this way */
            if (!p66 && !pf3 && !pf2) {                      /* pshufw mm, mm/m64, imm8 */
                jmodrm(&p, j, &m, 1); if (BADM(m)) FB();
                m.reg &= 7; if (!m.ismem) m.rm &= 7;
                j->op = JO_MMSHUF; j->imm = *p++; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
            }
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 1); if (BADM(m)) FB();
            u8 im = *p++;
            if (im != 0x00 && im != 0x55 && im != 0xAA && im != 0xFF) { j->op = JO_XSHUF; j->imm = im; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
            j->op = JO_XDUP; j->imm = im & 3; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
            if (!(g_jopt & JOPT_INT) || opsz == 2 || pf3 || pf2 || lock) FB();
            j->op = JO_BSWAP; j->sz = (u8) opsz; j->at = OT_REG; j->ar = (u8)((o2 & 7) | rexB); break;
        case 0xBC: case 0xBD:                                /* bsf/bsr, with F3: tzcnt/lzcnt (translated only when the flags are dead) */
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2 || pf2) FB();
            j->op = JO_BSF; j->sz = (u8) opsz; j->t = (u8)(o2 & 1); j->sz2 = (u8) pf3; j->fw = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xA3: case 0xAB: case 0xB3: case 0xBB:          /* bt/bts/btr/btc r/m, r (a lock prefix: interpreter) */
            if (!(g_jopt & JOPT_MISC) || pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_BT; j->sz = (u8) opsz; j->t = (u8)(o2 == 0xA3 ? 0 : o2 == 0xAB ? 1 : o2 == 0xB3 ? 2 : 3); j->fr = 1;   /* fr: the flags other than CF are kept */
            SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
        case 0xBA: {                                         /* bt/bts/btr/btc r/m, imm8 */
            if (!(g_jopt & JOPT_MISC) || pf3 || pf2) FB();
            jmodrm(&p, j, &m, 1); if (BADM(m) || (m.reg & 7) < 4) FB();
            j->op = JO_BT; j->sz = (u8) opsz; j->t = (u8)((m.reg & 7) - 4); j->fr = 1; SETRM(m, 0); j->bt = OT_IMM; j->imm = *p++; break; }
        case 0xB8:                                           /* popcnt (F3 0F B8) */
            if (!(g_jopt & JOPT_MISC) || !pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_POPCNT; j->sz = (u8) opsz; j->fw = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x12: case 0x13: case 0x16: case 0x17: {        /* movlps/movlpd/movhps/movhpd, movhlps/movlhps */
            if (!(g_jopt & JOPT_MISC) || pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            int st = o2 & 1;
            j->op = JO_XLH;
            if (m.ismem) {
                j->t = (u8)((o2 >= 0x16 ? 1 : 0) + (st ? 2 : 0));                       /* 0 load low, 1 load high, 2 store low, 3 store high */
                if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            } else {
                if (p66 || st) FB();
                j->t = o2 == 0x12 ? 4 : 5;                                                  /* movhlps / movlhps */
                j->at = OT_REG; j->ar = (u8) m.reg; j->bt = OT_REG; j->br = (u8) m.rm;
            }
            break; }
        case 0x38: {                                         /* pshufb and the SSE4 integer ops of jsse38; the other 0F 38 instructions go through jfb_end */
            if (!(g_jopt & JOPT_MISC) || !p66 || pf3 || pf2) FB();
            u8 o3 = *p++; jmodrm(&p, j, &m, 0); if (BADM(m) || !jsse38(j, o3, m.ismem)) FB();
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
        case 0x3A: {                                         /* palignr xmm, xmm/m128, imm8 (the other 0F 3A instructions stay with the interpreter) */
            if (!(g_jopt & JOPT_MISC) || !p66 || pf3 || pf2 || *p != 0x0F) FB();
            p++; jmodrm(&p, j, &m, 1); if (BADM(m)) FB();
            j->op = JO_XPALIGN; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); j->imm = *p++; break; }
        case 0xD7:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (m.ismem || lock) FB();
            j->op = JO_PMOVMSKB; j->at = OT_REG; j->ar = (u8) m.reg; j->bt = OT_REG; j->br = (u8) m.rm; break;
        case 0x71: case 0x72: {                              /* psrlw/psraw/psllw (71) and psrld/psrad/pslld (72) xmm, imm8 */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 1); if (m.ismem || lock) FB();
            int ext = m.reg & 7; if (ext != 2 && ext != 4 && ext != 6) FB();
            j->op = JO_XSHN; j->t = (u8) ext; j->sz = (u8)(o2 == 0x71 ? 16 : 32); j->imm = *p++; j->br = (u8) m.rm; break; }
        case 0x2B: case 0xE7:                                /* movntps/movntpd/movntdq: a plain 16-byte store */
            if (pf3 || pf2 || (o2 == 0xE7 && !p66)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m) || !m.ismem) FB();
            j->op = JO_XST; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
        case 0xD6:                                           /* movq xmm/m64, xmm: the store form */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m) || !m.ismem) FB();
            j->op = JO_XMOVDST; j->sz = 8; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
        case 0x67:                                           /* packuswb */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_XVEC; j->t = 16; j->sz = 0; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x73: {                                         /* psrlq/psllq/psrldq/pslldq xmm, imm8 */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 1); if (m.ismem || lock) FB();
            int ext = m.reg & 7; if (ext != 2 && ext != 3 && ext != 6 && ext != 7) FB();
            j->op = JO_XSHI; j->t = (u8) ext; j->imm = *p++; j->br = (u8) m.rm; break; }
        case 0xB0: case 0xB1: case 0xC0: case 0xC1:                                      /* cmpxchg / xadd r/m, r (memory form: LL/SC loop) */
            if (pf3 || pf2) FB();
            j->sz = (o2 & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0); if (BADA(m) || !m.ismem) FB();
            j->op = o2 < 0xC0 ? JO_CMPXCHG : JO_XADD; j->lk = (u8) lock; j->fw = 1; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
        case 0x6E: case 0x7E:
            if (o2 == 0x7E && pf3 && !pf2 && !p66 && (g_jopt & JOPT_ATOM)) {            /* movq xmm, xmm/m64 */
                jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
                j->op = JO_XMOVQ; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
            }
            if (pf3 || pf2 || (o2 == 0x7E && pf3)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->mmx = !p66; if (j->mmx) m.reg &= 7;
            j->sz = w ? 8 : 4;
            if (o2 == 0x6E) { j->op = JO_XMOVDLD; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            else { j->op = JO_XMOVDST; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; }
            break;
        default: FB();
        }
        break; }
    default: FB();
    }
    j->len = (u8)(p - s);
    return 1;
}

