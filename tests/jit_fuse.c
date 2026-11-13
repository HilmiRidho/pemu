/* tests/jit_fuse.c - flag producers followed by jcc (fused in the JIT), flags dead / live at the exits (guest for tests/jit_embed.sh).
 * Every function runs one op + jcc over many operand pairs and prints a hash of the branch decision, the result and the flags seen afterwards.
 * Generated from the table below; interpreter, JIT and native must print the same lines. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
typedef unsigned long u64;
static const u64 V[] = { 0, 1, 2, 5, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffffUL, 0x80000000UL, 0xffffffffUL,
                         0x7fffffffffffffffUL, 0x8000000000000000UL, 0xffffffffffffffffUL, 0x123456789abcdef0UL, 0xfffffffffffffffbUL, 0x1000, 0xfffUL };
#define NV (sizeof V / sizeof V[0])
/* variants: 0 = flags dead at both exits, 1 = read at both, 2 = read only at the taken exit, 3 = only at the fall-through */
#define SAVEF "lea -256(%%rsp), %%rsp\n\t pushfq\n\t pop %%r11\n\t lea 256(%%rsp), %%rsp\n\t"
#define KILLF "xor %%r9d, %%r9d\n\t"          /* overwrites the flags: dead at this exit */
#define BODY(OP, CC, NT, TT) \
    asm volatile( \
        "mov 0(%%rdi), %%rax\n\t mov 8(%%rdi), %%rcx\n\t mov %%rcx, %%r8\n\t lea 64(%%rdi), %%rsi\n\t mov $1, %%edx\n\t" \
        "mov %%rax, 3(%%rsi,%%rdx,2)\n\t mov %%rax, 0(%%rsi)\n\t xor %%r10d, %%r10d\n\t xor %%r11d, %%r11d\n\t" \
        OP "\n\t j" #CC " 1f\n\t" NT "jmp 2f\n\t 1: mov $1, %%r10d\n\t" TT "2:\n\t" \
        "mov %%rax, 16(%%rdi)\n\t mov 3(%%rsi,%%rdx,2), %%r9\n\t mov %%r9, 24(%%rdi)\n\t mov %%r10, 32(%%rdi)\n\t mov %%r11, 40(%%rdi)\n\t mov 0(%%rsi), %%r9\n\t mov %%r9, 48(%%rdi)\n\t" \
        : : "D"(io) : "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "cc", "memory")

#define OPLIST(X) \
    X(cmp64_rr, "cmpq %%rcx, %%rax") \
    X(cmp64_ru, "cmpq %%r8, %%rax") \
    X(cmp64_ur, "cmpq %%rcx, %%r8") \
    X(cmp64_rm, "cmpq 3(%%rsi,%%rdx,2), %%rax") \
    X(cmp64_mr, "cmpq %%rcx, 3(%%rsi,%%rdx,2)") \
    X(cmp64_ri0, "cmpq $5, %%rax") \
    X(cmp64_mi0, "cmpq $5, 3(%%rsi,%%rdx,2)") \
    X(cmp64_ri1, "cmpq $-5, %%rax") \
    X(cmp64_mi1, "cmpq $-5, 3(%%rsi,%%rdx,2)") \
    X(cmp64_ri2, "cmpq $4096, %%rax") \
    X(cmp64_ri3, "cmpq $0x12345, %%rax") \
    X(cmp64_ri4, "cmpq $0xff, %%rax") \
    X(cmp64_ri5, "cmpq $0x7ffe, %%rax") \
    X(cmp64_ri6, "cmpq $0x55555555, %%rax") \
    X(cmp64_ri7, "cmpq $-256, %%rax") \
    X(cmp64_ri8, "cmpq $0x7ffff000, %%rax") \
    X(cmp64_ri9, "cmpq $-2, %%rax") \
    X(test64_rr, "testq %%rcx, %%rax") \
    X(test64_ur, "testq %%rcx, %%r8") \
    X(test64_rm, "testq 3(%%rsi,%%rdx,2), %%rax") \
    X(test64_mr, "testq %%rcx, 3(%%rsi,%%rdx,2)") \
    X(test64_ri0, "testq $5, %%rax") \
    X(test64_mi0, "testq $5, 3(%%rsi,%%rdx,2)") \
    X(test64_ri1, "testq $-5, %%rax") \
    X(test64_mi1, "testq $-5, 3(%%rsi,%%rdx,2)") \
    X(test64_ri2, "testq $4096, %%rax") \
    X(test64_ri3, "testq $0x12345, %%rax") \
    X(test64_ri4, "testq $0xff, %%rax") \
    X(test64_ri5, "testq $0x7ffe, %%rax") \
    X(test64_ri6, "testq $0x55555555, %%rax") \
    X(test64_ri7, "testq $-256, %%rax") \
    X(test64_ri8, "testq $0x7ffff000, %%rax") \
    X(test64_ri9, "testq $-2, %%rax") \
    X(sub64_rr, "subq %%rcx, %%rax") \
    X(sub64_ur, "subq %%rcx, %%r8") \
    X(sub64_rm, "subq 3(%%rsi,%%rdx,2), %%rax") \
    X(sub64_mr, "subq %%rcx, 3(%%rsi,%%rdx,2)") \
    X(sub64_ri0, "subq $5, %%rax") \
    X(sub64_mi0, "subq $5, 3(%%rsi,%%rdx,2)") \
    X(sub64_ri1, "subq $-5, %%rax") \
    X(sub64_mi1, "subq $-5, 3(%%rsi,%%rdx,2)") \
    X(sub64_ri2, "subq $4096, %%rax") \
    X(sub64_ri3, "subq $0x12345, %%rax") \
    X(sub64_ri4, "subq $0xff, %%rax") \
    X(sub64_ri5, "subq $0x7ffe, %%rax") \
    X(sub64_ri6, "subq $0x55555555, %%rax") \
    X(sub64_ri7, "subq $-256, %%rax") \
    X(sub64_ri8, "subq $0x7ffff000, %%rax") \
    X(sub64_ri9, "subq $-2, %%rax") \
    X(add64_rr, "addq %%rcx, %%rax") \
    X(add64_ur, "addq %%rcx, %%r8") \
    X(add64_rm, "addq 3(%%rsi,%%rdx,2), %%rax") \
    X(add64_mr, "addq %%rcx, 3(%%rsi,%%rdx,2)") \
    X(add64_ri0, "addq $5, %%rax") \
    X(add64_mi0, "addq $5, 3(%%rsi,%%rdx,2)") \
    X(add64_ri1, "addq $-5, %%rax") \
    X(add64_mi1, "addq $-5, 3(%%rsi,%%rdx,2)") \
    X(add64_ri2, "addq $4096, %%rax") \
    X(add64_ri3, "addq $0x12345, %%rax") \
    X(add64_ri4, "addq $0xff, %%rax") \
    X(add64_ri5, "addq $0x7ffe, %%rax") \
    X(add64_ri6, "addq $0x55555555, %%rax") \
    X(add64_ri7, "addq $-256, %%rax") \
    X(add64_ri8, "addq $0x7ffff000, %%rax") \
    X(add64_ri9, "addq $-2, %%rax") \
    X(and64_rr, "andq %%rcx, %%rax") \
    X(and64_ur, "andq %%rcx, %%r8") \
    X(and64_rm, "andq 3(%%rsi,%%rdx,2), %%rax") \
    X(and64_mr, "andq %%rcx, 3(%%rsi,%%rdx,2)") \
    X(and64_ri0, "andq $5, %%rax") \
    X(and64_ri1, "andq $-5, %%rax") \
    X(and64_ri2, "andq $4096, %%rax") \
    X(and64_ri3, "andq $0x12345, %%rax") \
    X(and64_ri4, "andq $0xff, %%rax") \
    X(and64_ri5, "andq $0x7ffe, %%rax") \
    X(and64_ri6, "andq $0x55555555, %%rax") \
    X(and64_ri7, "andq $-256, %%rax") \
    X(and64_ri8, "andq $0x7ffff000, %%rax") \
    X(and64_ri9, "andq $-2, %%rax") \
    X(or64_rr, "orq %%rcx, %%rax") \
    X(xor64_rr, "xorq %%rcx, %%rax") \
    X(test64_same, "testq %%rax, %%rax") \
    X(cmp64_same, "cmpq %%rax, %%rax") \
    X(cmp32_rr, "cmpl %%ecx, %%eax") \
    X(cmp32_ru, "cmpl %%r8d, %%eax") \
    X(cmp32_ur, "cmpl %%ecx, %%r8d") \
    X(cmp32_rm, "cmpl 3(%%rsi,%%rdx,2), %%eax") \
    X(cmp32_mr, "cmpl %%ecx, 3(%%rsi,%%rdx,2)") \
    X(cmp32_ri0, "cmpl $5, %%eax") \
    X(cmp32_mi0, "cmpl $5, 3(%%rsi,%%rdx,2)") \
    X(cmp32_ri1, "cmpl $-5, %%eax") \
    X(cmp32_mi1, "cmpl $-5, 3(%%rsi,%%rdx,2)") \
    X(cmp32_ri2, "cmpl $4096, %%eax") \
    X(cmp32_ri3, "cmpl $0x80000000, %%eax") \
    X(cmp32_ri4, "cmpl $0x7fff, %%eax") \
    X(cmp32_ri5, "cmpl $0xf0f0f0f0, %%eax") \
    X(cmp32_ri6, "cmpl $0x3f0, %%eax") \
    X(cmp32_ri7, "cmpl $-2, %%eax") \
    X(test32_rr, "testl %%ecx, %%eax") \
    X(test32_ur, "testl %%ecx, %%r8d") \
    X(test32_rm, "testl 3(%%rsi,%%rdx,2), %%eax") \
    X(test32_mr, "testl %%ecx, 3(%%rsi,%%rdx,2)") \
    X(test32_ri0, "testl $5, %%eax") \
    X(test32_mi0, "testl $5, 3(%%rsi,%%rdx,2)") \
    X(test32_ri1, "testl $-5, %%eax") \
    X(test32_mi1, "testl $-5, 3(%%rsi,%%rdx,2)") \
    X(test32_ri2, "testl $4096, %%eax") \
    X(test32_ri3, "testl $0x80000000, %%eax") \
    X(test32_ri4, "testl $0x7fff, %%eax") \
    X(test32_ri5, "testl $0xf0f0f0f0, %%eax") \
    X(test32_ri6, "testl $0x3f0, %%eax") \
    X(test32_ri7, "testl $-2, %%eax") \
    X(sub32_rr, "subl %%ecx, %%eax") \
    X(sub32_ur, "subl %%ecx, %%r8d") \
    X(sub32_rm, "subl 3(%%rsi,%%rdx,2), %%eax") \
    X(sub32_mr, "subl %%ecx, 3(%%rsi,%%rdx,2)") \
    X(sub32_ri0, "subl $5, %%eax") \
    X(sub32_mi0, "subl $5, 3(%%rsi,%%rdx,2)") \
    X(sub32_ri1, "subl $-5, %%eax") \
    X(sub32_mi1, "subl $-5, 3(%%rsi,%%rdx,2)") \
    X(sub32_ri2, "subl $4096, %%eax") \
    X(sub32_ri3, "subl $0x80000000, %%eax") \
    X(sub32_ri4, "subl $0x7fff, %%eax") \
    X(sub32_ri5, "subl $0xf0f0f0f0, %%eax") \
    X(sub32_ri6, "subl $0x3f0, %%eax") \
    X(sub32_ri7, "subl $-2, %%eax") \
    X(add32_rr, "addl %%ecx, %%eax") \
    X(add32_ur, "addl %%ecx, %%r8d") \
    X(add32_rm, "addl 3(%%rsi,%%rdx,2), %%eax") \
    X(add32_mr, "addl %%ecx, 3(%%rsi,%%rdx,2)") \
    X(add32_ri0, "addl $5, %%eax") \
    X(add32_mi0, "addl $5, 3(%%rsi,%%rdx,2)") \
    X(add32_ri1, "addl $-5, %%eax") \
    X(add32_mi1, "addl $-5, 3(%%rsi,%%rdx,2)") \
    X(add32_ri2, "addl $4096, %%eax") \
    X(add32_ri3, "addl $0x80000000, %%eax") \
    X(add32_ri4, "addl $0x7fff, %%eax") \
    X(add32_ri5, "addl $0xf0f0f0f0, %%eax") \
    X(add32_ri6, "addl $0x3f0, %%eax") \
    X(add32_ri7, "addl $-2, %%eax") \
    X(and32_rr, "andl %%ecx, %%eax") \
    X(and32_ur, "andl %%ecx, %%r8d") \
    X(and32_rm, "andl 3(%%rsi,%%rdx,2), %%eax") \
    X(and32_mr, "andl %%ecx, 3(%%rsi,%%rdx,2)") \
    X(and32_ri0, "andl $5, %%eax") \
    X(and32_ri1, "andl $-5, %%eax") \
    X(and32_ri2, "andl $4096, %%eax") \
    X(and32_ri3, "andl $0x80000000, %%eax") \
    X(and32_ri4, "andl $0x7fff, %%eax") \
    X(and32_ri5, "andl $0xf0f0f0f0, %%eax") \
    X(and32_ri6, "andl $0x3f0, %%eax") \
    X(and32_ri7, "andl $-2, %%eax") \
    X(or32_rr, "orl %%ecx, %%eax") \
    X(xor32_rr, "xorl %%ecx, %%eax") \
    X(test32_same, "testl %%eax, %%eax") \
    X(cmp32_same, "cmpl %%eax, %%eax") \
    X(cmp16_rr, "cmpw %%cx, %%ax") \
    X(cmp16_ru, "cmpw %%r8w, %%ax") \
    X(cmp16_ur, "cmpw %%cx, %%r8w") \
    X(cmp16_rm, "cmpw 3(%%rsi,%%rdx,2), %%ax") \
    X(cmp16_mr, "cmpw %%cx, 3(%%rsi,%%rdx,2)") \
    X(cmp16_ri0, "cmpw $5, %%ax") \
    X(cmp16_mi0, "cmpw $5, 3(%%rsi,%%rdx,2)") \
    X(cmp16_ri1, "cmpw $0x8000, %%ax") \
    X(cmp16_mi1, "cmpw $0x8000, 3(%%rsi,%%rdx,2)") \
    X(test16_rr, "testw %%cx, %%ax") \
    X(test16_ur, "testw %%cx, %%r8w") \
    X(test16_rm, "testw 3(%%rsi,%%rdx,2), %%ax") \
    X(test16_mr, "testw %%cx, 3(%%rsi,%%rdx,2)") \
    X(test16_ri0, "testw $5, %%ax") \
    X(test16_mi0, "testw $5, 3(%%rsi,%%rdx,2)") \
    X(test16_ri1, "testw $0x8000, %%ax") \
    X(test16_mi1, "testw $0x8000, 3(%%rsi,%%rdx,2)") \
    X(sub16_rr, "subw %%cx, %%ax") \
    X(sub16_ur, "subw %%cx, %%r8w") \
    X(sub16_rm, "subw 3(%%rsi,%%rdx,2), %%ax") \
    X(sub16_mr, "subw %%cx, 3(%%rsi,%%rdx,2)") \
    X(sub16_ri0, "subw $5, %%ax") \
    X(sub16_mi0, "subw $5, 3(%%rsi,%%rdx,2)") \
    X(sub16_ri1, "subw $0x8000, %%ax") \
    X(sub16_mi1, "subw $0x8000, 3(%%rsi,%%rdx,2)") \
    X(add16_rr, "addw %%cx, %%ax") \
    X(add16_ur, "addw %%cx, %%r8w") \
    X(add16_rm, "addw 3(%%rsi,%%rdx,2), %%ax") \
    X(add16_mr, "addw %%cx, 3(%%rsi,%%rdx,2)") \
    X(add16_ri0, "addw $5, %%ax") \
    X(add16_mi0, "addw $5, 3(%%rsi,%%rdx,2)") \
    X(add16_ri1, "addw $0x8000, %%ax") \
    X(add16_mi1, "addw $0x8000, 3(%%rsi,%%rdx,2)") \
    X(and16_rr, "andw %%cx, %%ax") \
    X(and16_ur, "andw %%cx, %%r8w") \
    X(and16_rm, "andw 3(%%rsi,%%rdx,2), %%ax") \
    X(and16_mr, "andw %%cx, 3(%%rsi,%%rdx,2)") \
    X(and16_ri0, "andw $5, %%ax") \
    X(and16_ri1, "andw $0x8000, %%ax") \
    X(or16_rr, "orw %%cx, %%ax") \
    X(xor16_rr, "xorw %%cx, %%ax") \
    X(test16_same, "testw %%ax, %%ax") \
    X(cmp16_same, "cmpw %%ax, %%ax") \
    X(cmp8_rr, "cmpb %%cl, %%al") \
    X(cmp8_ru, "cmpb %%r8b, %%al") \
    X(cmp8_ur, "cmpb %%cl, %%r8b") \
    X(cmp8_rm, "cmpb 3(%%rsi,%%rdx,2), %%al") \
    X(cmp8_mr, "cmpb %%cl, 3(%%rsi,%%rdx,2)") \
    X(cmp8_ri0, "cmpb $5, %%al") \
    X(cmp8_mi0, "cmpb $5, 3(%%rsi,%%rdx,2)") \
    X(cmp8_ri1, "cmpb $0x80, %%al") \
    X(cmp8_mi1, "cmpb $0x80, 3(%%rsi,%%rdx,2)") \
    X(test8_rr, "testb %%cl, %%al") \
    X(test8_ur, "testb %%cl, %%r8b") \
    X(test8_rm, "testb 3(%%rsi,%%rdx,2), %%al") \
    X(test8_mr, "testb %%cl, 3(%%rsi,%%rdx,2)") \
    X(test8_ri0, "testb $5, %%al") \
    X(test8_mi0, "testb $5, 3(%%rsi,%%rdx,2)") \
    X(test8_ri1, "testb $0x80, %%al") \
    X(test8_mi1, "testb $0x80, 3(%%rsi,%%rdx,2)") \
    X(sub8_rr, "subb %%cl, %%al") \
    X(sub8_ur, "subb %%cl, %%r8b") \
    X(sub8_rm, "subb 3(%%rsi,%%rdx,2), %%al") \
    X(sub8_mr, "subb %%cl, 3(%%rsi,%%rdx,2)") \
    X(sub8_ri0, "subb $5, %%al") \
    X(sub8_mi0, "subb $5, 3(%%rsi,%%rdx,2)") \
    X(sub8_ri1, "subb $0x80, %%al") \
    X(sub8_mi1, "subb $0x80, 3(%%rsi,%%rdx,2)") \
    X(add8_rr, "addb %%cl, %%al") \
    X(add8_ur, "addb %%cl, %%r8b") \
    X(add8_rm, "addb 3(%%rsi,%%rdx,2), %%al") \
    X(add8_mr, "addb %%cl, 3(%%rsi,%%rdx,2)") \
    X(add8_ri0, "addb $5, %%al") \
    X(add8_mi0, "addb $5, 3(%%rsi,%%rdx,2)") \
    X(add8_ri1, "addb $0x80, %%al") \
    X(add8_mi1, "addb $0x80, 3(%%rsi,%%rdx,2)") \
    X(and8_rr, "andb %%cl, %%al") \
    X(and8_ur, "andb %%cl, %%r8b") \
    X(and8_rm, "andb 3(%%rsi,%%rdx,2), %%al") \
    X(and8_mr, "andb %%cl, 3(%%rsi,%%rdx,2)") \
    X(and8_ri0, "andb $5, %%al") \
    X(and8_ri1, "andb $0x80, %%al") \
    X(or8_rr, "orb %%cl, %%al") \
    X(xor8_rr, "xorb %%cl, %%al") \
    X(test8_same, "testb %%al, %%al") \
    X(cmp8_same, "cmpb %%al, %%al") \
    X(dead_and64_i0, "andq $5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i1, "andq $-5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i2, "andq $4096, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i3, "andq $0x12345, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i4, "andq $0xff, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i5, "andq $0x7ffe, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i6, "andq $0x55555555, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i7, "andq $-256, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i8, "andq $0x7ffff000, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and64_i9, "andq $-2, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i0, "orq $5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i1, "orq $-5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i2, "orq $4096, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i3, "orq $0x12345, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i4, "orq $0xff, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i5, "orq $0x7ffe, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i6, "orq $0x55555555, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i7, "orq $-256, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i8, "orq $0x7ffff000, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_or64_i9, "orq $-2, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i0, "xorq $5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i1, "xorq $-5, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i2, "xorq $4096, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i3, "xorq $0x12345, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i4, "xorq $0xff, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i5, "xorq $0x7ffe, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i6, "xorq $0x55555555, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i7, "xorq $-256, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i8, "xorq $0x7ffff000, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_xor64_i9, "xorq $-2, %%rax\n\t cmpq %%rcx, %%rcx") \
    X(dead_and32_i0, "andl $5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i1, "andl $-5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i2, "andl $4096, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i3, "andl $0x80000000, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i4, "andl $0x7fff, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i5, "andl $0xf0f0f0f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i6, "andl $0x3f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_and32_i7, "andl $-2, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i0, "orl $5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i1, "orl $-5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i2, "orl $4096, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i3, "orl $0x80000000, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i4, "orl $0x7fff, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i5, "orl $0xf0f0f0f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i6, "orl $0x3f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_or32_i7, "orl $-2, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i0, "xorl $5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i1, "xorl $-5, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i2, "xorl $4096, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i3, "xorl $0x80000000, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i4, "xorl $0x7fff, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i5, "xorl $0xf0f0f0f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i6, "xorl $0x3f0, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(dead_xor32_i7, "xorl $-2, %%eax\n\t cmpl %%ecx, %%ecx") \
    X(cmp8_hi, "cmpb %%ch, %%al") \
    X(cmp8_hi2, "cmpb %%al, %%ch") \
    X(test8_hi, "testb %%ah, %%cl") \
    X(cmpb_rm0, "cmpb $0, (%%rsi)") \

#define CCLIST(X, N, OP, V) \
    X(N, OP, o, V) \
    X(N, OP, no, V) \
    X(N, OP, b, V) \
    X(N, OP, ae, V) \
    X(N, OP, e, V) \
    X(N, OP, ne, V) \
    X(N, OP, be, V) \
    X(N, OP, a, V) \
    X(N, OP, s, V) \
    X(N, OP, ns, V) \
    X(N, OP, p, V) \
    X(N, OP, np, V) \
    X(N, OP, l, V) \
    X(N, OP, ge, V) \
    X(N, OP, le, V) \
    X(N, OP, g, V)

#define FN(N, OP, CC, VAR) static __attribute__((noinline)) void f_##N##_##CC##_##VAR(u64 *io) { \
    if (VAR == 0) { BODY(OP, CC, KILLF, KILLF); } else if (VAR == 1) { BODY(OP, CC, SAVEF, SAVEF); } \
    else if (VAR == 2) { BODY(OP, CC, KILLF, SAVEF); } else { BODY(OP, CC, SAVEF, KILLF); } }
#define ONE(N, OP, CC, VAR) FN(N, OP, CC, VAR)
#define PERCC(N, OP) CCLIST(ONE, N, OP, 0) CCLIST(ONE, N, OP, 1) CCLIST(ONE, N, OP, 2) CCLIST(ONE, N, OP, 3)
OPLIST(PERCC)
typedef void (*fn_t)(u64 *);
typedef struct { const char *name; fn_t f; } Ent;
#define ENT(N, OP, CC, VAR) { #N "." #CC "." #VAR, f_##N##_##CC##_##VAR },
#define PERCC2(N, OP) CCLIST(ENT, N, OP, 0) CCLIST(ENT, N, OP, 1) CCLIST(ENT, N, OP, 2) CCLIST(ENT, N, OP, 3)
static const Ent tab[] = { OPLIST(PERCC2) };
int main(int argc, char **argv) {
    u64 buf[16] __attribute__((aligned(16)));
    const char *only = argc > 1 ? argv[1] : NULL;
    for (unsigned k = 0; k < sizeof tab / sizeof tab[0]; k++) {
        if (only && strcmp(only, tab[k].name)) continue;
        u64 h = 1469598103934665603UL;
        for (unsigned i = 0; i < NV; i++) for (unsigned j = 0; j < NV; j++) {
            memset(buf, 0, sizeof buf); buf[0] = V[i]; buf[1] = V[j];
            tab[k].f(buf);
            u64 r[5] = { buf[2], buf[3], buf[4], buf[5] & 0x8c5, buf[6] };
            if (only) printf("  a=%016lx b=%016lx rax=%016lx mem=%016lx br=%lu fl=%03lx m0=%016lx\n", V[i], V[j], r[0], r[1], r[2], r[3], r[4]);
            for (int q = 0; q < 5; q++) { h ^= r[q]; h *= 1099511628211UL; }
        }
        printf("%s %016lx\n", tab[k].name, h);
    }
    return 0;
}
