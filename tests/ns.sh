#!/bin/sh
# Namespace and handle calls must not escape the emulated view (x86_64 guest under pemu_x64, x86_64 host, plain gcc):
#   clone(CLONE_NEWUSER) used to "succeed" as a plain fork (a sandbox tool would believe it is isolated), unshare / setns went to the
#   host, name_to_handle_at took the path as a HOST path.  They must now fail with EPERM and resolve the path in the rootfs.  The refusal
#   table (linux-user/proot/deny.inc.h) applies with or without a rootfs: unshare(CLONE_NEW*) is EPERM either way, and so is clone with a
#   namespace flag (as clone3 always was): pemu cannot give the namespace, and a silent plain fork is worse than an error.
#   Both hooks share that table; tests/deny.sh covers the rest of it (AArch64 guest, mount API, ptrace ...).
# Skips (exit 0) when the host is not x86_64 or gcc cannot link statically.  usage: tests/ns.sh [BUILD_DIR]
cd "$(dirname "$0")/.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "ns: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/ns_guest.c 2>/dev/null || { echo "ns: cannot build a static guest, skipping"; exit 0; }
P=$T/only-in-rootfs
mkdir -p "$T/rf$T"; echo x > "$T/rf$P"; cp "$T/g" "$T/rf/g"
rc=0
out=$(timeout 60 "$B/pemu_x64" "$T/g" "$P" 2>&1)
echo "$out" | grep -q "^unshare(NEWUTS): Operation not permitted" || { echo "ns: unshare(CLONE_NEWUTS) must be EPERM without a rootfs too: $out"; rc=1; }
echo "$out" | grep -q "^clone(NEWUSER): Operation not permitted" || { echo "ns: clone(CLONE_NEWUSER) must be EPERM, not a silent fork: $out"; rc=1; }
out=$(timeout 60 "$B/pemu_x64" -r "$T/rf" --no-default-binds /g "$P" 2>&1)
echo "$out" | grep -q "^unshare(NEWUTS): Operation not permitted"  || { echo "ns: unshare(CLONE_NEWUTS) must be EPERM in a rootfs: $out"; rc=1; }
echo "$out" | grep -q "^clone(NEWUSER): Operation not permitted"   || { echo "ns: clone(CLONE_NEWUSER) must be EPERM in a rootfs, not a silent fork: $out"; rc=1; }
echo "$out" | grep -q "^name_to_handle_at: found"                  || { echo "ns: name_to_handle_at must resolve the path inside the rootfs: $out"; rc=1; }
echo "$out" | grep -q "^setns: Operation not permitted"            || { echo "ns: setns must be EPERM in a rootfs: $out"; rc=1; }
[ $rc = 0 ] && echo "ns pemu_x64: ok" || echo "ns pemu_x64: FAILED"
exit $rc
