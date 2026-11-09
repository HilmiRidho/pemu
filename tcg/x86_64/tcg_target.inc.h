/* tcg/x86_64/tcg_target.inc.h - JIT overview and configuration (AArch64 guest -> x86_64 host). SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ JIT (AArch64 -> x86_64)
 *
 * Block-translating JIT. Guest registers stay in the CPU struct (r15 points at it);
 * every guest instruction becomes a short x86_64 sequence that works on that memory
 * directly, so no register state is cached across instructions. This keeps the
 * fallback path trivial: any instruction not translated natively is executed by the
 * interpreter (step()) through a helper call, in the middle of a translated block.
 *
 * Condition flags: after a flag-setting instruction the x86 flags are captured with
 * LAHF/SETO into cpu.xfl (carry kept INVERTED, i.e. x86 "borrow" convention, so one
 * jcc table serves both ADD and SUB). A consumer right after the producer uses the
 * live x86 flags, otherwise they are restored with ADD AL,0x7f / SAHF.
 * cpu.xfl is the canonical flag state while the JIT runs; cpu.nzcv is only valid
 * around interpreter helpers (jsync_in / jsync_out convert between the two).
 *
 * Direct branches are chained (patched on first use); indirect ones use a small
 * direct-mapped inline cache. Async signals are polled on backward / indirect
 * branches and after every syscall. The cache is flushed when the guest changes
 * executable mappings (munmap, mprotect, MAP_FIXED over code, IC instructions). */
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { A_ADD, A_OR, A_ADC, A_SBB, A_AND, A_SUB, A_XOR, A_CMP };
enum { S_ROL = 0, S_ROR = 1, S_SHL = 4, S_SHR = 5, S_SAR = 7 };
#define OFF_X(n)   ((s32) offsetof(CPU, x) + 8 * (n))
#define OFF_V(n)   ((s32) offsetof(CPU, v) + 16 * (n))
#define OFF_SP     ((s32) offsetof(CPU, sp))
#define OFF_PC     ((s32) offsetof(CPU, pc))
#define OFF_TPIDR  ((s32) offsetof(CPU, tpidr))
#define OFF_XFL    ((s32) offsetof(CPU, xfl))
#define OFF_JFLAG  ((s32) offsetof(CPU, jflag))
#define OFF_PATCH  ((s32) offsetof(CPU, patch))

#define JCACHE_SZ  (160u << 20)
#define JTRAMP_SZ  128
#define JH_BITS    19
#define JIC_BITS   14
#define JMAP_CAP   (6u << 20)
#define JPG_BITS   15
#define JMAXI      48

typedef struct { u64 pc; u8 *code; } JEnt;
static u8 *jbase, *jcur, *jlim, *jenter, *jepi;
static JEnt *jh, *jic;
static u32 jhn, jmapn, jpgn;
static u32 *jm_off; static u64 *jm_pc, *jpg;
static unsigned long long jstat_blocks, jstat_flush;
