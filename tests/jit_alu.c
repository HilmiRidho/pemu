/* tests/jit_alu.c - guest for tests/jit_alu.sh: imul r, r/m[, imm] and rol/ror r/m32|64, imm8 with the flags used afterwards. SPDX-License-Identifier: Apache-2.0
 * Lines starting with '~' hold flags that the SDM calls undefined on a real CPU (SF/ZF/AF/PF after imul, OF after a rotate by more than 1): the interpreter and the JIT
 * must agree on them, a real CPU may not, so the script leaves them out of the comparison with the native run. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint64_t seed = 0x2545F4914F6CDD1DULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static uint64_t val(void) {
    static const uint64_t e[] = { 0, 1, 2, 3, ~0ULL, ~0ULL - 1, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x100000000ULL, 0xb504f333ULL, 0xb504f334ULL, 0x3037000500ULL, 0x5555555555555555ULL };
    uint64_t v = rnd(); return (rnd() % 3 == 0) ? e[rnd() % (sizeof e / sizeof e[0])] : (rnd() % 2 ? v : v >> (rnd() % 64));
}
static uint64_t H[4];
static void mix(int h, uint64_t v) { H[h] = (H[h] ^ v) * 0x100000001b3ULL; H[h] ^= H[h] >> 29; }
static void start(void) { for (int i = 0; i < 4; i++) H[i] = 14695981039346656037ULL; }
static void emit(const char *nm) { printf("%-14s %016llx\n~%-13s %016llx\n", nm, (unsigned long long) H[0], nm, (unsigned long long) H[1]); }
#define PRE "movl $0x7fffffff, %%eax; addl $1, %%eax; "          /* sets SF OF AF PF and clears ZF CF: the flags imul / rol must keep or replace */
#define FL "pushfq; popq %1"
#define IMUL_CASE(nm, T, ins, ...) do { start(); for (int i = 0; i < 1500; i++) { T a = (T) val(), b = (T) val(); unsigned long f; \
    __asm__ volatile(PRE ins " ; " FL : "+r"(a), "=r"(f) : "r"(b) __VA_ARGS__ : "cc", "rax"); mix(0, (uint64_t) a); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit(nm); } while (0)
static __attribute__((aligned(16))) uint64_t M;
int main(void) {
    IMUL_CASE("imul r32,r32", uint32_t, "imull %2, %0");
    IMUL_CASE("imul r64,r64", uint64_t, "imulq %2, %0");
    /* memory source */
    start(); for (int i = 0; i < 1500; i++) { uint32_t a = (uint32_t) val(); M = val(); unsigned long f; __asm__ volatile(PRE "imull %2, %0; " FL : "+r"(a), "=r"(f) : "m"(M) : "cc", "rax"); mix(0, a); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit("imul r32,m32");
    start(); for (int i = 0; i < 1500; i++) { uint64_t a = val(); M = val(); unsigned long f; __asm__ volatile(PRE "imulq %2, %0; " FL : "+r"(a), "=r"(f) : "m"(M) : "cc", "rax"); mix(0, a); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit("imul r64,m64");
    /* three-operand forms (8-bit and 32-bit immediates) */
#define IMUL3(nm, T, ins, imm) do { start(); for (int i = 0; i < 1500; i++) { T s = (T) val(), d; unsigned long f; \
    __asm__ volatile(PRE ins " $" #imm ", %2, %0; " FL : "=r"(d), "=r"(f) : "r"(s) : "cc", "rax"); mix(0, (uint64_t) d); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit(nm); } while (0)
    IMUL3("imul32 imm8 7", uint32_t, "imull", 7); IMUL3("imul32 imm8 -3", uint32_t, "imull", -3); IMUL3("imul32 imm32", uint32_t, "imull", 100000);
    IMUL3("imul64 imm8 -128", uint64_t, "imulq", -128); IMUL3("imul64 imm32", uint64_t, "imulq", 0x7fffffff); IMUL3("imul64 imm32 n", uint64_t, "imulq", -2000000000);
    /* every condition after imul */
    start(); for (int i = 0; i < 1500; i++) { uint64_t a = val(), b = val(); unsigned char c[6]; int jc = 0;
        __asm__ volatile("imulq %6, %0; seto %1; setno %2; setc %3; setnc %4; setb %5" : "+r"(a), "=m"(c[0]), "=m"(c[1]), "=m"(c[2]), "=m"(c[3]), "=m"(c[4]) : "r"(b) : "cc");
        mix(0, c[0] | c[1] << 1 | c[2] << 2 | c[3] << 3 | c[4] << 4); mix(0, a);
        uint32_t x = (uint32_t) val(), y = (uint32_t) val();
        __asm__ volatile("imull %2, %1; jo 1f; addl $1, %0; 1: jc 2f; addl $2, %0; 2: jnc 3f; addl $4, %0; 3: jno 4f; addl $8, %0; 4:" : "+r"(jc), "+r"(x) : "r"(y) : "cc"); mix(0, (uint64_t) jc + x);
        int m1 = 10, m2 = 20; uint64_t p = val(), q = val(); __asm__ volatile("imulq %4, %3; cmovo %5, %0; cmovc %6, %1" : "+r"(m1), "+r"(m2), "+r"(p) , "+r"(q) : "r"(p), "r"(1), "r"(2) : "cc");   /* (operands as written: q = q * p) */
        mix(0, (uint64_t)(m1 * 100 + m2)); mix(0, q); }
    emit("imul cc");
    /* flags dead afterwards: the old translation */
    start(); for (int i = 0; i < 1500; i++) { uint64_t a = val(), b = val(); uint32_t z; __asm__ volatile("imulq %2, %0; xorl %1, %1" : "+r"(a), "=r"(z) : "r"(b) : "cc"); mix(0, a + z); } emit("imul dead");
    /* rol / ror by an immediate */
#define ROT(nm, T, ins, n) do { start(); for (int i = 0; i < 1000; i++) { T a = (T) val(); unsigned long f; \
    __asm__ volatile(PRE ins " $" #n ", %0; " FL : "+r"(a), "=r"(f) : : "cc", "rax"); mix(0, (uint64_t) a); mix(0, f & ((n == 1) ? 0x801 : 0x001)); mix(1, f & 0x8d5); } emit(nm); } while (0)
    ROT("rol32 1", uint32_t, "roll", 1); ROT("rol32 3", uint32_t, "roll", 3); ROT("rol32 13", uint32_t, "roll", 13); ROT("rol32 31", uint32_t, "roll", 31);
    ROT("ror32 1", uint32_t, "rorl", 1); ROT("ror32 5", uint32_t, "rorl", 5); ROT("ror32 17", uint32_t, "rorl", 17); ROT("ror32 31", uint32_t, "rorl", 31);
    ROT("rol64 1", uint64_t, "rolq", 1); ROT("rol64 7", uint64_t, "rolq", 7); ROT("rol64 33", uint64_t, "rolq", 33); ROT("rol64 63", uint64_t, "rolq", 63);
    ROT("ror64 1", uint64_t, "rorq", 1); ROT("ror64 11", uint64_t, "rorq", 11); ROT("ror64 32", uint64_t, "rorq", 32); ROT("ror64 63", uint64_t, "rorq", 63);
    /* memory destination, and the flag consumers */
    start(); for (int i = 0; i < 1000; i++) { M = val(); unsigned long f; __asm__ volatile(PRE "rolq $9, %2; " FL : "=m"(M), "=r"(f) : "m"(M) : "cc", "rax"); mix(0, M); mix(0, f & 0x001); mix(1, f & 0x8d5); } emit("rol64 m");
    start(); for (int i = 0; i < 1000; i++) { M = val(); unsigned long f; __asm__ volatile(PRE "rorl $5, %2; " FL : "=m"(M), "=r"(f) : "m"(M) : "cc", "rax"); mix(0, M); mix(0, f & 0x001); mix(1, f & 0x8d5); } emit("ror32 m");
    start(); for (int i = 0; i < 1500; i++) { uint64_t a = val(); unsigned char c[3]; int jc = 0; uint32_t x = (uint32_t) val();
        __asm__ volatile("rolq $5, %0; setc %1; setnc %2; setb %3" : "+r"(a), "=m"(c[0]), "=m"(c[1]), "=m"(c[2]) :: "cc"); mix(0, a); mix(0, c[0] | c[1] << 1 | c[2] << 2);
        __asm__ volatile("rorl $3, %1; jc 1f; addl $1, %0; 1: jnc 2f; addl $2, %0; 2:" : "+r"(jc), "+r"(x) :: "cc"); mix(0, (uint64_t) jc + x); }
    emit("rot cc");
    /* rotate with the flags dead: the old translation; rotate by a count of 0 mod 32 must not touch the flags */
    start(); for (int i = 0; i < 1000; i++) { uint64_t a = val(); uint32_t z; __asm__ volatile("rolq $12, %0; xorl %1, %1" : "+r"(a), "=r"(z) :: "cc"); mix(0, a + z); } emit("rot dead");
    return 0;
}
