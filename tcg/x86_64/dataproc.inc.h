/* tcg/x86_64/dataproc.inc.h - data processing (immediate and register). SPDX-License-Identifier: Apache-2.0 */
/* ---- data processing */
static void ld_shifted(int sf, int hr, int rm, int type, int amt) {
    gld(sf, hr, rm);
    if (amt) sh_i(sf, type == 0 ? S_SHL : type == 1 ? S_SHR : type == 2 ? S_SAR : S_ROR, hr, amt);
}
static void and_imm(int w, int reg, u64 v, int tmp) {          /* reg &= v (v already limited to the operand size) */
    if (w ? ((s64) v == (s32) v) : 1) alu_ri(w, A_AND, reg, (s32) v);
    else { mov_ri(tmp, v); alu_rr(w, A_AND, reg, tmp); }
}
static int j_dpimm(u64 pc, u32 i, int *flive) {
    int sf = i >> 31, rd = i & 31, rn = (i >> 5) & 31;
    if ((i & 0x1F000000) == 0x10000000) {                      /* ADR / ADRP */
        s64 imm = sext((((i >> 5) & 0x7ffff) << 2) | ((i >> 29) & 3), 21);
        u64 v = (i >> 31) ? ((pc & ~0xfffULL) + (u64)(imm * 4096)) : pc + (u64) imm;
        if (rd != 31) { mov_ri(RAX, v); gst(RAX, rd); }
    } else if ((i & 0x1F000000) == 0x11000000) {               /* ADD/SUB imm */
        u32 imm = (i >> 10) & 0xfff; if ((i >> 22) & 1) imm <<= 12;
        int sub = (i >> 30) & 1, S = (i >> 29) & 1;
        gldsp(sf, RAX, rn);
        alu_ri(sf, sub ? A_SUB : A_ADD, RAX, (s32) imm);
        if (S && !sub) cmc();
        if (S) gst(RAX, rd); else gsts(RAX, rd);
        if (S) { fl_spill(); *flive = 1; }
    } else if ((i & 0x1F800000) == 0x12000000) {               /* logical imm */
        u64 wm, tm;
        if (decode_bm((i >> 22) & 1, (i >> 10) & 0x3f, (i >> 16) & 0x3f, sf, &wm, &tm) < 0) return -1;
        int opc = (i >> 29) & 3;
        wm &= msk(sf);
        gld(sf, RAX, rn);
        if (opc == 1) { if (sf ? ((s64) wm == (s32) wm) : 1) alu_ri(sf, A_OR, RAX, (s32) wm); else { mov_ri(RCX, wm); alu_rr(sf, A_OR, RAX, RCX); } }
        else if (opc == 2) { if (sf ? ((s64) wm == (s32) wm) : 1) alu_ri(sf, A_XOR, RAX, (s32) wm); else { mov_ri(RCX, wm); alu_rr(sf, A_XOR, RAX, RCX); } }
        else and_imm(sf, RAX, wm, RCX);
        if (opc == 3) { stc(); gst(RAX, rd); fl_spill(); *flive = 1; } else gsts(RAX, rd);
    } else if ((i & 0x1F800000) == 0x12800000) {               /* move wide */
        int opc = (i >> 29) & 3, hw = (i >> 21) & 3; u64 imm = (u64)((i >> 5) & 0xffff) << (hw * 16);
        if (!sf && hw > 1) return -1;
        if (opc == 1) return -1;
        if (opc == 3) {
            if (rd == 31) return 0;
            if (sf) { op_m(0x66, 0, 0xC7, 0, R15, OFF_X(rd) + 2 * hw, 0); e2((u32)((i >> 5) & 0xffff)); }
            else { gld(0, RAX, rd); alu_ri(0, A_AND, RAX, (s32)(u32) ~(0xffffu << (hw * 16))); alu_ri(0, A_OR, RAX, (s32)(u32) imm); gst(RAX, rd); }
        } else if (rd != 31) {
            mov_ri(RAX, opc == 0 ? (~imm & msk(sf)) : imm); gst(RAX, rd);
        }
    } else if ((i & 0x1F800000) == 0x13000000) {               /* bitfield */
        u64 wm, tm; int opc = (i >> 29) & 3, immr = (i >> 16) & 0x3f, imms = (i >> 10) & 0x3f, n = sf ? 64 : 32;
        if (opc == 3 || decode_bm((i >> 22) & 1, imms, immr, sf, &wm, &tm) < 0) return -1;
        if (rd == 31) return 0;
        gld(sf, RAX, rn);
        if (opc == 2) {                                        /* UBFM */
            if (imms == n - 1) sh_i(sf, S_SHR, RAX, immr);                              /* LSR */
            else if (imms + 1 == immr) sh_i(sf, S_SHL, RAX, n - immr);                  /* LSL */
            else if (immr == 0 && imms == 7) movzx8(RAX, RAX);
            else if (immr == 0 && imms == 15) movzx16(RAX, RAX);
            else if (immr == 0 && imms == 31 && sf) mov_rr(0, RAX, RAX);
            else if (imms >= immr) {
                sh_i(sf, S_SHR, RAX, immr);
                int wdt = imms - immr + 1;
                and_imm(sf, RAX, wdt == 64 ? ~0ULL : ((1ULL << wdt) - 1), RCX);
            } else {
                and_imm(sf, RAX, (1ULL << (imms + 1)) - 1, RCX);
                sh_i(sf, S_SHL, RAX, n - immr);
            }
        } else if (opc == 0) {                                 /* SBFM */
            if (imms == n - 1) sh_i(sf, S_SAR, RAX, immr);                              /* ASR */
            else if (immr == 0 && imms == 7) movsx8(sf, RAX, RAX);
            else if (immr == 0 && imms == 15) movsx16(sf, RAX, RAX);
            else if (immr == 0 && imms == 31 && sf) movsxd(RAX, RAX);
            else if (imms >= immr) { sh_i(sf, S_SHL, RAX, n - 1 - imms); sh_i(sf, S_SAR, RAX, n - 1 - imms + immr); }
            else { sh_i(sf, S_SHL, RAX, n - 1 - imms); sh_i(sf, S_SAR, RAX, immr - imms - 1); }
        } else {                                               /* BFM */
            u64 m = msk(sf);
            gld(sf, RDX, rd);
            sh_i(sf, S_ROR, RAX, immr);
            and_imm(sf, RAX, wm & m, RCX);                     /* rax = ror(src) & wm */
            mov_ri(RCX, ~wm & m); alu_rr(sf, A_AND, RCX, RDX); /* rcx = dst & ~wm */
            alu_rr(sf, A_OR, RAX, RCX);                        /* bot */
            and_imm(sf, RAX, tm & m, RCX);                     /* bot & tm */
            mov_ri(RCX, ~tm & m); alu_rr(sf, A_AND, RCX, RDX); /* dst & ~tm */
            alu_rr(sf, A_OR, RAX, RCX);
        }
        gst(RAX, rd);
    } else if ((i & 0x1F800000) == 0x13800000) {               /* EXTR */
        int imms = (i >> 10) & 0x3f, rm = (i >> 16) & 31;
        if (!sf && imms >= 32) return -1;
        if (rd == 31) return 0;
        gld(sf, RAX, rm); gld(sf, RCX, rn);
        if (imms) { op_r(0, sf, 0x0FAC, RCX, RAX, 0); e1((u32) imms); }       /* shrd rax, rcx, imms */
        gst(RAX, rd);
    } else return -1;
    return 0;
}
static void j_div(int sf, int sgn, int rd, int rn, int rm) {
    gld(sf, RAX, rn); gld(sf, RCX, rm);
    test_rr(sf, RCX, RCX);
    u8 *nz = jcc_fwd(5);
    mov_ri(RAX, 0); u8 *d1 = jmp_fwd();
    here(nz);
    if (!sgn) { mov_ri(RDX, 0); f7(sf, 6, RCX); }
    else {
        alu_ri(sf, A_CMP, RCX, -1);
        u8 *gen = jcc_fwd(5);
        f7(sf, 3, RAX);
        u8 *d2 = jmp_fwd();
        here(gen);
        if (sf) e1(0x48);
        e1(0x99);
        f7(sf, 7, RCX);
        here(d2);
    }
    here(d1);
    gst(RAX, rd);
}
/* returns 1 when the instruction ended the block, 0 to continue, -1 when it is not handled */
static int j_dpreg(u64 pc, u32 i, int fl, int *flive) {
    int sf = i >> 31, rd = i & 31, rn = (i >> 5) & 31, rm = (i >> 16) & 31;
    (void) pc;
    if ((i & 0x1F000000) == 0x0A000000) {                      /* logical shifted */
        int opc = (i >> 29) & 3, nn = (i >> 21) & 1, amt = (i >> 10) & 0x3f, ty = (i >> 22) & 3;
        if (!sf && amt >= 32) return -1;
        if (opc == 1 && rn == 31 && !nn) {                     /* MOV / shifted MOV */
            ld_shifted(sf, RAX, rm, ty, amt); gst(RAX, rd); return 0;
        }
        ld_shifted(sf, RCX, rm, ty, amt);
        if (nn) f7(sf, 2, RCX);
        gld(sf, RAX, rn);
        alu_rr(sf, opc == 1 ? A_OR : opc == 2 ? A_XOR : A_AND, RAX, RCX);
        if (opc == 3) stc();
        gst(RAX, rd);
        if (opc == 3) { fl_spill(); *flive = 1; }
    } else if ((i & 0x1F200000) == 0x0B000000) {               /* add/sub shifted */
        int sub = (i >> 30) & 1, S = (i >> 29) & 1, ty = (i >> 22) & 3, amt = (i >> 10) & 0x3f;
        if (ty == 3 || (!sf && amt >= 32)) return -1;
        ld_shifted(sf, RCX, rm, ty, amt);
        gld(sf, RAX, rn);
        alu_rr(sf, sub ? A_SUB : A_ADD, RAX, RCX);
        if (S && !sub) cmc();
        gst(RAX, rd);
        if (S) { fl_spill(); *flive = 1; }
    } else if ((i & 0x1F200000) == 0x0B200000) {               /* add/sub extended */
        int sub = (i >> 30) & 1, S = (i >> 29) & 1, sh = (i >> 10) & 7;
        gld(1, RCX, rm); ext_rcx((i >> 13) & 7, sh);
        gldsp(sf, RAX, rn);
        alu_rr(sf, sub ? A_SUB : A_ADD, RAX, RCX);
        if (S && !sub) cmc();
        if (S) gst(RAX, rd); else gsts(RAX, rd);
        if (S) { fl_spill(); *flive = 1; }
    } else if ((i & 0x1FE00000) == 0x1A000000) {               /* ADC / SBC */
        int sub = (i >> 30) & 1, S = (i >> 29) & 1;
        if (!fl) fl_restore();
        gld(sf, RAX, rn); gld(sf, RCX, rm);
        if (sub) alu_rr(sf, A_SBB, RAX, RCX);
        else { cmc(); alu_rr(sf, A_ADC, RAX, RCX); if (S) cmc(); }
        gst(RAX, rd);
        if (S) { fl_spill(); *flive = 1; }
    } else if ((i & 0x1FE00000) == 0x1A400000) {               /* CCMP / CCMN */
        int sub = (i >> 30) & 1, cc = (i >> 12) & 15;
        int nz = i & 15;
        u32 ah = ((nz >> 3) & 1) << 7 | ((nz >> 2) & 1) << 6 | ((~nz >> 1) & 1), cx = (ah << 8) | (u32)(nz & 1);
        u8 *jf = NULL, *jd = NULL;
        if ((cc >> 1) != 7) { if (!fl) fl_restore(); jf = jcc_fwd(xcc(cc) ^ 1); }
        gld(sf, RAX, rn);
        if ((i >> 11) & 1) mov_ri(RCX, (i >> 16) & 31); else gld(sf, RCX, rm);
        alu_rr(sf, sub ? A_CMP : A_ADD, RAX, RCX);
        if (!sub) cmc();
        fl_spill();
        if (jf) {
            jd = jmp_fwd();
            here(jf);
            op_m(0, 1, 0xC7, 0, R15, OFF_XFL, 0); e4(cx);
            here(jd);
        }
    } else if ((i & 0x1FE00000) == 0x1A800000) {               /* CSEL family */
        int op = (i >> 30) & 1, o2 = (i >> 10) & 1, cc = (i >> 12) & 15;
        if ((cc >> 1) == 7) { gld(sf, RAX, rn); gst(RAX, rd); return 0; }
        if (!fl) fl_restore();
        gld(sf, RDX, rm);
        if (op) f7(sf, 2, RDX);
        if (o2) lea(sf, RDX, RDX, 1);
        gld(sf, RCX, rn);
        cmov(sf, xcc(cc), RDX, RCX);
        gst(RDX, rd);
    } else if ((i & 0x5FE00000) == 0x1AC00000) {               /* 2-source */
        int op = (i >> 10) & 0x3f;
        switch (op) {
        case 2: j_div(sf, 0, rd, rn, rm); break;
        case 3: j_div(sf, 1, rd, rn, rm); break;
        case 8: case 9: case 10: case 11:
            gld(sf, RAX, rn); gld(sf, RCX, rm);
            sh_cl(sf, op == 8 ? S_SHL : op == 9 ? S_SHR : op == 10 ? S_SAR : S_ROR, RAX);
            gst(RAX, rd); break;
        default: return -1;
        }
    } else if ((i & 0x5FE00000) == 0x5AC00000) {               /* 1-source */
        int op = (i >> 10) & 0x3f, n = sf ? 64 : 32;
        if (op == 4) {                                         /* CLZ */
            gld(sf, RAX, rn);
            op_r(0, sf, 0x0FBD, RCX, RAX, 0);
            mov_ri(RDX, ~0ULL); cmov(1, 4, RCX, RDX);
            f7(1, 3, RCX); alu_ri(1, A_ADD, RCX, n - 1);
            gst(RCX, rd);
        } else if (op == 2 || (op == 3 && sf)) {               /* REV32 / REV */
            gld(sf, RAX, rn);
            rexp(sf, 0, 0, RAX, 0); e1(0x0F); e1(0xC8);        /* bswap rax / eax */
            if (op == 2 && sf) sh_i(1, S_ROR, RAX, 32);
            gst(RAX, rd);
        } else return -1;
    } else if ((i & 0x1F000000) == 0x1B000000) {               /* 3-source */
        int o0 = (i >> 15) & 1, op31 = (i >> 21) & 7, ra = (i >> 10) & 31;
        switch (op31) {
        case 0:
            gld(sf, RAX, rn); gld(sf, RCX, rm); imul_rr(sf, RAX, RCX);
            if (ra != 31) { gld(sf, RDX, ra); if (o0) { alu_rr(sf, A_SUB, RDX, RAX); mov_rr(sf, RAX, RDX); } else alu_rr(sf, A_ADD, RAX, RDX); }
            else if (o0) f7(sf, 3, RAX);
            gst(RAX, rd); break;
        case 1: case 5:
            gld(0, RAX, rn); gld(0, RCX, rm);
            if (op31 == 1) { movsxd(RAX, RAX); movsxd(RCX, RCX); }
            imul_rr(1, RAX, RCX);
            if (ra != 31) { gld(1, RDX, ra); if (o0) { alu_rr(1, A_SUB, RDX, RAX); mov_rr(1, RAX, RDX); } else alu_rr(1, A_ADD, RAX, RDX); }
            else if (o0) f7(1, 3, RAX);
            gst(RAX, rd); break;
        case 2: case 6:
            gld(1, RAX, rn); gld(1, RCX, rm);
            f7(1, op31 == 2 ? 5 : 4, RCX);
            gst(RDX, rd); break;
        default: return -1;
        }
    } else return -1;
    return 0;
}
