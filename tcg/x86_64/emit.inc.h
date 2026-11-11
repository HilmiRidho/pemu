/* tcg/x86_64/emit.inc.h - x86_64 instruction encoders. SPDX-License-Identifier: Apache-2.0 */
/* ---- raw emitters */
static void e1(u32 b) { *jcur++ = (u8) b; }
static void e2(u32 v) { memcpy(jcur, &v, 2); jcur += 2; }
static void e4(u32 v) { memcpy(jcur, &v, 4); jcur += 4; }
static void e8(u64 v) { memcpy(jcur, &v, 8); jcur += 8; }
static void rexp(int w, int reg, int idx, int base, int force) {
    u32 r = 0x40u | ((u32) w << 3) | ((((u32) reg >> 3) & 1) << 2) | ((((u32) idx >> 3) & 1) << 1) | (((u32) base >> 3) & 1);
    if (r != 0x40u || force) e1(r);
}
static void modrm_m(int reg, int base, s32 disp) {
    int b = base & 7;
    int mod = (disp == 0 && b != 5) ? 0 : (disp >= -128 && disp <= 127) ? 1 : 2;
    e1((u32)(mod << 6 | (reg & 7) << 3 | b));
    if (b == 4) e1(0x24);
    if (mod == 1) e1((u32) disp & 0xff); else if (mod == 2) e4((u32) disp);
}
static void op_m(int pfx, int w, u32 op, int reg, int base, s32 disp, int force) {
    if (pfx) e1((u32) pfx);
    rexp(w, reg, 0, base, force);
    if (op > 0xff) { e1(0x0F); e1(op & 0xff); } else e1(op);
    modrm_m(reg, base, disp);
}
static void op_r(int pfx, int w, u32 op, int reg, int rm, int force) {
    if (pfx) e1((u32) pfx);
    rexp(w, reg, 0, rm, force);
    if (op > 0xff) { e1(0x0F); e1(op & 0xff); } else e1(op);
    e1(0xC0u | (u32)(reg & 7) << 3 | (u32)(rm & 7));
}
static void ldq(int w, int r, int base, s32 d) { op_m(0, w, 0x8B, r, base, d, 0); }
static void stq(int w, int r, int base, s32 d) { op_m(0, w, 0x89, r, base, d, 0); }
static void lea(int w, int r, int base, s32 d) { op_m(0, w, 0x8D, r, base, d, 0); }
static void mov_rr(int w, int dst, int src) { op_r(0, w, 0x89, src, dst, 0); }
static void alu_rr(int w, int k, int dst, int src) { op_r(0, w, (u32) k * 8 + 1, src, dst, 0); }
static void alu_ri(int w, int k, int dst, s32 imm) {
    if (imm >= -128 && imm <= 127) { op_r(0, w, 0x83, k, dst, 0); e1((u32) imm & 0xff); }
    else { op_r(0, w, 0x81, k, dst, 0); e4((u32) imm); }
}
static void mov_ri(int r, u64 v) {                  /* never touches flags */
    if (v <= 0xffffffffULL) { rexp(0, 0, 0, r, 0); e1(0xB8u + (u32)(r & 7)); e4((u32) v); }
    else if ((s64) v == (s32) v) { rexp(1, 0, 0, r, 0); e1(0xC7); e1(0xC0u | (u32)(r & 7)); e4((u32) v); }
    else { rexp(1, 0, 0, r, 0); e1(0xB8u + (u32)(r & 7)); e8(v); }
}
static void sh_i(int w, int k, int r, int n) { if (n) { op_r(0, w, 0xC1, k, r, 0); e1((u32) n); } }
static void sh_cl(int w, int k, int r) { op_r(0, w, 0xD3, k, r, 0); }
static void f7(int w, int k, int r) { op_r(0, w, 0xF7, k, r, 0); }
static void imul_rr(int w, int d, int s) { op_r(0, w, 0x0FAF, d, s, 0); }
static void test_rr(int w, int a, int b) { op_r(0, w, 0x85, b, a, 0); }
static void cmov(int w, int cc, int d, int s) { op_r(0, w, 0x0F40u + (u32) cc, d, s, 0); }
static void movzx8(int d, int s) { op_r(0, 0, 0x0FB6, d, s, s >= 4); }
static void movzx16(int d, int s) { op_r(0, 0, 0x0FB7, d, s, 0); }
static void movsx8(int w, int d, int s) { op_r(0, w, 0x0FBE, d, s, s >= 4); }
static void movsx16(int w, int d, int s) { op_r(0, w, 0x0FBF, d, s, 0); }
static void movsxd(int d, int s) { op_r(0, 1, 0x63, d, s, 0); }
static u8 *jcc_fwd(int cc) { e1(0x0F); e1(0x80u + (u32) cc); e4(0); return jcur - 4; }
static u8 *jmp_fwd(void) { e1(0xE9); e4(0); return jcur - 4; }
static void here(u8 *p) { s32 d = (s32)(jcur - (p + 4)); memcpy(p, &d, 4); }
static void jmp_to(u8 *t) { e1(0xE9); e4((u32)(s32)(t - (jcur + 4))); }
static void call_abs(void *f) { mov_ri(RAX, (u64) f); e1(0xFF); e1(0xD0); }
static void fl_spill(void) { e1(0x9F); e1(0x0F); e1(0x90); e1(0xC0); op_m(0x66, 0, 0x89, RAX, R15, OFF_XFL, 0); }
static void fl_restore(void) { op_m(0x66, 0, 0x8B, RAX, R15, OFF_XFL, 0); e1(0x04); e1(0x7F); e1(0x9E); }
static void cmc(void) { e1(0xF5); }
static void stc(void) { e1(0xF9); }
