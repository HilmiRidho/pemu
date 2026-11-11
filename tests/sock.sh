#!/bin/sh
# AF_UNIX path handling under pemu_x64 -r ROOTFS (x86_64 host, plain gcc + python3):
#   - a path the guest lookup refuses (symlink loop -> ELOOP) used to be handed to the HOST kernel as it stood, so connect() reached a host
#     socket where open() said ELOOP;
#   - connect() to a symlink with an ABSOLUTE target (/link -> /run/real.sock) used to fail with ENOENT: the host kernel followed the link
#     against the host root;
#   - recvmmsg returned host paths as the sender's name (getsockname / recvfrom / recvmsg were already converted).
# Skips (exit 0) when the host is not x86_64, gcc cannot link statically or python3 is missing.  usage: tests/sock.sh [BUILD_DIR]
cd "$(dirname "$0")/.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "sock: x86_64 host required, skipping"; exit 0; }
command -v python3 >/dev/null || { echo "sock: python3 missing, skipping"; exit 0; }
T=$(mktemp -d); trap 'kill $PY 2>/dev/null; rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/sock_guest.c 2>/dev/null || { echo "sock: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/loop" "$T/rf$T" "$T/rf/run"; cp "$T/g" "$T/rf/g"; ln -s loop "$T/rf$T/loop"; ln -s /run/real.sock "$T/rf/link"
python3 - "$T" <<'PY' &
import socket, sys, time
t = sys.argv[1]
keep = []
for p in (t + "/loop/s", t + "/rf/run/real.sock"):          # a HOST socket behind the looping path, and a socket that lives in the rootfs
    s = socket.socket(socket.AF_UNIX); s.bind(p); s.listen(5); keep.append(s)
time.sleep(30)
PY
PY=$!
sleep 1
rc=0
run() { timeout 60 "$B/pemu_x64" -r "$T/rf" --no-default-binds /g "$@" 2>&1; }
out=$(run connect "$T/loop/s");  echo "$out" | grep -q "CONNECTED" && { echo "sock: connect() through a symlink loop reached a HOST socket: $out"; rc=1; }
echo "$out" | grep -q "Too many levels of symbolic links" || { echo "sock: connect() through a symlink loop must be ELOOP: $out"; rc=1; }
out=$(run connect /link);        echo "$out" | grep -q "CONNECTED" || { echo "sock: connect() via an absolute symlink inside the rootfs must work: $out"; rc=1; }
out=$(run connect /run/real.sock); echo "$out" | grep -q "CONNECTED" || { echo "sock: plain connect() inside the rootfs must work: $out"; rc=1; }
out=$(run names)
for k in getsockname recvfrom recvmsg recvmmsg; do echo "$out" | grep -q "^$k *: /sock-[ab]$" || { echo "sock: $k must report a guest path (/sock-x): $out"; rc=1; }; done
echo "$out" | grep -q "$T" && { echo "sock: a host path leaked into the guest: $out"; rc=1; }
[ $rc = 0 ] && echo "sock pemu_x64: ok" || echo "sock pemu_x64: FAILED"
exit $rc
