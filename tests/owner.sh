#!/bin/sh
# Fake owners (x86_64 guest under pemu_x64, x86_64 host, plain gcc): what a fake root chowns must show up in stat / fstat / statx,
# survive rename and a new pemu process, and PEMU_FAKE_OWNER=0 must restore the old behaviour (chown "succeeds", nothing is
# remembered).  A symlink cannot hold the xattr: lchown still succeeds and the target is untouched.
# Skips (exit 0) when the host is not x86_64, gcc cannot link statically, or the file system has no user xattrs.
# usage: tests/owner.sh [BUILD_DIR]
cd "$(dirname "$0")/.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "owner: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/owner_guest.c 2>/dev/null || { echo "owner: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/rf"; cp "$T/g" "$T/rf/g.bin"; : > "$T/probe"
python3 -c "import os,sys; os.setxattr(sys.argv[1], 'user.pemu.own', b'1:1')" "$T/probe" 2>/dev/null || { echo "owner: no user xattrs here, skipping"; exit 0; }
R="-r $T/rf --root-id --no-default-binds"
rc=0
out=$(timeout 60 "$B/pemu_x64" $R /g.bin write 2>&1)
exp='chown 1000:100: 0
after chown: 1000:100 statx 1000:100
chown -1:50: 0
keeps uid: 1000:50 statx 1000:50
fchown 7:8: 0
fstat: 7:8
chown dir 33:33: 0
dir: 33:33 statx 33:33
lchown link 5:5: 0
link (target): 7:8 statx 7:8
chown 0:0: 0
back to root: 0:0 statx 0:0
chown 1000:100 again: 0
renamed: 1000:100 statx 1000:100'
[ "$out" = "$exp" ] || { echo "owner: write run differs:"; echo "$out"; echo "-- expected --"; echo "$exp"; rc=1; }
out=$(timeout 60 "$B/pemu_x64" $R /g.bin show 2>&1)
exp='f: 1000:100 statx 1000:100
d: 33:33 statx 33:33'
[ "$out" = "$exp" ] || { echo "owner: a new pemu process must see the same owners: $out"; rc=1; }
if [ "$(id -u)" != 0 ]; then       # as real root the host chown really works, so there is nothing to compare
    out=$(PEMU_FAKE_OWNER=0 timeout 60 "$B/pemu_x64" $R /g.bin show 2>&1)
    [ "$out" = "f: 0:0 statx 0:0
d: 0:0 statx 0:0" ] || { echo "owner: PEMU_FAKE_OWNER=0 must show the plain mapping (root): $out"; rc=1; }
fi
[ $rc = 0 ] && echo "owner pemu_x64: ok" || echo "owner pemu_x64: FAILED"
exit $rc
