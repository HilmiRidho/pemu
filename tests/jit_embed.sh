#!/bin/sh
# fuse: cmp/test/sub/add/and + jcc over every condition, operand form and size, with the flags dead or live at the exits (tests/jit_fuse.c, 19200 cases);
# vec: palignr, pmovmskb, the pxor zero idiom (tests/jit_vec.c).
# JIT tests for translations that have no AArch64 hardware in the loop: the 66 0F 38 instructions (tests/jit_sse4_*.c), imul / rol / ror with live flags (tests/jit_alu.c)
# and cvttss2si / cvttsd2si (tests/jit_cvt.c).
# Each guest is run natively, under the interpreter (-nojit) and under the AArch64 JIT, whose generated code is executed by the interpreter of pemu.c (-DPEMU_JIT_EMBED,
# see tests/embed_hook.inc.h). Interpreter and JIT must agree on every line; the native run only on the lines without a leading '~' (flags that the SDM calls undefined).
# x86_64 host with gcc and a static libc.  usage: tests/jit_embed.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/.."
[ "$(uname -m)" = x86_64 ] || { echo "jit_embed: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
echo 'int main(void){return 0;}' > "$T/s.c"
$CC -static -o "$T/s" "$T/s.c" 2>/dev/null || { echo "jit_embed: no static libc, skipping"; exit 0; }
$CC -O2 -w -DPEMU_EMBED -Dmain=pemu_unused_main -c pemu.c -o "$T/a64.o"
$CC -O2 -w -DPEMU_JIT_EMBED -c pemu_x64.c -o "$T/x64.o"
$CC -o "$T/pemu_x64e" "$T/x64.o" "$T/a64.o" -lm -lpthread
rc=0
for g in sse4_reg sse4_mem alu cvt fuse vec; do
    case $g in sse4_*|vec) f=tests/jit_$g.c; fl=-msse4.2 ;; *) f=tests/jit_$g.c; fl= ;; esac
    $CC -O1 $fl -static -o "$T/g_$g" "$f" -lm
    "$T/g_$g" | grep -v '^~' > "$T/native-$g"
    timeout 120 "$T/pemu_x64e" -nojit "$T/g_$g" > "$T/nojit-$g" 2>&1 || true
    PEMU_JIT_STATS=1 timeout 300 "$T/pemu_x64e" "$T/g_$g" > "$T/jit-$g" 2> "$T/stats-$g" || true
    if diff -u "$T/nojit-$g" "$T/jit-$g" > "$T/diff"; then echo "jit_embed $g (jit = interpreter): ok"; else cat "$T/diff"; echo "jit_embed $g (jit = interpreter): FAILED"; rc=1; fi
    for v in nojit jit; do
        grep -v '^~' "$T/$v-$g" > "$T/$v-$g.def"
        if diff -u "$T/native-$g" "$T/$v-$g.def" > "$T/diff"; then echo "jit_embed $g ($v = native): ok"; else cat "$T/diff"; echo "jit_embed $g ($v = native): FAILED"; rc=1; fi
    done
done
# what the JIT translates must not run through the interpreter any more (the register-operand guest uses no memory form, so even pmovsxbq/pmovzxbq are translated)
if ! grep -q 'third opcode byte, executed fallbacks' "$T/stats-sse4_reg"; then
    echo "jit_embed: no per-opcode fallback statistics in the JIT output: FAILED"; rc=1
elif grep -E 'executed fallbacks.* 38/(17|1c|1d|1e|2[0-5]|29|2b|3[0-5]|3[7-9a-f]|40) x' "$T/stats-sse4_reg" > /dev/null; then
    grep 'executed fallbacks' "$T/stats-sse4_reg"; echo "jit_embed: a translated 0F 38 instruction still falls back: FAILED"; rc=1
else echo "jit_embed: translated 0F 38 instructions no longer fall back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-alu"; then echo "jit_embed: no fallback statistics for the alu guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-alu" | grep -E ' (0f af|69|6b|c1|d1) x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-alu"; echo "jit_embed: imul / rol / ror still fall back: FAILED"; rc=1
else echo "jit_embed: imul / rol / ror with live flags no longer fall back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-cvt"; then echo "jit_embed: no fallback statistics for the cvt guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-cvt" | grep -E ' 0f 2c x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-cvt"; echo "jit_embed: cvttss2si / cvttsd2si still fall back: FAILED"; rc=1
else echo "jit_embed: cvttss2si / cvttsd2si no longer fall back: ok"; fi
if ! grep -q 'flag producers fused' "$T/stats-fuse" || grep -q ' 0 flag producers fused' "$T/stats-fuse"; then echo "jit_embed: no flag producer was fused in the fuse guest: FAILED"; rc=1; else echo "jit_embed: cmp/test/sub/add + jcc are fused: ok"; fi
if grep 'interpreter fallbacks executed' "$T/stats-vec" | grep -E ' 0f 3a x' > /dev/null; then echo "jit_embed: palignr still falls back: FAILED"; rc=1; else echo "jit_embed: palignr no longer falls back: ok"; fi
exit $rc
