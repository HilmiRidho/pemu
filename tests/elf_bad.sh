#!/bin/sh
# Damaged ELF files must be refused by the loader (exit 127 with a message), never half loaded or crash the emulator (x86_64 guest, any host with python3).
# A hand-made valid file must still run.  usage: tests/elf_bad.sh [BUILD_DIR]
cd "$(dirname "$0")/.."
B=${1:-build}
command -v python3 >/dev/null || { echo "elf_bad: python3 missing, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I - "$T" <<'PY'
import struct, sys
d = sys.argv[1]
code = bytes.fromhex("b83c000000" "31ff" "0f05")                       # exit(0)
def elf(name, etype=2, phentsize=56, phs=None):
    if phs is None: phs = [(1, 5, 0, 0x10000000, 0x80 + len(code), 0x80 + len(code))]
    ph = b"".join(struct.pack("<IIQQQQQQ", t, f, o, v, v, fs, ms, 0x1000) for (t, f, o, v, fs, ms) in phs)
    eh = b"\x7fELF\x02\x01\x01" + b"\0" * 9 + struct.pack("<HHIQQQIHHHHHH", etype, 62, 1, 0x10000080, 64, 0, 0, 64, phentsize, len(phs), 0, 0, 0)
    body = eh + ph
    body += b"\0" * (0x80 - len(body)) + code
    open(d + "/" + name, "wb").write(body); 
elf("ok")
# two PT_LOAD sharing one page: R+X segment [0x10000000, 0x10001100) and RW segment at 0x10001800.  The code exits 0 only if the tail of
# the first one (0x100010f0) and the data of the second one (0x10001800) are both there; the first segment used to lose its tail.
code2 = bytes.fromhex("0fb63c25f0100010" "83f75a" "0fb6042500180010" "83f077" "09c7" "b83c000000" "0f05")   # exit((byte[0x100010f0] ^ 0x5a) | (byte[0x10001800] ^ 0x77))
ph2 = struct.pack("<IIQQQQQQ", 1, 5, 0, 0x10000000, 0x10000000, 0x1100, 0x1100, 0x1000) + struct.pack("<IIQQQQQQ", 1, 6, 0x1800, 0x10001800, 0x10001800, 8, 8, 0x1000)
eh2 = b"\x7fELF\x02\x01\x01" + b"\0" * 9 + struct.pack("<HHIQQQIHHHHHH", 2, 62, 1, 0x100000c0, 64, 0, 0, 64, 56, 2, 0, 0, 0)
b2 = bytearray(eh2 + ph2); b2 += b"\0" * (0xc0 - len(b2)) + code2      # two program headers end at 0xb0
b2 += b"\0" * (0x10f0 - len(b2)) + b"\x5a"; b2 += b"\0" * (0x1800 - len(b2)) + b"\x77" + b"\0" * 7
open(d + "/shared_page", "wb").write(bytes(b2))
# mov rax,[moffs64] / mov [moffs64],rax / mov al,[moffs64] / mov [moffs64],al / mov eax,[moffs32] (0x67): opcodes A0-A3, which the
# interpreter did not know (node/npm hit them).  The code exits 0 only if every load and store behaved.
def le(v, n): return v.to_bytes(n, "little")
code3 = (b"\x48\xa1" + le(0x10000200, 8) + b"\x48\xa3" + le(0x10000208, 8)
         + bytes.fromhex("488b142508020010" "4831c2" "4889d7" "48c1ef20" "09d7")
         + b"\xa0" + le(0x10000200, 8) + b"\xa2" + le(0x10000210, 8)
         + bytes.fromhex("0fb60c2510020010" "81f188000000" "09cf")
         + b"\x67\xa1" + le(0x10000200, 4) + bytes.fromhex("3588776655" "09c7" "b83c000000" "0f05"))
ph3 = struct.pack("<IIQQQQQQ", 1, 7, 0, 0x10000000, 0x10000000, 0x1000, 0x1000, 0x1000)
eh3 = b"\x7fELF\x02\x01\x01" + b"\0" * 9 + struct.pack("<HHIQQQIHHHHHH", 2, 62, 1, 0x10000080, 64, 0, 0, 64, 56, 1, 0, 0, 0)
b3 = bytearray(eh3 + ph3); b3 += b"\0" * (0x80 - len(b3)) + code3
b3 += b"\0" * (0x200 - len(b3)) + le(0x1122334455667788, 8); b3 += b"\0" * (0x1000 - len(b3))
open(d + "/moffs", "wb").write(bytes(b3))
elf("filesz_gt_memsz", phs=[(1, 5, 0, 0x10000000, 0x100000, 0x80 + len(code))])
elf("no_load", phs=[(4, 4, 0, 0x10000000, 0, 0)])
elf("bad_phentsize", phentsize=0)
elf("et_rel", etype=1)
elf("wrapped_vaddr", phs=[(1, 5, 0, 0xfffffffffffff000, 0x80, 0x80 + len(code))])
elf("huge_memsz", phs=[(1, 5, 0, 0x10000000, 0x80, 0xfffffffffffff000)])
PY
chmod +x "$T"/*
rc=0
timeout 60 "$B/pemu_x64" "$T/ok" >"$T/out" 2>&1; r=$?
[ $r -eq 0 ] || { echo "elf_bad: the valid hand-made file must run (exit $r): $(cat "$T/out")"; rc=1; }
timeout 60 "$B/pemu_x64" "$T/shared_page" >"$T/out" 2>&1; r=$?
[ $r -eq 0 ] || { echo "elf_bad: two segments sharing a page must both load (exit $r): $(cat "$T/out")"; rc=1; }
timeout 60 "$B/pemu_x64" "$T/moffs" >"$T/out" 2>&1; r=$?
[ $r -eq 0 ] || { echo "elf_bad: mov to/from a 64-bit absolute address must run (exit $r): $(cat "$T/out")"; rc=1; }
# `pemu login NAME cmd`: a bare command name is looked up in PATH (inside the rootfs) when the name as given does not exist
mkdir -p "$T/rfp/usr/bin"; cp "$T/moffs" "$T/rfp/usr/bin/mo"; chmod 755 "$T/rfp/usr/bin/mo"
PATH=/usr/local/bin:/usr/bin timeout 60 "$B/pemu_x64" -r "$T/rfp" -w / -- mo >"$T/out" 2>&1; r=$?
[ $r -eq 0 ] || { echo "elf_bad: a bare command name must be found in PATH (exit $r): $(cat "$T/out")"; rc=1; }
PATH=/usr/local/bin:/usr/bin timeout 60 "$B/pemu_x64" -r "$T/rfp" -w / -- nonexistent >"$T/out" 2>&1; r=$?
{ [ $r -eq 127 ] && grep -q "nonexistent: No such file" "$T/out"; } || { echo "elf_bad: a missing command must still fail with ENOENT, exit 127 (exit $r): $(cat "$T/out")"; rc=1; }
for n in filesz_gt_memsz no_load bad_phentsize et_rel wrapped_vaddr huge_memsz; do
    timeout 60 "$B/pemu_x64" "$T/$n" >"$T/out" 2>&1; r=$?
    if [ $r -eq 127 ] && grep -q "^pemu: .*$n: " "$T/out"; then :; else echo "elf_bad: $n must be refused with exit 127 and a message (exit $r): $(cat "$T/out")"; rc=1; fi
done
[ $rc -eq 0 ] && echo "elf_bad pemu_x64: ok"
exit $rc
