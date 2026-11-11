/* target/x86_64/sse2.inc.h - SSE2. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ SSE2 */
static void pint(int op, V*d, const V*s, int*ok) {
    V t = *d; int i;
    switch (op) {
    case 0x60: for (i = 0; i < 8; i++) { d->b[2 * i] = t.b[i]; d->b[2 * i + 1] = s->b[i]; } break;
    case 0x61: for (i = 0; i < 4; i++) { d->w[2 * i] = t.w[i]; d->w[2 * i + 1] = s->w[i]; } break;
    case 0x62: for (i = 0; i < 2; i++) { d->d[2 * i] = t.d[i]; d->d[2 * i + 1] = s->d[i]; } break;
    case 0x63: for (i = 0; i < 8; i++) { int v = t.sw[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); int w = s->sw[i]; d->sb[8 + i] = (s8)(w > 127 ? 127 : w < -128 ? -128 : w); } break;
    case 0x64: for (i = 0; i < 16; i++) d->b[i] = t.sb[i] > s->sb[i] ? 0xff : 0; break;
    case 0x65: for (i = 0; i < 8; i++) d->w[i] = t.sw[i] > s->sw[i] ? 0xffff : 0; break;
    case 0x66: for (i = 0; i < 4; i++) d->d[i] = t.sd[i] > s->sd[i] ? 0xffffffffu : 0; break;
    case 0x67: for (i = 0; i < 8; i++) { int v = t.sw[i]; d->b[i] = (u8)(v > 255 ? 255 : v < 0 ? 0 : v); int w = s->sw[i]; d->b[8 + i] = (u8)(w > 255 ? 255 : w < 0 ? 0 : w); } break;
    case 0x68: for (i = 0; i < 8; i++) { d->b[2 * i] = t.b[8 + i]; d->b[2 * i + 1] = s->b[8 + i]; } break;
    case 0x69: for (i = 0; i < 4; i++) { d->w[2 * i] = t.w[4 + i]; d->w[2 * i + 1] = s->w[4 + i]; } break;
    case 0x6A: for (i = 0; i < 2; i++) { d->d[2 * i] = t.d[2 + i]; d->d[2 * i + 1] = s->d[2 + i]; } break;
    case 0x6B: for (i = 0; i < 4; i++) { s64 v = t.sd[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); s64 w = s->sd[i]; d->sw[4 + i] = (s16)(w > 32767 ? 32767 : w < -32768 ? -32768 : w); } break;
    case 0x6C: d->q[0] = t.q[0]; d->q[1] = s->q[0]; break;
    case 0x6D: d->q[0] = t.q[1]; d->q[1] = s->q[1]; break;
    case 0x74: for (i = 0; i < 16; i++) d->b[i] = t.b[i] == s->b[i] ? 0xff : 0; break;
    case 0x75: for (i = 0; i < 8; i++) d->w[i] = t.w[i] == s->w[i] ? 0xffff : 0; break;
    case 0x76: for (i = 0; i < 4; i++) d->d[i] = t.d[i] == s->d[i] ? 0xffffffffu : 0; break;
    case 0xD1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->w[i] = n > 15 ? 0 : (u16)(t.w[i] >> n); break; }
    case 0xD2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->d[i] = n > 31 ? 0 : t.d[i] >> n; break; }
    case 0xD3: { u64 n = s->q[0]; for (i = 0; i < 2; i++) d->q[i] = n > 63 ? 0 : t.q[i] >> n; break; }
    case 0xD4: d->q[0] = t.q[0] + s->q[0]; d->q[1] = t.q[1] + s->q[1]; break;
    case 0xD5: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.sw[i] * s->sw[i]); break;
    case 0xD8: for (i = 0; i < 16; i++) d->b[i] = t.b[i] > s->b[i] ? t.b[i] - s->b[i] : 0; break;
    case 0xD9: for (i = 0; i < 8; i++) d->w[i] = t.w[i] > s->w[i] ? t.w[i] - s->w[i] : 0; break;
    case 0xDA: for (i = 0; i < 16; i++) d->b[i] = t.b[i] < s->b[i] ? t.b[i] : s->b[i]; break;
    case 0xDB: d->q[0] = t.q[0] & s->q[0]; d->q[1] = t.q[1] & s->q[1]; break;
    case 0xDC: for (i = 0; i < 16; i++) { int v = t.b[i] + s->b[i]; d->b[i] = (u8)(v > 255 ? 255 : v); } break;
    case 0xDD: for (i = 0; i < 8; i++) { int v = t.w[i] + s->w[i]; d->w[i] = (u16)(v > 65535 ? 65535 : v); } break;
    case 0xDE: for (i = 0; i < 16; i++) d->b[i] = t.b[i] > s->b[i] ? t.b[i] : s->b[i]; break;
    case 0xDF: d->q[0] = ~t.q[0] & s->q[0]; d->q[1] = ~t.q[1] & s->q[1]; break;
    case 0xE0: for (i = 0; i < 16; i++) d->b[i] = (u8)((t.b[i] + s->b[i] + 1) >> 1); break;
    case 0xE1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->sw[i] = (s16)(t.sw[i] >> (n > 15 ? 15 : n)); break; }
    case 0xE2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->sd[i] = t.sd[i] >> (n > 31 ? 31 : n); break; }
    case 0xE3: for (i = 0; i < 8; i++) d->w[i] = (u16)((t.w[i] + s->w[i] + 1) >> 1); break;
    case 0xE4: for (i = 0; i < 8; i++) d->w[i] = (u16)(((u32) t.w[i] * s->w[i]) >> 16); break;
    case 0xE5: for (i = 0; i < 8; i++) d->w[i] = (u16)(((s32) t.sw[i] * s->sw[i]) >> 16); break;
    case 0xE8: for (i = 0; i < 16; i++) { int v = t.sb[i] - s->sb[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); } break;
    case 0xE9: for (i = 0; i < 8; i++) { int v = t.sw[i] - s->sw[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); } break;
    case 0xEA: for (i = 0; i < 8; i++) d->sw[i] = t.sw[i] < s->sw[i] ? t.sw[i] : s->sw[i]; break;
    case 0xEB: d->q[0] = t.q[0] | s->q[0]; d->q[1] = t.q[1] | s->q[1]; break;
    case 0xEC: for (i = 0; i < 16; i++) { int v = t.sb[i] + s->sb[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); } break;
    case 0xED: for (i = 0; i < 8; i++) { int v = t.sw[i] + s->sw[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); } break;
    case 0xEE: for (i = 0; i < 8; i++) d->sw[i] = t.sw[i] > s->sw[i] ? t.sw[i] : s->sw[i]; break;
    case 0xEF: d->q[0] = t.q[0] ^ s->q[0]; d->q[1] = t.q[1] ^ s->q[1]; break;
    case 0xF1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->w[i] = n > 15 ? 0 : (u16)(t.w[i] << n); break; }
    case 0xF2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->d[i] = n > 31 ? 0 : t.d[i] << n; break; }
    case 0xF3: { u64 n = s->q[0]; for (i = 0; i < 2; i++) d->q[i] = n > 63 ? 0 : t.q[i] << n; break; }
    case 0xF4: d->q[0] = (u64) t.d[0] * s->d[0]; d->q[1] = (u64) t.d[2] * s->d[2]; break;
    case 0xF5: for (i = 0; i < 4; i++) d->sd[i] = t.sw[2 * i] * s->sw[2 * i] + t.sw[2 * i + 1] * s->sw[2 * i + 1]; break;
    case 0xF6: for (int h = 0; h < 2; h++) { u32 sum = 0; for (i = 0; i < 8; i++) sum += (u32)abs((int) t.b[8 * h + i] - (int) s->b[8 * h + i]); d->q[h] = sum; } break;
    case 0xF8: for (i = 0; i < 16; i++) d->b[i] = (u8)(t.b[i] - s->b[i]); break;
    case 0xF9: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.w[i] - s->w[i]); break;
    case 0xFA: for (i = 0; i < 4; i++) d->d[i] = t.d[i] - s->d[i]; break;
    case 0xFB: d->q[0] = t.q[0] - s->q[0]; d->q[1] = t.q[1] - s->q[1]; break;
    case 0xFC: for (i = 0; i < 16; i++) d->b[i] = (u8)(t.b[i] + s->b[i]); break;
    case 0xFD: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.w[i] + s->w[i]); break;
    case 0xFE: for (i = 0; i < 4; i++) d->d[i] = t.d[i] + s->d[i]; break;
    default: *ok = 0;
    }
}
static u64 cvt_i(double v, int sz, int tr) {
    double r = tr ? trunc(v) : nearbyint(v);
    if (isnan(r) || (sz == 8 ? (r >= 9223372036854775808.0 || r < -9223372036854775808.0) : (r >= 2147483648.0 || r < -2147483648.0)))
        return sz == 8 ? 0x8000000000000000ULL : 0x80000000ULL;
    return sz == 8 ? (u64)(s64) r : (u64)(u32)(s32) r;
}

#define SRC(n) do { if (m.ismem) { memset(&sv, 0, 16); memcpy(&sv, (void*) m.ea, (size_t)(n)); } else sv = c->x[m.rm]; s = &sv; } while (0)
#define FARITH(T, SQRT, FLD, N) for (int i = 0; i < (N); i++) { T a = d->FLD[i], b = s->FLD[i], r; \
    switch (op) { case 0x51: r = SQRT(b); break; case 0x58: r = a + b; break; case 0x59: r = a * b; break; \
    case 0x5C: r = a - b; break; case 0x5D: r = a < b ? a : b; break; case 0x5E: r = a / b; break; default: r = a > b ? a : b; } d->FLD[i] = r; }

static int cmp_pred(int p, double a, double b) {
    int un = isnan(a) || isnan(b);
    switch (p & 7) {
    case 0: return !un && a == b; case 1: return !un && a < b; case 2: return !un && a <= b; case 3: return un;
    case 4: return un || a != b; case 5: return un || !(a < b); case 6: return un || !(a <= b); default: return !un;
    }
}

