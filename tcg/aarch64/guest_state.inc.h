/* tcg/aarch64/guest_state.inc.h - JIT guest state access. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ guest state access */
static void c_ld(int rt, u32 off) { a_ldst_u(3, 1, rt, RCPU, off); }
static void c_st(int rt, u32 off) { a_ldst_u(3, 0, rt, RCPU, off); }
static void g_ld(int rt, int n, int sz, int rex) {              /* guest register n, zero-extended, into rt */
    int hb = (sz == 1 && !rex && n >= 4 && n < 8) ? pin(n - 4) : -1, h = pin(n);
    if (hb >= 0) { a_bfm(1, 0, rt, hb, 8, 15); return; }          /* ah, ch, dh, bh */
    if (h >= 0 && !(sz == 1 && !rex && n >= 4 && n < 8)) {
        switch (sz) { case 8: a_mov(rt, h); break; case 4: a_movw(rt, h); break; case 2: a_andmask(rt, h, 16); break; default: a_andmask(rt, h, 8); }
        return;
    }
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: a_ldst_u(3, 1, rt, RCPU, off); break;
    case 4: a_ldst_u(2, 1, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 1, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 1, rt, RCPU, off);
    }
}
static void g_st(int rt, int n, int sz, int rex) {              /* sz 4 and 8 write the whole register: rt must hold a zero-extended value */
    int hb = (sz == 1 && !rex && n >= 4 && n < 8) ? pin(n - 4) : -1, h = pin(n);
    if (hb >= 0) { a_bfi(hb, rt, 8, 8); return; }
    if (h >= 0 && !(sz == 1 && !rex && n >= 4 && n < 8)) {
        switch (sz) { case 8: case 4: if (rt != h) a_mov(h, rt); break; case 2: a_bfi(h, rt, 0, 16); break; default: a_bfi(h, rt, 0, 8); }
        return;
    }
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: case 4: a_ldst_u(3, 0, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 0, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 0, rt, RCPU, off);
    }
}
static void g_ldx(int rt, int n, int sz2, int sx, int dsz, int rex) {   /* low sz2 bytes of guest register n, zero- or sign-extended (to dsz) into rt */
    int lo = 0, rn = n;
    if (sz2 == 1 && !rex && n >= 4 && n < 8) { rn = n - 4; lo = 8; }
    int h = pin(rn);
    if (h < 0) {
        int size = sz2 == 1 ? 0 : sz2 == 2 ? 1 : 2;
        a_ldst_u(size, !sx ? 1 : (dsz == 8 ? 2 : 3), rt, RCPU, OFF_R(rn) + (lo ? 1u : 0u)); return;
    }
    a_bfm(sx && dsz == 8, sx, rt, h, lo, lo + sz2 * 8 - 1);
}
static void j_pin_spill(void) { if (g_jpin) { a_stp(19, 20, RCPU, (int) OFF_R(0)); a_stp(21, 22, RCPU, (int) OFF_R(2)); a_stp(23, 24, RCPU, (int) OFF_R(4)); a_stp(25, 29, RCPU, (int) OFF_R(6)); } }
static void j_pin_fill(void)  { if (g_jpin) { a_ldp(19, 20, RCPU, (int) OFF_R(0)); a_ldp(21, 22, RCPU, (int) OFF_R(2)); a_ldp(23, 24, RCPU, (int) OFF_R(4)); a_ldp(25, 29, RCPU, (int) OFF_R(6)); } }
/* x86 memory ordering (TSO) for guests with more than one thread (g_jmt): every load is followed by dmb ishld and every
 * store is preceded by dmb ishst. ishld orders the load before all later loads AND stores (load->load, load->store);
 * ishst orders all earlier stores before the store (store->store). Only store->load may be reordered, as on x86. A full
 * dmb ish before each store is not needed: it would only add the load->store order that ishld already gives, and it is
 * much more expensive. Every translated load goes through a_dmb_ld, so no load escapes the argument.
 * (ldar/stlr would be cheaper still, but they fault on unaligned addresses, which x86 guests use freely.)
 * Single-threaded guests pay nothing. */
static void a_dmb_ld(void) { if (g_jmt) E(0xD50339BFu); }               /* dmb ishld */
static void a_dmb_st(void) { if (g_jmt) E(0xD5033ABFu); }               /* dmb ishst */
/* FEAT_LRCPC2 forms (HWCAP_ILRCPC): ldapur = load-acquire(PC), stlur = store-release, both with the unscaled imm9 of ldur/stur.
 * An acquire load orders itself before all later loads and stores, a release store orders all earlier loads and stores before
 * itself, and a release store followed by an acquire(PC) load may still be reordered: that is exactly x86 TSO, so they replace
 * "ldur + dmb ishld" and "dmb ishst + stur" for integer accesses. They need NATURAL alignment (with FEAT_LSE2: no 16-byte
 * boundary crossing) and fault otherwise, and x86 guests use unaligned addresses freely, so every 2/4/8-byte access is guarded:
 * the low address bits are tested (and + cbnz: no host flags are touched, the JIT keeps them live) and a misaligned address takes
 * the old dmb sequence. 1-byte accesses are always aligned and need no guard. x17 is the scratch (it is only ever used inside
 * fl_spill / j_adc, never across an access); both operands must not be x17. SSE/FP accesses keep the dmb pair: LRCPC2 has no
 * vector forms. */
static void a_rc_s(int size, int opc, int rt, int rn, int off) {          /* opc as a_ldst_s: 0 stlur, 1 ldapur, 2 ldapurs* to x, 3 ldapurs* to w */
    E((u32) size << 30 | 0x19000000u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt);
}
static u32 *j_align_guard(int sz, int rn, int off) {                      /* returns the cbnz to the slow path (taken when [rn + off] is not sz-aligned) */
    int nb = sz == 2 ? 1 : sz == 4 ? 2 : 3;
    if (off % sz == 0) a_andmask(17, rn, nb);
    else { a_addi(off < 0, 17, rn, (u32)(off < 0 ? -off : off), 0); a_andmask(17, 17, nb); }
    return a_cbz_fwd(1, 17);
}
static inline int j_rc_ok(int rt, int rn) { return g_jmt && g_jrcpc && rt != 17 && rn != 17; }
static void m_ld(int sz, int sgn, int sf, int rt, int rn, int off) {      /* guest memory, [rn + off], off in -256..255 */
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3, opc = !sgn ? 1 : (sf ? 2 : 3);
    if (j_rc_ok(rt, rn)) {
        if (sz == 1) { a_rc_s(size, opc, rt, rn, off); return; }
        u32 *slow = j_align_guard(sz, rn, off);
        a_rc_s(size, opc, rt, rn, off);
        u32 *end = a_b_fwd();
        jpatch19(slow);
        a_ldst_s(size, opc, rt, rn, off); a_dmb_ld();
        jpatch26(end);
        return;
    }
    a_ldst_s(size, opc, rt, rn, off); a_dmb_ld();
}
#define OFF_X(n) ((u32) offsetof(CPU, x) + 16u * (u32)(n))
static void q_ldc(int vt, u32 off) { E(0x3DC00000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* ldr qt, [cpu, #off] */
static void q_stc(int vt, u32 off) { E(0x3D800000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* str qt, [cpu, #off] */
static void q_ldm(int vt, int rn, int off) { E(0x3CC00000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); a_dmb_ld(); }  /* ldur qt, [rn, #off] */
static void q_stm(int vt, int rn, int off) { a_dmb_st(); E(0x3C800000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }  /* stur qt, [rn, #off] */
static void q_log(int op, int vd, int vn, int vm) {                      /* 0 eor 1 orr 2 and 3 bic: vd = vn op vm (16b) */
    static const u32 base[4] = { 0x6E201C00u, 0x4EA01C00u, 0x4E201C00u, 0x4E601C00u };
    E(base[op] | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void q_v3(int kind, int size, int vd, int vn, int vm) {          /* vd = vn op vm, lanes of 1<<size bytes (16b vector) */
    /* "three same": 0x4E200400 | U << 29 | opcode << 11; "permute" (zip): 0x4E000800 | opcode << 12 */
    static const u32 base[16] = {
        0x4E208400u, 0x6E208400u, 0x6E208C00u,                                   /* 0 add   1 sub    2 cmeq   */
        0x6E206C00u, 0x6E206400u, 0x4E206C00u, 0x4E206400u,                      /* 3 umin  4 umax   5 smin   6 smax  */
        0x4E203400u,                                                             /* 7 cmgt (signed)               */
        0x6E200C00u, 0x4E200C00u, 0x6E202C00u, 0x4E202C00u,                      /* 8 uqadd 9 sqadd 10 uqsub 11 sqsub */
        0x6E201400u, 0x4E209C00u,                                                /* 12 urhadd (pavg)  13 mul      */
        0x4E003800u, 0x4E007800u };                                              /* 14 zip1 (punpckl*) 15 zip2 (punpckh*) */
    E(base[kind] | (u32) size << 22 | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void q_shi(int kind, int esz, int vd, int vn, int n) {            /* 16b vector shift by immediate; kind 2 = srl (ushr), 4 = sra (sshr), 6 = sll (shl); esz = lane bits; 1 <= n < esz (sra: n <= esz - 1) */
    u32 sh = kind == 6 ? (u32)(esz + n) : (u32)(2 * esz - n);
    E((kind == 2 ? 0x6F000400u : kind == 4 ? 0x4F000400u : 0x4F005400u) | sh << 16 | (u32) vn << 5 | (u32) vd);
}
static void q_ins4s(int vd, int di, int vn, int si) { E(0x6E040400u | (u32)((di << 3) | 4) << 16 | (u32)(si << 2) << 11 | (u32) vn << 5 | (u32) vd); }   /* mov vd.s[di], vn.s[si] */
static void q_ins8h(int vd, int di, int vn, int si) { E(0x6E000400u | (u32)((di << 2) | 2) << 16 | (u32)(si << 1) << 11 | (u32) vn << 5 | (u32) vd); }   /* mov vd.h[di], vn.h[si] */
static void q_dup4s(int vd, int vn, int lane) { E(0x4E040400u | (u32)(lane << 3) << 16 | (u32) vn << 5 | (u32) vd); }   /* dup vd.4s, vn.s[lane] */
/* scalar float: sz 8 = double (d regs), 4 = single (s regs). Loads/stores touch only the low sz bytes of the xmm slot. */
static void f_ldc(int sz, int vt, u32 off) { E((sz == 8 ? 0xFD400000u : 0xBD400000u) | ((off >> (sz == 8 ? 3 : 2)) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }   /* ldr dt/st, [cpu, #off] */
static void f_stc(int sz, int vt, u32 off) { E((sz == 8 ? 0xFD000000u : 0xBD000000u) | ((off >> (sz == 8 ? 3 : 2)) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }   /* str dt/st, [cpu, #off] */
static void f_ldm(int sz, int vt, int rn, int off) { E((sz == 8 ? 0xFC400000u : 0xBC400000u) | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); a_dmb_ld(); }          /* ldur dt/st, [rn, #off] (zeroes the rest of vt) */
static void f_stm(int sz, int vt, int rn, int off) { a_dmb_st(); E((sz == 8 ? 0xFC000000u : 0xBC000000u) | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }          /* stur dt/st, [rn, #off] */
#define OFF_MM(n) ((u32) offsetof(CPU, mm) + 8u * (u32)(n))
/* vector operand access: an xmm register is 16 bytes at OFF_X, an MMX register (j->mmx) 8 bytes at OFF_MM (the rest of the vreg is zero) */
static void vm_ldc(int mmx, int vt, int n) { if (mmx) f_ldc(8, vt, OFF_MM(n)); else q_ldc(vt, OFF_X(n)); }
static void vm_stc(int mmx, int vt, int n) { if (mmx) f_stc(8, vt, OFF_MM(n)); else q_stc(vt, OFF_X(n)); }
static void vm_ldm(int mmx, int vt, int rb, int off) { if (mmx) f_ldm(8, vt, rb, off); else q_ldm(vt, rb, off); }
static void vm_stm(int mmx, int vt, int rb, int off) { if (mmx) f_stm(8, vt, rb, off); else q_stm(vt, rb, off); }
static void f_arith(int t, int sz, int vd, int vn, int vm) {             /* t: 0 fadd 1 fmul 2 fsub 3 fdiv 4 fsqrt (vd = sqrt(vm)) */
    static const u32 opc[4] = { 0x2, 0x0, 0x3, 0x1 };
    u32 ty = sz == 8 ? 1u << 22 : 0;
    if (t == 4) E(0x1E21C000u | ty | (u32) vm << 5 | (u32) vd);
    else E(0x1E200800u | ty | opc[t] << 12 | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void f_scvtf(int sz, int src64, int vd, int rn) { E(0x1E220000u | (src64 ? 0x80000000u : 0u) | (sz == 8 ? 1u << 22 : 0u) | (u32) rn << 5 | (u32) vd); }   /* scvtf dd/sd, xn/wn */
static void m_st(int sz, int rt, int rn, int off) {
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    if (j_rc_ok(rt, rn)) {
        if (sz == 1) { a_rc_s(size, 0, rt, rn, off); return; }
        u32 *slow = j_align_guard(sz, rn, off);
        a_rc_s(size, 0, rt, rn, off);
        u32 *end = a_b_fwd();
        jpatch19(slow);
        a_dmb_st(); a_ldst_s(size, 0, rt, rn, off);
        jpatch26(end);
        return;
    }
    a_dmb_st(); a_ldst_s(size, 0, rt, rn, off);
}

