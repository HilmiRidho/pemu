#!/bin/sh
# Build pemu and pemu_x64, check the CPU core in isolation, then run the tests. Run from anywhere; the script cd's to its own directory.
#
# Layout (modelled on qemu-user): pemu.c / pemu_x64.c are entry files that include target/<arch>/ (guest CPU),
# tcg/<host>/ (JIT) and linux-user/<arch>/ (syscalls, signals, ELF, main). linux-user/pemu_root.h (parts in linux-user/proot/) is the built-in proot.
#
# Outputs (in $OUT):
#   pemu      AArch64 guest on an x86_64 host (pemu.c);  x86_64 guest on an aarch64 host (Termux/Android, pemu_x64.c)
#   pemu_x64  x86_64 guest (useful on an aarch64 host; also builds on x86_64 for testing)
#
# Version: read from ./VERSION and compiled in as PEMU_RELEASE.
# Environment: CC (compiler), ARCH (target arch, default uname -m), OUT (output dir, default ./build),
#              SKIP_TESTS=1 (cross builds: the test binaries cannot run on the build machine)
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
ARCH=${ARCH:-$(uname -m)}
OUT=${OUT:-build}
VERDEF="-DPEMU_RELEASE=\"$(tr -d ' \n' < VERSION)\""   # single source of truth: ./VERSION
mkdir -p "$OUT"
# bionic (Android) has pthread inside libc and recent NDKs ship no libpthread to link against
case "$($CC -dumpmachine 2>/dev/null)" in
  *android*) PTH="" ;;
  *)         PTH="-lpthread" ;;
esac
case "$ARCH" in
  x86_64|amd64)  $CC -O2 $VERDEF -o "$OUT/pemu" pemu.c -lm ;;
  aarch64|arm64) $CC -O2 $VERDEF -o "$OUT/pemu" pemu_x64.c -lm $PTH ;;
  *) echo "build.sh: unsupported ARCH '$ARCH'" >&2; exit 1 ;;
esac
$CC -O2 $VERDEF -o "$OUT/pemu_x64" pemu_x64.c -lm $PTH
# the CPU core (target/ + tcg/) must not depend on linux-user/: compile it alone. Anything it needs from the OS goes through
# the PemuOS table (target/<guest>/cpu.inc.h), so a direct call to a linux-user/ function fails here (see tests/check_core_*.c)
CHK="-fsyntax-only -Werror=implicit-function-declaration -I."
$CC $CHK tests/check_core_x86_64.c
case "$ARCH" in x86_64|amd64) $CC $CHK -DPEMU_JIT_EMBED tests/check_core_x86_64.c; $CC $CHK tests/check_core_aarch64.c ;; esac
echo "core check: target/ + tcg/ compile without linux-user/"
# tests (host-native, no guest binaries needed)
if [ -z "$SKIP_TESTS" ]; then
  $CC -O0 -Wno-unused-function -Ilinux-user -o "$OUT/test_pemu_root" tests/test_pemu_root.c $PTH
  "$OUT/test_pemu_root"
  $CC -O0 -Wno-unused-function -Ilinux-user -o "$OUT/test_proot_compat" tests/test_proot_compat.c $PTH
  "$OUT/test_proot_compat" 2>/dev/null
  tests/e2e.sh "$OUT"        # runs a freestanding AArch64 / x86_64 guest under the emulators (x86_64 host, needs clang + lld)
fi
