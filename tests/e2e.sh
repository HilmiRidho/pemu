#!/bin/sh
# End-to-end test: runs one freestanding guest (tests/e2e_guest.c) under the emulators on an x86_64 host.
#   AArch64 guest under `pemu`, x86_64 guest under `pemu_x64`. Needs clang + lld.  usage: tests/e2e.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "e2e: x86_64 host required, skipping"; exit 0; }
command -v clang >/dev/null && command -v ld.lld >/dev/null || { echo "e2e: clang/lld missing, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
run() {   # target emulator
    rf=$T/rf-$2; mkdir -p "$rf/etc" "$rf/tmp" "$rf/bin"
    clang --target=$1 -O1 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -fuse-ld=lld -o "$rf/bin/guest" tests/e2e_guest.c
    printf 'welcome\n' > "$rf/etc/motd"; ln -s motd "$rf/etc/link"
    PEMU_FAKE_PROC=1 timeout 60 "$B/$2" login "$rf" -w / /bin/guest > "$T/out-$2" 2>&1 || true
    # the fake /proc/stat is read as 5 bytes "cpu  "
    if diff -u tests/e2e.expected "$T/out-$2"; then echo "e2e $2 ($1): ok"; else echo "e2e $2 ($1): FAILED"; return 1; fi
}
rc=0
run aarch64-linux-gnu pemu || rc=1
run x86_64-linux-gnu pemu_x64 || rc=1
exit $rc
