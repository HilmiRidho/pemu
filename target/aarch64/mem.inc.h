/* target/aarch64/mem.inc.h - guest memory access and flag helpers. SPDX-License-Identifier: Apache-2.0 */

static u64 mrd(u64 a, int n) {
    switch (n) { case 1: return *(u8*)a; case 2: return *(u16u*)a; case 4: return *(u32u*)a; default: return *(u64u*)a; }
}
static void mwr(u64 a, u64 v, int n) {
    switch (n) { case 1: *(u8*)a = (u8)v; break; case 2: *(u16u*)a = (u16)v; break; case 4: *(u32u*)a = (u32)v; break; default: *(u64u*)a = v; }
}

static u64 adc(CPU*c, int sf, u64 a, u64 b, int cin, int setf) {
    u64 m = msk(sf), r; int C, V;
    a &= m; b &= m;
    if (sf) {
        unsigned __int128 s = (unsigned __int128) a + b + cin;
        r = (u64) s; C = (int)((s >> 64) & 1); V = (int)((((a ^ r) & (b ^ r)) >> 63) & 1);
    } else {
        u64 s = a + b + cin;
        r = s & m; C = (int)((s >> 32) & 1); V = (int)((((a ^ r) & (b ^ r)) >> 31) & 1);
    }
    if (setf) c->nzcv = (u32)((((r >> (sf ? 63 : 31)) & 1) << 3) | ((r == 0) << 2) | (C << 1) | V);
    return r;
}
static u64 addsub(CPU*c, int sf, u64 a, u64 b, int sub, int setf) {
    return adc(c, sf, a, sub ? ~b : b, sub, setf);
}
static int cond(CPU*c, int cc) {
    int n = (c->nzcv >> 3) & 1, z = (c->nzcv >> 2) & 1, cf = (c->nzcv >> 1) & 1, v = c->nzcv & 1, r;
    switch (cc >> 1) {
    case 0: r = z; break; case 1: r = cf; break; case 2: r = n; break; case 3: r = v; break;
    case 4: r = cf && !z; break; case 5: r = n == v; break; case 6: r = n == v && !z; break;
    default: r = 1; }
    if ((cc & 1) && cc != 15) r = !r;
    return r;
}
static u64 shiftv(u64 v, int type, int amt, int sf) {
    int n = sf ? 64 : 32;
    u64 m = msk(sf);
    v &= m; amt &= n - 1;
    switch (type) {
    case 0: return (v << amt) & m;
    case 1: return v >> amt;
    case 2: return (u64)((sf ? (s64) v : (s64)(s32) v) >> amt) & m;
    default: return amt ? ((v >> amt) | (v << (n - amt))) & m : v;
    }
}
static u64 extendv(u64 v, int opt, int sh) {
    switch (opt) {
    case 0: v &= 0xff; break; case 1: v &= 0xffff; break; case 2: v &= M32; break;
    case 4: v = (u64) sext(v, 8); break; case 5: v = (u64) sext(v, 16); break; case 6: v = (u64) sext(v, 32); break;
    }
    return v << sh;
}
static u64 ror_n(u64 w, int r, int n) {
    u64 m = n == 64 ? ~0ULL : ((1ULL << n) - 1);
    r %= n;
    return r ? ((w >> r) | (w << (n - r))) & m : w & m;
}
static int decode_bm(int N, int imms, int immr, int sf, u64*wm, u64*tm) {
    unsigned x = ((unsigned) N << 6) | (~imms & 0x3f);
    if (!x) return -1;
    int len = 31 - __builtin_clz(x);
    if (len < 1 || (!sf && N)) return -1;
    int es = 1 << len, lv = es - 1;
    int S = imms & lv, R = immr & lv, d = (S - R) & lv;
    u64 w = S + 1 == 64 ? ~0ULL : ((1ULL << (S + 1)) - 1);
    u64 t = d + 1 == 64 ? ~0ULL : ((1ULL << (d + 1)) - 1);
    w = ror_n(w, R, es);
    int ds = sf ? 64 : 32;
    u64 W = 0, T = 0;
    for (int i = 0; i < ds; i += es) { W |= w << i; T |= t << i; }
    *wm = W; *tm = T;
    return 0;
}

