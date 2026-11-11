/* tcg/x86_64/ldst.inc.h - loads and stores. SPDX-License-Identifier: Apache-2.0 */
/* ---- loads / stores */
static void gpr_ld(int nb, int sgn, int to64, int base, s32 d) {       /* -> RAX */
    switch (nb) {
    case 1: op_m(0, sgn ? to64 : 0, sgn ? 0x0FBE : 0x0FB6, RAX, base, d, 0); break;
    case 2: op_m(0, sgn ? to64 : 0, sgn ? 0x0FBF : 0x0FB7, RAX, base, d, 0); break;
    case 4: if (sgn) op_m(0, 1, 0x63, RAX, base, d, 0); else ldq(0, RAX, base, d); break;
    default: ldq(1, RAX, base, d);
    }
}
static void gpr_st(int nb, int src, int base, s32 d) {
    switch (nb) {
    case 1: op_m(0, 0, 0x88, src, base, d, src >= 4); break;
    case 2: op_m(0x66, 0, 0x89, src, base, d, 0); break;
    case 4: stq(0, src, base, d); break;
    default: stq(1, src, base, d);
    }
}
static void vld(int rt, int nb, int base, s32 d) {
    s32 vo = OFF_V(rt);
    if (nb == 16) { op_m(0, 0, 0x0F10, 0, base, d, 0); op_m(0, 0, 0x0F11, 0, R15, vo, 0); }
    else { gpr_ld(nb, 0, 0, base, d); stq(1, RAX, R15, vo); op_m(0, 1, 0xC7, 0, R15, vo + 8, 0); e4(0); }
}
static void vst(int rt, int nb, int base, s32 d) {
    s32 vo = OFF_V(rt);
    if (nb == 16) { op_m(0, 0, 0x0F10, 0, R15, vo, 0); op_m(0, 0, 0x0F11, 0, base, d, 0); }
    else { ldq(1, RAX, R15, vo); gpr_st(nb, RAX, base, d); }
}
static void ext_rcx(int opt, int sh) {
    switch (opt) {
    case 0: movzx8(RCX, RCX); break;
    case 1: movzx16(RCX, RCX); break;
    case 2: mov_rr(0, RCX, RCX); break;
    case 4: movsx8(1, RCX, RCX); break;
    case 5: movsx16(1, RCX, RCX); break;
    case 6: movsxd(RCX, RCX); break;
    default: break;
    }
    sh_i(1, S_SHL, RCX, sh);
}
static int j_ldst(u64 pc, u32 i) {
    int rn = (i >> 5) & 31, rt = i & 31;
    if ((i & 0x3B000000) == 0x18000000) {                      /* literal */
        int opc = i >> 30, V = (i >> 26) & 1;
        u64 a = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) * 4);
        if (!V) {
            if (opc == 3) return 0;
            mov_ri(RCX, a);
            if (opc == 0) ldq(0, RAX, RCX, 0); else if (opc == 1) ldq(1, RAX, RCX, 0); else op_m(0, 1, 0x63, RAX, RCX, 0, 0);
            gst(RAX, rt);
        } else {
            if (opc > 2) return -1;
            mov_ri(RCX, a); vld(rt, 4 << opc, RCX, 0);
        }
        return 0;
    }
    if ((i & 0x3A000000) == 0x28000000) {                      /* pair */
        int opc = i >> 30, V = (i >> 26) & 1, L = (i >> 22) & 1, idx = (i >> 23) & 3, rt2 = (i >> 10) & 31, sc, nb;
        if (opc == 3) return -1;
        sc = !V ? (opc == 2 ? 3 : 2) : 2 + opc; nb = 1 << sc;
        s32 off = (s32)(sext((i >> 15) & 0x7f, 7) * (1 << sc)), d = 0;
        gldsp(1, RCX, rn);
        if (idx == 3) lea(1, RCX, RCX, off); else if (idx != 1) d = off;
        if (!V) {
            if (L) {
                int sg = opc == 1;
                gpr_ld(nb, sg, 1, RCX, d); mov_rr(1, RSI, RAX);
                gpr_ld(nb, sg, 1, RCX, d + nb);
                gst(RSI, rt); gst(RAX, rt2);
            } else {
                gld(1, RAX, rt); gld(1, RDX, rt2);
                gpr_st(nb, RAX, RCX, d); gpr_st(nb, RDX, RCX, d + nb);
            }
        } else if (L) {
            if (nb == 16) {
                op_m(0, 0, 0x0F10, 0, RCX, d, 0); op_m(0, 0, 0x0F10, 1, RCX, d + 16, 0);
                op_m(0, 0, 0x0F11, 0, R15, OFF_V(rt), 0); op_m(0, 0, 0x0F11, 1, R15, OFF_V(rt2), 0);
            } else {
                gpr_ld(nb, 0, 0, RCX, d); mov_rr(1, RSI, RAX);
                gpr_ld(nb, 0, 0, RCX, d + nb);
                stq(1, RSI, R15, OFF_V(rt)); op_m(0, 1, 0xC7, 0, R15, OFF_V(rt) + 8, 0); e4(0);
                stq(1, RAX, R15, OFF_V(rt2)); op_m(0, 1, 0xC7, 0, R15, OFF_V(rt2) + 8, 0); e4(0);
            }
        } else {
            if (nb == 16) {
                op_m(0, 0, 0x0F10, 0, R15, OFF_V(rt), 0); op_m(0, 0, 0x0F10, 1, R15, OFF_V(rt2), 0);
                op_m(0, 0, 0x0F11, 0, RCX, d, 0); op_m(0, 0, 0x0F11, 1, RCX, d + 16, 0);
            } else {
                ldq(1, RAX, R15, OFF_V(rt)); ldq(1, RDX, R15, OFF_V(rt2));
                gpr_st(nb, RAX, RCX, d); gpr_st(nb, RDX, RCX, d + nb);
            }
        }
        if (idx == 1) { lea(1, RSI, RCX, off); gsts(RSI, rn); } else if (idx == 3) gsts(RCX, rn);
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
        s32 d = 0; int wb = 0, imm = 0;
        gldsp(1, RCX, rn);
        if ((i >> 24) & 1) d = (s32)(((i >> 10) & 0xfff) << sc);
        else if (!((i >> 21) & 1)) {
            imm = (s32) sext((i >> 12) & 0x1ff, 9); int m = (i >> 10) & 3;
            if (m == 1) wb = 1;
            else if (m == 3) { lea(1, RCX, RCX, imm); wb = 2; }
            else d = imm;
        } else if (((i >> 10) & 3) == 2) {
            gld(1, RDX, (i >> 16) & 31);
            mov_rr(1, RSI, RCX); mov_rr(1, RCX, RDX);
            ext_rcx((i >> 13) & 7, ((i >> 12) & 1) ? sc : 0);
            alu_rr(1, A_ADD, RCX, RSI);
        } else return -1;
        if (!V) {
            if (isld) { gpr_ld(nb, sgn, to64, RCX, d); gst(RAX, rt); }
            else { gld(1, RAX, rt); gpr_st(nb, RAX, RCX, d); }
        } else {
            if (isld) vld(rt, nb, RCX, d); else vst(rt, nb, RCX, d);
        }
        if (wb == 1) { lea(1, RSI, RCX, imm); gsts(RSI, rn); } else if (wb == 2) gsts(RCX, rn);
        return 0;
    }
    return -1;
}
