/* tests/jit_sse4_mem.c - guest for tests/jit_sse4.sh: memory operands (also at the very end of a page), the exact flags and every condition code after ptest. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <immintrin.h>
static uint64_t seed = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static __m128i rv(void) { uint64_t a = rnd(), b = rnd(); switch (rnd() % 5) { case 0: a = 0; break; case 1: b = ~0ULL; break; case 2: a &= 0x8000800080008000ULL; break; case 3: b &= 0xff00ff00ff00ff00ULL; break; } return _mm_set_epi64x((long long) b, (long long) a); }
static uint64_t H = 14695981039346656037ULL;
static void mix(__m128i v) { uint64_t t[2]; memcpy(t, &v, 16); H = (H ^ t[0]) * 0x100000001b3ULL; H = (H ^ t[1]) * 0x100000001b3ULL; H ^= H >> 29; }
static void mixu(uint64_t v) { H = (H ^ v) * 0x100000001b3ULL; H ^= H >> 31; }
static __attribute__((aligned(16))) __m128i M;
#define MEMOP(nm) do { H = 14695981039346656037ULL; for (int i = 0; i < 500; i++) { __m128i a = rv(); M = rv(); __asm__ volatile(nm " %1, %0" : "+x"(a) : "m"(M)); mix(a); } printf("m %-10s %016llx\n", nm, (unsigned long long) H); } while (0)
/* pmov*: the memory operand is only 8 / 4 / 2 bytes wide: it is placed so that it ends exactly at the end of a mapped page, with a PROT_NONE page behind it */
static uint8_t *edge;
#define PMOV(nm, n) do { H = 14695981039346656037ULL; for (int i = 0; i < 500; i++) { uint8_t *p = edge + 4096 - (n); for (int k = 0; k < (n); k++) p[k] = (uint8_t) rnd(); __m128i r = _mm_set1_epi8(0x55); \
    __asm__ volatile(nm " (%1), %0" : "+x"(r) : "r"(p) : "memory"); mix(r); } printf("p %-10s %016llx\n", nm, (unsigned long long) H); } while (0)
static unsigned long fl_after_ptest(__m128i a, __m128i b) { unsigned long f; __asm__ volatile("movl $0x7fffffff, %%eax; addl $1, %%eax; ptest %2, %1; pushfq; popq %0" : "=r"(f) : "x"(a), "x"(b) : "cc", "rax"); return f & 0x8d5; }
static unsigned long fl_after_ptest_m(__m128i a) { unsigned long f; __asm__ volatile("movl $0x7fffffff, %%eax; addl $1, %%eax; ptest %2, %1; pushfq; popq %0" : "=r"(f) : "x"(a), "m"(M) : "cc", "rax"); return f & 0x8d5; }
int main(void) {
    MEMOP("pminsb"); MEMOP("pminsd"); MEMOP("pminuw"); MEMOP("pminud"); MEMOP("pmaxsb"); MEMOP("pmaxsd"); MEMOP("pmaxuw"); MEMOP("pmaxud");
    MEMOP("pcmpeqq"); MEMOP("pcmpgtq"); MEMOP("pmulld"); MEMOP("packusdw"); MEMOP("pabsb"); MEMOP("pabsw"); MEMOP("pabsd");
    edge = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); mprotect(edge + 4096, 4096, PROT_NONE);
    PMOV("pmovzxbw", 8); PMOV("pmovsxbw", 8); PMOV("pmovzxbd", 4); PMOV("pmovsxbd", 4); PMOV("pmovzxbq", 2); PMOV("pmovsxbq", 2);
    PMOV("pmovzxwd", 8); PMOV("pmovsxwd", 8); PMOV("pmovzxwq", 4); PMOV("pmovsxwq", 4); PMOV("pmovzxdq", 8); PMOV("pmovsxdq", 8);
    /* ptest: exact flags after it (the flags it must clear are set beforehand), register and memory form */
    H = 14695981039346656037ULL;
    for (int i = 0; i < 800; i++) { __m128i a = rv(), b = rv(); if (i % 4 == 0) b = a; if (i % 6 == 0) a = _mm_setzero_si128(); if (i % 9 == 0) b = _mm_setzero_si128(); if (i % 11 == 0) a = _mm_set1_epi8(-1);
        mixu(fl_after_ptest(a, b)); M = b; mixu(fl_after_ptest_m(a)); }
    printf("ptest flags  %016llx\n", (unsigned long long) H);
    /* every condition code after ptest (setcc and jcc read the cached host C or the exact flags from memory) */
    H = 14695981039346656037ULL;
    for (int i = 0; i < 800; i++) { __m128i a = rv(), b = rv(); if (i % 4 == 0) b = a; if (i % 6 == 0) a = _mm_setzero_si128(); if (i % 9 == 0) b = _mm_setzero_si128();
        unsigned char c[16];
        __asm__ volatile("ptest %1, %0; setb %2; setae %3; sete %4; setne %5; setbe %6; seta %7; sets %8; setns %9; seto %10; setno %11; setp %12; setnp %13; setl %14; setge %15; setle %16; setg %17"
            : "+x"(a), "+x"(b), "=m"(c[0]), "=m"(c[1]), "=m"(c[2]), "=m"(c[3]), "=m"(c[4]), "=m"(c[5]), "=m"(c[6]), "=m"(c[7]), "=m"(c[8]), "=m"(c[9]), "=m"(c[10]), "=m"(c[11]), "=m"(c[12]), "=m"(c[13]) , "=m"(c[14]), "=m"(c[15]) :: "cc");
        uint64_t t[2]; memcpy(t, c, 16); mixu(t[0]); mixu(t[1]);
        int jc = 0; __asm__ volatile("ptest %2, %1; jnc 1f; addl $1, %0; 1: jz 2f; addl $2, %0; 2: ja 3f; addl $4, %0; 3: jbe 4f; addl $8, %0; 4:" : "+r"(jc) : "x"(a), "x"(b) : "cc"); mixu((uint64_t) jc);
        int cm = 100, cm2 = 200; __asm__ volatile("ptest %3, %2; cmovc %4, %0; cmovz %5, %1" : "+r"(cm), "+r"(cm2) : "x"(a), "x"(b), "r"(7), "r"(9) : "cc"); mixu((uint64_t)(cm * 1000 + cm2)); }
    printf("ptest cc     %016llx\n", (unsigned long long) H);
    /* flags dead after ptest: only the memory access remains (and it may fault) */
    H = 14695981039346656037ULL;
    for (int i = 0; i < 300; i++) { __m128i a = rv(); M = rv(); int z; __asm__ volatile("ptest %2, %1; xorl %0, %0" : "=r"(z) : "x"(a), "m"(M) : "cc"); mixu((uint64_t) z + 1); }
    printf("ptest dead   %016llx\n", (unsigned long long) H);
    /* a 16-byte ptest operand ending exactly at the page end, and the fault one byte further (the guest must see SIGSEGV, not the emulator die) */
    { __m128i a = _mm_set1_epi8(1); uint8_t *p = edge + 4096 - 16; memset(p, 3, 16); unsigned long f; __asm__ volatile("ptest (%2), %1; pushfq; popq %0" : "=r"(f) : "x"(a), "r"(p) : "cc", "memory"); printf("ptest edge   %lx\n", f & 0x8d5); }
    return 0;
}
