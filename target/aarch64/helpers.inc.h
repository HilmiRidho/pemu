/* target/aarch64/helpers.inc.h - register macros. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ helpers */
#define X(r)     ((r) == 31 ? 0 : c->x[r])
#define SET(r,v) do { u64 _sv = (v); if ((r) != 31) c->x[r] = _sv; } while (0)
#define XS(r)    ((r) == 31 ? c->sp : c->x[r])
#define SETS(r,v) do { u64 _sv = (v); if ((r) == 31) c->sp = _sv; else c->x[r] = _sv; } while (0)
#define M32 0xffffffffULL

static inline s64 sext(u64 v, int bits) { return (s64)(v << (64-bits)) >> (64-bits); }
static inline u64 msk(int sf) { return sf ? ~0ULL : M32; }

