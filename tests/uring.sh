#!/bin/sh
# io_uring must not bypass the rootfs: under `pemu_x64 -r ROOTFS` an io_uring OPENAT would open HOST files (the kernel resolves the path
# from the ring; pemu never sees it).  Builds tests/uring_guest.c (static x86_64, plain gcc) and runs it
#   - without a rootfs: both opens read the host file (the ring is passed through; there is nothing to bypass);
#   - inside a rootfs that holds a different file at the same path: open() reads the rootfs's copy, io_uring_setup must fail with
#     ENOSYS, and the host's file must not be readable through the ring.
# Skips (exit 0) when the host is not x86_64, gcc cannot link statically, or the kernel offers no io_uring.  usage: tests/uring.sh [BUILD_DIR]
cd "$(dirname "$0")/.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "uring: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/uring_guest.c 2>/dev/null || { echo "uring: cannot build a static guest, skipping"; exit 0; }
P=$T/marker                                   # one absolute path, two different files behind it
mkdir -p "$T/rf$T"; echo HOST-FILE > "$P"; echo GUEST-ROOTFS-FILE > "$T/rf$P"; cp "$T/g" "$T/rf/g"
"$T/g" "$P" > "$T/native" 2>&1 || { echo "uring: no io_uring on this kernel, skipping"; exit 0; }
rc=0
out=$(timeout 60 "$B/pemu_x64" "$T/g" "$P" 2>&1)
echo "$out" | grep -q "io_uring *: opened 'HOST-FILE'" || { echo "uring: without a rootfs the ring should work: $out"; rc=1; }
out=$(timeout 60 "$B/pemu_x64" -r "$T/rf" --no-default-binds /g "$P" 2>&1)
echo "$out" | grep -q "open() *: opened 'GUEST-ROOTFS-FILE'" || { echo "uring: open() must read the rootfs file: $out"; rc=1; }
echo "$out" | grep -q "HOST-FILE" && { echo "uring: io_uring reached the HOST file system from inside a rootfs: $out"; rc=1; }
echo "$out" | grep -q "errno 38" || { echo "uring: io_uring_setup must fail with ENOSYS inside a rootfs: $out"; rc=1; }
[ $rc = 0 ] && echo "uring pemu_x64: ok" || echo "uring pemu_x64: FAILED"
exit $rc
