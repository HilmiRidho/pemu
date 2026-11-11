/* tcg/aarch64/tcg_target.inc.h - JIT overview and configuration. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ JIT (x86_64 guest -> AArch64 host)
 *
 * Block-translating JIT, modelled on the one in pemu.c (the other direction). A block is a run of guest
 * instructions that ends at a branch, call, return, syscall or after JMAXI instructions.
 *
 *  - Guest rax..rdi are pinned to host x19-x25,x29 while translated code runs; the other registers stay in the CPU
 *    struct (x28 points at it). The pinned ones are written back when the code is left and around a fallback: an
 *    instruction the translator does not know is executed by the interpreter (step()) through a helper call, in the
 *    middle of a translated block, and guest state is always consistent at such a call. After a fault the pinned
 *    registers are read from the signal context (jit_fault).
 *  - Arithmetic flags stay lazy, exactly as in the interpreter (la/lb/lr/lmeta). When the next instruction of the
 *    block reads the flags (jcc, setcc, cmovcc) the host flags (NZCV) of the producing instruction are used directly
 *    (cmp + b.cond). The lazy fields are only written when the flags can still be observed (liveness within the
 *    block; flags are assumed live at a block exit) and always before anything that can
 *    fault or call a helper, so a fault or a signal sees exact flags.
 *  - Blocks are superblocks: direct jmp and forward jcc on the same page are followed (a jcc's taken path is an out-of-line
 *    side exit), and a call continues inline after the callee returns. Direct branches are chained (patched on first use);
 *    indirect ones use a per-thread direct-mapped inline cache. call = host bl + a push on the return stack buffer in the
 *    CPU struct, ret = pop + compare + host ret (inline cache on a mismatch). Pending signals are polled on backward and
 *    indirect branches and after every syscall.
 *  - Faults in translated code are mapped back to the guest rip through a host-pc -> guest-rip table.
 *  - The cache is flushed when the guest unmaps, re-protects or MAP_FIXED-overwrites pages that were translated.
 *    Pages that are writable and executable at the same time are never translated (x86 needs no explicit flush
 *    for self-modifying code), they are interpreted. Instructions that end a block and are not translated (syscall, ...) are
 *    interpreted by the dispatcher outside the cache.
 *
 * Register use of translated code:  x28 CPU*   x27 &pending word of the thread   x26 inline cache of the thread
 *   x19-x25,x29 guest rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi
 *   x9,x10,x11  flag operands a,b,result of the last flag-producing instruction (kept for the lazy spill)
 *   x12,x13     effective address scratch      x0-x8,x14,x15  scratch       x16,x17  call / spill scratch
 *
 * With -DPEMU_JIT_EMBED (x86_64 test build) the generated AArch64 code is executed by the interpreter of pemu.c
 * (see tests/), which is how the back end is tested without an AArch64 machine. */

#ifdef PEMU_JIT_EMBED
extern void pemu_embed_run(u64 *x, u64 pc, u64 stop);
extern u64 pemu_embed_pc(void);
extern u64 pemu_embed_x(int n);
extern void pemu_embed_clear(void);
#endif

#define JCACHE_SZ  (64u << 20)          /* must stay below 128 MB: blocks branch to each other with B imm26 */
#define JTRAMP     64                   /* words reserved at the start of the cache (entry stub, epilogue) */
#define JH_BITS    18
#define JIC_BITS   13
#define JPG_BITS   15
#define JMAXI      64                   /* guest instructions per block */
#define JMAP_CAP   (4u << 20)
#define JRWX_MAX   64
#define JBLOCK_MAX 8192                 /* words a block may need at most; the cache is flushed when less is left */

#define RCPU  28
#define RPEND 27
#define RIC   26

#define OFF_R(n)   ((u32) offsetof(CPU, r) + 8u * (u32)(n))
#define OFF_RIP    ((u32) offsetof(CPU, rip))
#define OFF_RFL    ((u32) offsetof(CPU, rf))
#define OFF_FS     ((u32) offsetof(CPU, fs))
#define OFF_GS     ((u32) offsetof(CPU, gs))
#define OFF_LA     ((u32) offsetof(CPU, la))
#define OFF_LR     ((u32) offsetof(CPU, lr))
#define OFF_LMETA  ((u32) offsetof(CPU, lmeta))
#define OFF_PATCH  ((u32) offsetof(CPU, patch))
#define OFF_RSBI   ((u32) offsetof(CPU, rsbi))
#define OFF_RSB    ((int) offsetof(CPU, rsb))      /* must stay below 512: ldp/stp immediate */

typedef struct { u64 pc; u32 *code; } JEnt;
static u32 *jbase, *jcur, *jlim, *jenter, *jepi;
static JEnt *jh;
static u32 jhn, jmapn, jpgn, jrwxn;
static u32 *jm_off; static u64 *jm_pc, *jpg;
static u64 jrwx[JRWX_MAX][2]; static int jrwx_over;
static volatile int g_jflush_req;
/* ---- one code cache shared by all guest threads.
 *  - Translation and flushing are serialised by jmu. Looking blocks up is lock-free (a block is published into jh only
 *    when it is complete; jh entries are written code first, pc last, with release/acquire).
 *  - Every thread has its own inline cache (its x26) and its own pending-signal word (its x27), so a signal that
 *    arrives on a thread is delivered on that thread, as in the interpreter.
 *  - A flush needs every other thread out of the cache (stop-the-world): the flusher sets g_jstop and bit 0 of every
 *    thread's pending word (translated code polls that word on backward and indirect branches and leaves the cache),
 *    then waits until no thread is 'active'. A thread is active from the moment it relies on a pointer into the cache
 *    (block lookup, chain patching) until it is back in C; system calls and the interpreter run while not active, so a
 *    thread blocked in a futex never delays a flush. g_jgen counts flushes: a thread that sees a new generation clears
 *    its inline cache (and the other per-thread pointers into the old cache) before it enters the cache again. */
typedef struct JT { volatile int active; volatile u64 *pp; JEnt *ic; u32 gen; struct JT *next; } JT;
static __thread JT t_jt;
static JT *jreg;                                               /* every registered thread; protected by jmu */
static pthread_mutex_t jmu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_jstop;
static volatile u32 g_jgen;
static int g_jrcpc;                                            /* JOPT_RCPC is on and the host has FEAT_LRCPC2 (HWCAP_ILRCPC) */
static int g_jmt;                                              /* the guest has more than one thread: translated code carries x86 memory-ordering barriers */
static u32 *jt_start; static u64 jt_pc;                        /* block being translated (visible to its own chained exits only) */
static unsigned long long jst_blocks, jst_flush, jst_chain, jst_ins, jst_words;
static int g_jstats;
#define JOPT_SHFL 1u                    /* flags of shift-by-immediate kept lazy instead of running the interpreter */
#define JOPT_FP   2u                    /* scalar SSE float: movss/movsd, add/sub/mul/div/sqrt ss/sd, cvtsi2ss/sd */
#define JOPT_PIN  4u                    /* guest rax..rdi live in host registers while translated code runs */
#define JOPT_SB   8u                    /* superblocks: translation continues across direct jmp and forward jcc */
#define JOPT_RSB  16u                   /* return stack buffer: call/ret are host bl/ret, checked against a shadow stack */
#define JOPT_FCMP 32u                   /* comiss/comisd/ucomiss/ucomisd with the host flags of fcmp (jb/jae/jbe/ja/jp/jnp/setcc/cmovcc) */
#define JOPT_DIV  64u                   /* 32/64-bit div and idiv with udiv/sdiv when the dividend fits and the divisor is not 0 or -1 */
#define JOPT_CC   128u                  /* jcc/setcc/cmovcc after an interpreter fallback: a flags-only helper call instead of interpreting them too */
#define JOPT_INT  256u                  /* one-operand mul/imul (flags dead), bswap, 8/16-bit shl/shr/sar (flags dead) */
#define JOPT_RCPC 1024u                 /* multi-threaded guests: aligned integer loads/stores use ldapur/stlur (FEAT_LRCPC2) instead of ldur + dmb / dmb + stur; unaligned ones keep the dmb pair */
#define JOPT_ATOM 512u                  /* lock-prefixed add/sub/and/or/xor/inc/dec, cmpxchg, xadd, xchg (LL/SC loops), adc/sbb, push m64, xchg rax,r, movq xmm,xmm/m64, 8/16-bit rol/ror by immediate (flags dead) */
#define JOPT_MISC 2048u                 /* bt/bts/btr/btc, popcnt, tzcnt/lzcnt with live flags, 8/16-bit shl/shr/sar by immediate with live flags, xchg r,r, cld/std, movlps/movhps/movlpd/movhpd/movlhps/movhlps, pshufb */
static unsigned g_jopt = JOPT_MISC | JOPT_SHFL | JOPT_FP | JOPT_PIN | JOPT_SB | JOPT_RSB | JOPT_FCMP | JOPT_DIV | JOPT_CC | JOPT_INT | JOPT_ATOM | JOPT_RCPC;   /* PEMU_JIT_OPT=<mask> overrides (0 = none): handy to bisect a misbehaving guest */
/* Register pinning: guest rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi (numbers 0..7) are kept in callee-saved host registers, so
 * they survive helper calls and chained block-to-block branches. The CPU struct is only written back when translated
 * code is left (jepi), around an interpreter fallback (j_pin_spill / j_pin_fill) and, after a fault, from the signal
 * context (jit_fault). r8..r15 stay in memory. */
static const u8 jpin_reg[8] = { 19, 20, 21, 22, 23, 24, 25, 29 };
static int g_jpin;
static inline int pin(int n) { return (g_jpin && n >= 0 && n < 8) ? jpin_reg[n] : -1; }
static unsigned long long jdyn[512], jst_dyn;
static unsigned long long jdynv[1 << 18];                         /* statistics: the same, by variant (see jfb_key) */
static unsigned jfbv[1 << 18];                     /* statistics: interpreter fallbacks EXECUTED, by opcode (jfbh below counts translated sites) */
static unsigned jfbh[512], jst_fb;                               /* statistics: interpreter fallbacks by opcode (0x100 + x for 0F xx) */

