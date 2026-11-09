# pemu + built-in proot

proot uses `ptrace` to intercept guest syscalls. pemu already executes every guest syscall itself,
so path translation happens directly in the syscall layer: no ptrace, no tracer process, no external
`qemu`. One binary = emulator + chroot + bind mounts + fake root.

## Usage

    pemu login debian                     # enter a rootfs (bash -l, fake root, cwd /root)
    pemu login debian apt update          # run a single command
    pemu login -b $HOME:/mnt/home -w /mnt/home debian
    pemu install debian rootfs.tar.xz     # unpack a tarball (needs `tar`; strips one level automatically)
    pemu list
    pemu -r /path/rootfs -b /sdcard --root-id /bin/ls    # proot style, without `login`

`NAME` is looked up in `$PEMU_ROOTFS_DIR`, `~/.pemu/rootfs`, `$PREFIX/var/lib/pemu/rootfs`
and `$PREFIX/var/lib/proot-distro/installed-rootfs` (proot-distro rootfs directories work as they are).

| option | meaning |
|---|---|
| `-r DIR` | guest root file system |
| `-b SRC[:DST]` | bind mount (repeatable) |
| `-w DIR` | initial guest directory |
| `--root-id` | fake uid/gid 0 (this is `-0` in proot; in pemu `-0` is still argv0, as in qemu) |
| `--no-default-binds` | do not bind `/dev /proc /sys` |
| `login -e VAR=val` | guest environment variable |

`login` also fills in `/etc/resolv.conf` (when it is empty, a symlink, or has no usable non-loopback nameserver) and `/etc/hosts` (when empty), and writes
`APT::Sandbox::User "root"` so apt does not try to switch to the `_apt` user.

Environment variables:

| variable | effect |
|---|---|
| `PEMU_ROOTFS_DIR` | extra place to look for (and install) rootfs directories |
| `PEMU_SECURE_OPEN=0/1` | force the race-free `openat2` open path off/on (default: on, off on Android) |
| `PEMU_FAKE_PROC=1` | always serve the fake `/proc` files, even if the real ones can be read |

## Install from a .deb

Every push to GitHub builds `.deb` packages (see *Actions → Artifacts*); a `vX.Y.Z` tag also
publishes them under *Releases*.

    # Debian / Ubuntu (amd64, arm64)
    sudo apt install ./pemu_<version>_debian_<arch>.deb

    # Termux (aarch64)
    apt install ./pemu_<version>_termux_aarch64.deb      # or: dpkg -i ...

On an amd64 host `pemu` = AArch64 guest; on an arm64 / Termux host `pemu` = x86_64 guest.

## Source layout (modelled on qemu-user)

`pemu.c` and `pemu_x64.c` are thin entry files. They include the parts in dependency order, so each program is a
single translation unit.

    target/<guest>/      guest CPU: state, decoder, interpreter (SSE/MMX/x87/FP/SIMD)      ~ qemu target/i386, target/arm
    tcg/<host>/          JIT back end (x86_64 guest -> tcg/aarch64, AArch64 guest -> tcg/x86_64)  ~ qemu tcg/
    linux-user/<guest>/  syscall translation, signals, ELF loader, cpu_loop, main          ~ qemu linux-user/
    linux-user/pemu_root.h   built-in proot: umbrella header that includes linux-user/proot/*.inc.h   ~ qemu util/path.c (-L)
    tests/               unit / end-to-end tests, plus embed_hook.inc.h (test-only hook, see below)

Files named `*.inc.h` are **fragments**: they have no include guard, are not self-contained and must only be included, in
order, from `pemu.c` / `pemu_x64.c` (or from the `tests/check_core_*.c` checks). `pemu_root.h` is the only regular header;
it is the one the unit tests include.

Both JIT back ends use the same split: `tcg_target` (overview, configuration), `emit` (instruction encoders), `guest_state`,
`block_cache`, then the translators (`decode` / `codegen` / `translate` in `tcg/aarch64`; `helpers` / `ldst` / `dataproc` /
`translate` in `tcg/x86_64`).

`linux-user/proot/` splits the built-in proot by topic, in dependency order: `state` (globals, logging, fake ids), `bind`
(path normalisation, bind table), `link2symlink`, `path` (guest path walk), `secure_walk` (race-free `openat2` walks),
`syscall_helpers`, `fake_proc`, `open`, `socket`, `exec`, `mount`, `fake_id`, `kompat` (`-k`), `exit`, `setup`, `cmdline`
and `subcmd` (`login` / `install` / `list`).

`tests/embed_hook.inc.h` is only compiled with `-DPEMU_EMBED`. It lets the AArch64 interpreter in `pemu` execute the code
that the AArch64 JIT back end of `pemu_x64` generates, so that back end can be tested on an x86_64 machine. `build.sh` does
not enable it.

### CPU core and OS layer are separate

`target/` and `tcg/` (the CPU core and the JIT) never call into `linux-user/` by name. Everything that needs an operating
system goes through one table, `PemuOS` (end of `target/<guest>/cpu.inc.h`), which `linux-user/<guest>/cpu_loop.inc.h` installs
before `main()`. This is the hand-off qemu-user does when `cpu_exec()` returns an exception to `cpu_loop()`.

| callback | called by the core when | Linux layer does |
|---|---|---|
| `syscall(c)` | guest runs `syscall` (x86_64) / `svc` (AArch64), interpreter or JIT | `do_syscall` / `do_svc` |
| `exception(c, e)` | divide error `EXCP_DE`, `int3` `EXCP_BP`, unsupported instruction `EXCP_UD`, privileged instruction `EXCP_GP` (x86_64); `EXCP_BRK`, `EXCP_UDEF` (AArch64) | raises the matching signal, or delivers it to the guest handler |
| `pending(c)` (AArch64) | the JIT sees the exit-request word `pend` set | `check_pending`: deliver queued guest signals |
| `host_fault(c)` (AArch64) | a host fault in translated code comes back through `g_jb` | `linux_host_fault`: make it a guest signal |

The exit-request words (`pend`, `tpend`, `PENDP()`), `g_jb` and the CPU model identity (`PEMU_BRAND`) belong to the core; the
OS layer only sets or reads them. `tests/check_core_*.c` compile the core without `linux-user/`, and `build.sh` runs that
check, so a new direct dependency fails the build.

## Build

    ./build.sh        # pemu / pemu_x64 + resolver tests, output in ./build

`pemu` = AArch64 guest on an x86_64 host. `pemu_x64` = x86_64 guest on an aarch64 host (Termux).
`build.sh` honours `CC`, `ARCH`, `OUT` and `SKIP_TESTS=1` (for cross builds).

## Tests

`./build.sh` runs `tests/test_pemu_root.c` (resolver, `RESOLVE_*`, sockets, fake `/proc`, symlink-race stress)
and `tests/e2e.sh`, which builds one freestanding guest (`tests/e2e_guest.c`, raw syscalls) with clang and runs
it as an AArch64 guest under `pemu` and as an x86_64 guest under `pemu_x64`, comparing the output with
`tests/e2e.expected`. The end-to-end part needs an x86_64 host with clang and lld and is skipped otherwise.

## What is translated

open/openat/openat2 (including `RESOLVE_*`), stat/lstat/statx/newfstatat, access, mkdir, rmdir, unlink,
rename(2), link, symlink, readlink, chmod/fchmodat2, chown, utime*, mknod, truncate, statfs, xattr,
inotify_add_watch, chdir/fchdir/getcwd, execve (including `#!` scripts), `/proc/self/exe|cwd|fd/N`.

AF_UNIX socket paths are translated in both directions: `bind`, `connect`, `sendto`, `sendmsg` and
`sendmmsg` map guest paths to host paths; `getsockname`, `getpeername`, `recvfrom`, `recvmsg`, `accept`
and `accept4` map the returned addresses back to guest paths. Host paths longer than `sun_path`
(108 bytes) are reached through `/proc/self/fd/N/…`.

Absolute symlinks and `..` are resolved inside the rootfs, so they cannot lead out to the host.

### openat2 `RESOLVE_*`

`RESOLVE_NO_SYMLINKS`, `RESOLVE_BENEATH` and `RESOLVE_IN_ROOT` are enforced by the guest-side path walk
(guest symlinks, not host ones). `RESOLVE_NO_MAGICLINKS` and `RESOLVE_CACHED` are passed to the host.
`RESOLVE_NO_XDEV` is accepted but not enforced. Unknown bits fail with `EINVAL`.

### Race-free walks

The guest walk validates a path, but the host kernel walks the translated path again, so a guest thread
could swap a directory for a symlink in between. For paths inside the rootfs the last step therefore goes
through `openat2(rootfd, path, RESOLVE_IN_ROOT)`, which keeps the kernel's own walk inside the rootfs.
This covers `open*` and the calls that change things: mkdir, unlink, rmdir, rename, link, symlink, mknod,
chmod, chown, utimes, truncate, setxattr/removexattr and chdir. Read-only calls (stat, access, readlink,
getxattr, ...) stay on the faster plain path. Needs Linux 5.6+ and `/proc`; without `openat2` the plain
path is used. A stress test (`tests/test_pemu_root.c`) swaps a directory and a symlink in a loop:
thousands of escapes with the plain walk, none with the race-free one.

### Fake `/proc`

If the real file cannot be opened (typical on Android 8+), `/proc/stat`, `/proc/loadavg`,
`/proc/uptime`, `/proc/version`, `/proc/vmstat` and `/proc/sys/kernel/cap_last_cap` are served from an
in-memory file with plausible values. They are read-only snapshots taken at `open` time.

## Limitations

- Not a security boundary: read-only calls (stat, access, readlink, ...) can still race on symlinks, `execve`
  resolves its file separately, raw binds (`/dev /proc /sys`) are resolved by the host kernel, syscalls
  that are not translated see host paths, and the race-free walk is off by default on Android.
- Still untranslated: `execveat` through an already closed fd, abstract and unnamed AF_UNIX addresses (they have
  no path; the abstract namespace is the host's), and the `/proc/self/fd/N/name` form of a long socket path in
  `getsockname` results. `pivot_root` is refused (EPERM).
- Fake `/proc` files can be `stat`ed (`stat`/`newfstatat`/`statx`: regular file, mode 0444, size 0) when the real ones
  are hidden.
- `mount -o bind` / `umount` work: they edit pemu's bind table (this process tree only; the host is not touched,
  read-only/nosuid flags are not enforced). Every other `mount` type and `umount` of something that is not a bind
  are rejected (EPERM / EINVAL).
- `chroot(dir)` works: dir becomes the guest root, binds below it are kept (re-rooted), the others are hidden, the cwd
  is clamped. Do it from a single thread: other threads walking paths during the switch may briefly see a mix.
- Fake root is cosmetic: files owned by the host user appear as uid 0, `chown`/`setuid` always succeed.

## License

Apache-2.0, see `LICENSE`.
