/* tcg/x86_64/guest_state.inc.h - guest register access and flag state conversion. SPDX-License-Identifier: Apache-2.0 */
/* ---- guest register access (x[31] is never written, XZR reads are constants) */
static void gld(int w, int hr, int g) { if (g == 31) mov_ri(hr, 0); else ldq(w, hr, R15, OFF_X(g)); }
static void gldsp(int w, int hr, int g) { if (g == 31) ldq(w, hr, R15, OFF_SP); else ldq(w, hr, R15, OFF_X(g)); }
static void gst(int hr, int g) { if (g != 31) stq(1, hr, R15, OFF_X(g)); }
static void gsts(int hr, int g) { if (g == 31) stq(1, hr, R15, OFF_SP); else stq(1, hr, R15, OFF_X(g)); }
static void set_pc(u64 pc) { mov_ri(RAX, pc); stq(1, RAX, R15, OFF_PC); }
static int xcc(int cc) { static const u8 t[7] = {4, 3, 8, 0, 7, 0xD, 0xF}; return t[cc >> 1] ^ (cc & 1); }

/* ---- flag state conversion (xfl <-> nzcv) */
static void jsync_in(CPU *c) {
    u32 f = (u32) c->xfl, ah = (f >> 8) & 0xff, al = f & 0xff;
    c->nzcv = ((ah >> 7) & 1) << 3 | ((ah >> 6) & 1) << 2 | ((~ah) & 1) << 1 | (al & 1);
}
static void jsync_out(CPU *c) {
    u32 n = c->nzcv & 15, ah = ((n >> 3) & 1) << 7 | ((n >> 2) & 1) << 6 | ((~n >> 1) & 1);
    c->xfl = (u64)((ah << 8) | (n & 1));
}
