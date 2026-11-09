/* tcg/aarch64/emit.inc.h - AArch64 encoders. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ AArch64 encoders */
static inline void E(u32 w) { *jcur++ = w; }
#define SFB(sf) ((sf) ? 0x80000000u : 0u)

static void a_movz(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x52800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movk(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x72800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movn(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x12800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movi(int rd, u64 v) {                       /* any 64-bit constant, 1-4 instructions, never touches the flags */
    int nz = 0, nzn = 0, first = 1;
    for (int h = 0; h < 4; h++) { nz += ((v >> (16 * h)) & 0xffff) != 0; nzn += ((~v >> (16 * h)) & 0xffff) != 0; }
    if (nzn < nz) {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (ch == 0xffff) continue;
            if (first) { a_movn(1, rd, ~ch & 0xffff, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movn(1, rd, 0, 0);
    } else {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (!ch) continue;
            if (first) { a_movz(1, rd, ch, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movz(1, rd, 0, 0);
    }
}
static void a_addsub(int sf, int sub, int s, int rd, int rn, int rm) {
    E(SFB(sf) | (sub ? 0x40000000u : 0) | (s ? 0x20000000u : 0) | 0x0B000000u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_addsh(int rd, int rn, int rm, int lsl) {      /* add xd, xn, xm, lsl #n */
    E(0x8B000000u | (u32) rm << 16 | (u32) lsl << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_addi(int sub, int rd, int rn, u32 imm12, int sh12) {    /* x-form add/sub immediate (rn, rd may be sp) */
    E(0x91000000u | (sub ? 0x40000000u : 0) | (u32) sh12 << 22 | (imm12 & 0xfff) << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_log(int sf, int opc, int n, int rd, int rn, int rm) {    /* opc: 0 and, 1 orr, 2 eor, 3 ands; n: invert rm */
    E(SFB(sf) | (u32) opc << 29 | 0x0A000000u | (u32) n << 21 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_logsh(int opc, int rd, int rn, int rm, int type, int amt) {   /* x-form with shifted rm */
    E(0x80000000u | (u32) opc << 29 | 0x0A000000u | (u32) type << 22 | (u32) rm << 16 | (u32) amt << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_mov(int rd, int rm) { a_log(1, 1, 0, rd, 31, rm); }
static void a_movw(int rd, int rm) { a_log(0, 1, 0, rd, 31, rm); }      /* 32-bit move: zero-extends */
static void a_andmask(int rd, int rn, int nbits) { E(0x92400000u | (u32)(nbits - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* and xd, xn, #(2^nbits - 1) */
static void a_shv(int sf, int op, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC02000u | (u32) op << 10 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* 0 lsl 1 lsr 2 asr 3 ror */
static void a_bfm(int sf, int sbfm, int rd, int rn, int immr, int imms) {
    E(SFB(sf) | (sbfm ? 0x13000000u : 0x53000000u) | (sf ? 0x400000u : 0) | (u32) immr << 16 | (u32) imms << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_bfi(int rd, int rn, int lsb, int width) { E(0xB3400000u | (u32)((64 - lsb) & 63) << 16 | (u32)(width - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* bfi xd, xn, #lsb, #width */
static void a_lsl(int sf, int rd, int rn, int n) { int b = sf ? 64 : 32; a_bfm(sf, 0, rd, rn, (b - n) & (b - 1), b - 1 - n); }
static void a_lsr(int sf, int rd, int rn, int n) { a_bfm(sf, 0, rd, rn, n, sf ? 63 : 31); }
static void a_asr(int sf, int rd, int rn, int n) { a_bfm(sf, 1, rd, rn, n, sf ? 63 : 31); }
static void a_sxth(int sf, int rd, int rn) { a_bfm(sf, 1, rd, rn, 0, 15); }
static void a_sxtw(int rd, int rn) { a_bfm(1, 1, rd, rn, 0, 31); }
static void a_mul(int sf, int rd, int rn, int rm) { E(SFB(sf) | 0x1B007C00u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }
static void a_csel(int sf, int cc, int rd, int rn, int rm) { E(SFB(sf) | 0x1A800000u | (u32) rm << 16 | (u32) cc << 12 | (u32) rn << 5 | (u32) rd); }
static void a_mulh(int sgn, int rd, int rn, int rm) { E((sgn ? 0x9B407C00u : 0x9BC07C00u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* smulh / umulh */
static void a_rev(int sf, int rd, int rn) { E((sf ? 0xDAC00C00u : 0x5AC00800u) | (u32) rn << 5 | (u32) rd); }   /* rev: byte swap */
static void a_rbit(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC00000u | (u32) rn << 5 | (u32) rd); }   /* rbit */
static void a_clz(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC01000u | (u32) rn << 5 | (u32) rd); }    /* clz */
static void a_cset(int rd, int cc) { E(0x1A9F07E0u | (u32)(cc ^ 1) << 12 | (u32) rd); }              /* cset wd, cc */
/* loads and stores: size 0 b, 1 h, 2 w, 3 x;  opc 0 store, 1 load, 2 load sign-extended to x, 3 load sign-extended to w */
static void a_ldst_u(int size, int opc, int rt, int rn, u32 off) { E((u32) size << 30 | 0x39000000u | (u32) opc << 22 | ((off >> size) & 0xfff) << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldst_s(int size, int opc, int rt, int rn, int off) { E((u32) size << 30 | 0x38000000u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt); }
static void a_stp(int rt, int rt2, int rn, int off) { E(0xA9000000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldp(int rt, int rt2, int rn, int off) { E(0xA9400000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
static void a_b(u32 *t) { E(0x14000000u | ((u32)(t - jcur) & 0x3ffffff)); }
static u32 *a_bcond_fwd(int cc) { E(0x54000000u | (u32) cc); return jcur - 1; }        /* patched with jpatch19 */
static u32 *a_cbz_fwd(int nz, int rt) { E((nz ? 0xB5000000u : 0xB4000000u) | (u32) rt); return jcur - 1; }
static void jpatch19(u32 *at) { *at |= ((u32)(jcur - at) & 0x7ffff) << 5; }
static void a_adr_at(u32 *at, int rd, u32 *target) { s64 d = (s64)((u8*) target - (u8*) at); *at = 0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd; }
static void a_bl(u32 *t) { E(0x94000000u | ((u32)(t - jcur) & 0x3ffffff)); }
static void a_ret(void) { E(0xD65F03C0u); }
static void a_div(int sf, int sgn, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC00800u | (sgn ? 0x400u : 0u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }       /* udiv / sdiv */
static void a_msub(int sf, int rd, int rn, int rm, int ra) { E(SFB(sf) | 0x1B008000u | (u32) rm << 16 | (u32) ra << 10 | (u32) rn << 5 | (u32) rd); }     /* rd = ra - rn * rm */
static u32 *a_b_fwd(void) { E(0x14000000u); return jcur - 1; }
static void jpatch26(u32 *at) { *at |= (u32)(jcur - at) & 0x3ffffff; }
static void f_cmp(int sz, int vn, int vm) { E((sz == 8 ? 0x1E602000u : 0x1E202000u) | (u32) vm << 16 | (u32) vn << 5); }          /* fcmp dn, dm */
static void f_tox(int sz, int rd, int vn) { E((sz == 8 ? 0x9E660000u : 0x1E260000u) | (u32) vn << 5 | (u32) rd); }                  /* fmov xd, dn / fmov wd, sn */
static void a_br(int rn) { E(0xD61F0000u | (u32) rn << 5); }
static void a_adr(int rd, u32 *target) { s64 d = (s64)((u8*) target - (u8*) jcur); E(0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd); }
static void j_callabs(void *f) {                                /* call a C helper; x0..x5 = arguments */
    a_movi(16, (u64) f);
#ifdef PEMU_JIT_EMBED
    E(0xD4000001u | 0x7001u << 5);                              /* svc #0x7001: host call (see pemu.c) */
#else
    E(0xD63F0000u | 16u << 5);                                  /* blr x16 */
#endif
}
static void j_sync_icache(void *a, void *b) {
#ifndef PEMU_JIT_EMBED
    __builtin___clear_cache((char*) a, (char*) b);
#else
    (void) a; (void) b;
#endif
}

