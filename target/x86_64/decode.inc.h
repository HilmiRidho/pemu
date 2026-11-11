/* target/x86_64/decode.inc.h - instruction decoder. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ decoder */
typedef struct { u8*ip; int rex, w, p66, rep, repne, addr32, lock; u64 seg; } I;
typedef struct { int mod, reg, rm, ismem; u64 ea; } M;

static u64 getr(CPU*c, int n, int sz, int rex) {
    switch (sz) {
    case 8: return c->r[n];
    case 4: return (u32) c->r[n];
    case 2: return (u16) c->r[n];
    default: if (!rex && n >= 4 && n < 8) return (c->r[n - 4] >> 8) & 0xff; return c->r[n] & 0xff;
    }
}
static void setr(CPU*c, int n, int sz, int rex, u64 v) {
    switch (sz) {
    case 8: c->r[n] = v; break;
    case 4: c->r[n] = (u32) v; break;
    case 2: c->r[n] = (c->r[n] & ~0xffffULL) | (u16) v; break;
    default:
        if (!rex && n >= 4 && n < 8) c->r[n - 4] = (c->r[n - 4] & ~0xff00ULL) | ((v & 0xff) << 8);
        else c->r[n] = (c->r[n] & ~0xffULL) | (v & 0xff);
    }
}
static void modrm(CPU*c, I*in, M*m, int immbytes) {
    u8 b = *in->ip++;
    m->mod = b >> 6; m->reg = ((b >> 3) & 7) | ((in->rex & 4) << 1); m->rm = (b & 7) | ((in->rex & 1) << 3);
    if (m->mod == 3) { m->ismem = 0; return; }
    m->ismem = 1;
    u64 ea = 0;
    if ((b & 7) == 4) {
        u8 sib = *in->ip++;
        int sc = sib >> 6, idx = ((sib >> 3) & 7) | ((in->rex & 2) << 2), base = (sib & 7) | ((in->rex & 1) << 3);
        if (idx != 4) ea += c->r[idx] << sc;
        if ((sib & 7) == 5 && m->mod == 0) { ea += (u64)(s64)(s32) *(u32u*) in->ip; in->ip += 4; }
        else ea += c->r[base];
    } else if ((b & 7) == 5 && m->mod == 0) {
        s32 d = (s32) *(u32u*) in->ip; in->ip += 4;
        ea = (u64)(in->ip + immbytes) + (u64)(s64) d;
    } else ea = c->r[m->rm];
    if (m->mod == 1) { ea += (u64)(s64)(s8) *in->ip; in->ip += 1; }
    else if (m->mod == 2) { ea += (u64)(s64)(s32) *(u32u*) in->ip; in->ip += 4; }
    if (in->addr32) ea &= 0xffffffffULL;
    m->ea = ea + in->seg;
}
static inline u64 rdm(CPU*c, I*in, M*m, int sz) { return m->ismem ? mrd(m->ea, sz) : getr(c, m->rm, sz, in->rex); }
static inline void wrm(CPU*c, I*in, M*m, int sz, u64 v) { if (m->ismem) mwr(m->ea, v, sz); else setr(c, m->rm, sz, in->rex, v); }
/* ---- atomic read-modify-write on guest memory, for lock-prefixed instructions and xchg with a memory operand.
 * ARMW re-evaluates EXPR (which may set flags in c and may read `oldv`) until the compare-and-swap succeeds, so the
 * flags always come from the successful iteration. Misaligned operands (split locks) cannot be atomic on aarch64:
 * ATOMIC_OK is false for them and the caller falls back to the plain read-modify-write. */
#define ARMW_T(T, ea_, oldv, EXPR) do { T *armw_p = (T*)(ea_); T armw_cur = __atomic_load_n(armw_p, __ATOMIC_RELAXED); \
    for (;;) { u64 oldv = armw_cur; u64 armw_new = (EXPR); \
        if (__atomic_compare_exchange_n(armw_p, &armw_cur, (T) armw_new, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) break; } } while (0)
#define ARMW(ea_, sz_, oldv, EXPR) do { switch (sz_) { \
    case 1: ARMW_T(u8, ea_, oldv, EXPR); break; case 2: ARMW_T(u16, ea_, oldv, EXPR); break; \
    case 4: ARMW_T(u32, ea_, oldv, EXPR); break; default: ARMW_T(u64, ea_, oldv, EXPR); } } while (0)
#define ATOMIC_OK(in_, m_, sz_) ((in_)->lock && (m_)->ismem && !((m_)->ea & ((u64)(sz_) - 1)))
static inline s32 imm32(I*in) { s32 v = (s32) *(u32u*) in->ip; in->ip += 4; return v; }
static inline u64 immz(I*in, int sz) {                       /* Iz sign-extended to 64 */
    if (sz == 2) { u64 v = (u64)(s64)(s16) *(u16u*) in->ip; in->ip += 2; return v; }
    return (u64)(s64) imm32(in);
}
static void push(CPU*c, u64 v) { c->r[RSP] -= 8; mwr(c->r[RSP], v, 8); }
static u64 pop(CPU*c) { u64 v = mrd(c->r[RSP], 8); c->r[RSP] += 8; return v; }

static void undef(CPU*c, const u8*ip, const char*why) {
    fprintf(stderr, "pemu: unsupported instruction (%s) at rip=%#llx: %02x %02x %02x %02x %02x %02x\n", why, (unsigned long long) c->rip,
            ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]);
    g_os->exception(c, EXCP_UD);                          /* the OS layer ends the process */
    __builtin_trap();
}

