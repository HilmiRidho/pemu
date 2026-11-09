# pemu

User-mode emulator for Linux binaries with a built-in proot (chroot, bind mounts, fake root).
No `ptrace`, no tracer process: guest paths are translated directly in the syscall layer.

| binary | guest | host |
|---|---|---|
| `pemu` | AArch64 | x86_64 (e.g. Debian/Ubuntu amd64) |
| `pemu` | x86_64 | aarch64 (e.g. Termux, Debian arm64) |
| `pemu_x64` | x86_64 | aarch64 (also builds on x86_64, for testing) |

Version: see `VERSION` (`pemu --version`). License: Apache-2.0 (`LICENSE`).

## Install

From a `.deb` (every push builds them under *Actions → Artifacts*; a `vX.Y.Z` tag publishes them under *Releases*):

    sudo apt install ./pemu_<version>_debian_<arch>.deb        # Debian / Ubuntu (amd64, arm64)
    apt install ./pemu_<version>_termux_aarch64.deb            # Termux (or: dpkg -i ...)

From source (needs a C compiler; tests need clang + lld for the end-to-end part, otherwise it is skipped):

    ./build.sh            # output in ./build (pemu, pemu_x64); runs the tests
    SKIP_TESTS=1 CC=... ARCH=aarch64 ./build.sh               # cross build

`tar` (and `xz-utils` for `.tar.xz`) is needed for `pemu install`.

## Uninstall

    sudo apt remove pemu          # Debian / Ubuntu
    apt remove pemu               # Termux

Removing the package does **not** delete installed rootfs. Delete those first with `pemu uninstall NAME`
(see below), or remove `~/.pemu/rootfs` by hand.

## Usage

    pemu login debian                       # enter a rootfs (bash -l, fake root, cwd /root)
    pemu login debian apt update            # run one command
    pemu login debian -b $HOME:/mnt/home -w /mnt/home
    pemu login debian -b /sdcard -i 1000:1000 ls /sdcard   # options after NAME, then the command
    pemu install debian rootfs.tar.xz       # unpack a tarball (strips one top-level dir automatically)
    pemu list                               # installed rootfs
    pemu uninstall debian                   # delete a rootfs (asks first; -y to skip the question)

`NAME` is looked up in `$PEMU_ROOTFS_DIR`, `~/.pemu/rootfs`, `$PREFIX/var/lib/pemu/rootfs` and
`$PREFIX/var/lib/proot-distro/installed-rootfs` (proot-distro rootfs directories work as they are).
`pemu install` unpacks into `$PEMU_ROOTFS_DIR` if set, otherwise `~/.pemu/rootfs`.

### Sub-commands

| command | meaning |
|---|---|
| `login NAME\|DIR [options] [cmd [args...]]` | enter a rootfs; default command is `bash -l` |
| `install NAME TARBALL [--strip N]` | unpack a rootfs tarball; `--strip N` overrides the automatic choice |
| `uninstall NAME [-y]` | delete the rootfs `NAME` |
| `list` | list rootfs found in the lookup directories |

`login` options go **after** `NAME`; the first non-option argument starts the command (`--` ends the options):

| option | meaning |
|---|---|
| `-b, --bind SRC[:DST]` | bind mount (repeatable) |
| `-w, --cwd DIR` | initial guest directory (default `/root`) |
| `-e, --env VAR=val` | guest environment variable (repeatable) |
| `-i, --change-id UID[:GID]` | fake uid/gid instead of root |
| `--no-root-id` | do not fake uid/gid 0 (host ids stay) |
| `-k, --kernel-release R` | fake `uname -r` |
| `-l, --link2symlink` | emulate hard links with symlinks |
| `--kill-on-exit` | kill leftover guest processes when the main one exits |
| `--no-default-binds` | do not bind `/dev /proc /sys` |
| `-v, --verbose N` | log level |
| `-strace`, `-nojit` | trace guest syscalls / use the interpreter |
| `-h, --help` | |

Value options also accept `--name=value`. `pemu login -b x NAME` (options before `NAME`) is an error.
Without `login`, `pemu [-strace] [-nojit] program [args...]` only runs one program, without a rootfs.

`login` also fills in `/etc/resolv.conf` (when it is empty, a symlink, or has no non-loopback nameserver) and
`/etc/hosts` (when empty), and writes `APT::Sandbox::User "root"` so apt does not try to switch to `_apt`.

`uninstall` takes a plain name (no `/`, `.` or `..`), never follows symlinks (a symlinked rootfs only loses the link),
makes read-only directories writable before deleting, and leaves directories on another device (a real mount) alone.
Without `-y` it asks on the terminal and refuses when stdin is not a terminal.

### Environment

| variable | effect |
|---|---|
| `PEMU_ROOTFS_DIR` | extra place to look for (and install) rootfs directories |
| `PEMU_SECURE_OPEN=0/1` | race-free `openat2` open path off/on (default: on, off on Android) |
| `PEMU_FAKE_PROC=1` | always serve the fake `/proc` files, even if the real ones can be read |
| `PEMU_JIT=0` | run the interpreter instead of the JIT |
| `PEMU_STRACE=1` | trace guest syscalls (same as `-strace`) |
| `PEMU_SET_ENV=A=1,B=2` | extra guest environment variables (comma separated `VAR=val`) |

## Translated

open/openat/openat2 (with `RESOLVE_*`), stat/lstat/statx/newfstatat, access, mkdir, rmdir, unlink, rename, link,
symlink, readlink, chmod/fchmodat2, chown, utime*, mknod, truncate, statfs, xattr, inotify_add_watch,
chdir/fchdir/getcwd, execve (including `#!` scripts), `/proc/self/exe|cwd|fd/N`, AF_UNIX socket paths in both
directions (host paths longer than `sun_path` go through `/proc/self/fd/N/…`). Absolute symlinks and `..` are
resolved inside the rootfs.

- `openat2`: `RESOLVE_NO_SYMLINKS`, `BENEATH`, `IN_ROOT` are enforced on the guest path; `NO_MAGICLINKS`, `CACHED` go
  to the host; `NO_XDEV` is accepted but not enforced; unknown bits fail with `EINVAL`.
- Race-free walk: for paths inside the rootfs the last step uses `openat2(rootfd, path, RESOLVE_IN_ROOT)` (open and the
  mutating calls: mkdir, unlink, rmdir, rename, link, symlink, mknod, chmod, chown, utimes, truncate, xattr, chdir).
  Needs Linux 5.6+ and `/proc`.
- Fake `/proc`: when the real file cannot be opened (typical on Android 8+), `/proc/stat`, `loadavg`, `uptime`,
  `version`, `vmstat` and `sys/kernel/cap_last_cap` are served from memory (read-only snapshot taken at `open`).
- `mount -o bind` / `umount` edit pemu's bind table (this process tree only). Other mount types are rejected.
- `chroot(dir)` works (binds below it are kept, the others hidden, cwd clamped); call it from a single thread.

## Limitations

- Not a security boundary: read-only calls (stat, access, readlink, ...) can still race on symlinks, `execve`
  resolves its file separately, raw binds (`/dev /proc /sys`) are resolved by the host kernel, untranslated syscalls
  see host paths, and the race-free walk is off by default on Android.
- Untranslated: `execveat` through a closed fd, abstract/unnamed AF_UNIX addresses, `fanotify_mark`; `pivot_root`
  is refused (`EPERM`).
- Fake root is cosmetic: files owned by the host user appear as uid 0, `chown`/`setuid` always succeed.
- Fake `/proc` files report mode 0444, size 0 on `stat`.
- Only a small freestanding guest is covered by the end-to-end test; see `REVIEW.md` for what is not verified.
