/* tests/jit_cvt.c - guest for tests/jit_embed.sh: cvttss2si / cvttsd2si (r32 and r64, register and memory source) at the edges of the integer ranges; the flags must stay as they were. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
static uint64_t seed = 0x1234567887654321ULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static double dval(void) {
    static const double e[] = { 0.0, -0.0, 0.5, -0.5, 0.99999, -0.99999, 1.0, 1.9, -1.9, 2147483646.5, 2147483647.0, 2147483647.9, 2147483648.0, 2147483649.0, -2147483647.9, -2147483648.0, -2147483648.5, -2147483648.9999, -2147483649.0,
        4294967295.0, 4294967296.0, 9007199254740992.0, 9223372036854774784.0, 9223372036854775807.0, 9223372036854775808.0, -9223372036854775808.0, -9223372036854777856.0, 1e18, -1e18, 1e300, -1e300, 1e-300, INFINITY, -INFINITY, NAN, -NAN };
    switch (rnd() % 4) { case 0: case 1: return e[rnd() % (sizeof e / sizeof e[0])]; case 2: return (double)(int64_t) rnd() / (double)(1 + rnd() % 100000); default: { uint64_t b = rnd(); double d; memcpy(&d, &b, 8); return d; } }
}
static uint64_t H;
static void mix(uint64_t v) { H = (H ^ v) * 0x100000001b3ULL; H ^= H >> 29; }
#define PRE "movl $0x7fffffff, %%eax; addl $1, %%eax; "
static __attribute__((aligned(16))) double MD; static __attribute__((aligned(16))) float MF;
int main(void) {
    for (int k = 0; k < 8; k++) {
        H = 14695981039346656037ULL; const char *nm = "";
        for (int i = 0; i < 3000; i++) {
            double d = dval(); float f = (float) d; if (rnd() % 5 == 0) { uint32_t b = (uint32_t) rnd(); memcpy(&f, &b, 4); } unsigned long fl; uint64_t r;
            switch (k) {
            case 0: nm = "cvttsd2si r32,x"; { uint32_t x; __asm__ volatile(PRE "cvttsd2si %2, %0; pushfq; popq %1" : "=r"(x), "=r"(fl) : "x"(d) : "cc", "rax"); r = x; } break;
            case 1: nm = "cvttsd2si r64,x"; __asm__ volatile(PRE "cvttsd2si %2, %0; pushfq; popq %1" : "=r"(r), "=r"(fl) : "x"(d) : "cc", "rax"); break;
            case 2: nm = "cvttss2si r32,x"; { uint32_t x; __asm__ volatile(PRE "cvttss2si %2, %0; pushfq; popq %1" : "=r"(x), "=r"(fl) : "x"(f) : "cc", "rax"); r = x; } break;
            case 3: nm = "cvttss2si r64,x"; __asm__ volatile(PRE "cvttss2si %2, %0; pushfq; popq %1" : "=r"(r), "=r"(fl) : "x"(f) : "cc", "rax"); break;
            case 4: nm = "cvttsd2si r32,m64"; MD = d; { uint32_t x; __asm__ volatile(PRE "cvttsd2si %2, %0; pushfq; popq %1" : "=r"(x), "=r"(fl) : "m"(MD) : "cc", "rax"); r = x; } break;
            case 5: nm = "cvttsd2si r64,m64"; MD = d; __asm__ volatile(PRE "cvttsd2si %2, %0; pushfq; popq %1" : "=r"(r), "=r"(fl) : "m"(MD) : "cc", "rax"); break;
            case 6: nm = "cvttss2si r32,m32"; MF = f; { uint32_t x; __asm__ volatile(PRE "cvttss2si %2, %0; pushfq; popq %1" : "=r"(x), "=r"(fl) : "m"(MF) : "cc", "rax"); r = x; } break;
            default: nm = "cvttss2si r64,m32"; MF = f; __asm__ volatile(PRE "cvttss2si %2, %0; pushfq; popq %1" : "=r"(r), "=r"(fl) : "m"(MF) : "cc", "rax"); break;
            }
            mix(r); mix(fl & 0x8d5);
        }
        printf("%-18s %016llx\n", nm, (unsigned long long) H);
    }
    /* the result feeds a compare and a branch right away, and the flags are dead afterwards */
    H = 14695981039346656037ULL;
    for (int i = 0; i < 3000; i++) { double d = dval(); int32_t r; int n = 0; __asm__ volatile("cvttsd2si %1, %0" : "=r"(r) : "x"(d)); if (r < 0) n += 1; if (r == INT32_MIN) n += 2; mix((uint64_t) n + ((uint64_t)(uint32_t) r << 8)); }
    printf("%-18s %016llx\n", "cvtt then cmp", (unsigned long long) H);
    return 0;
}
