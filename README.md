# pemu

User-mode emulator for Linux binaries with a built-in proot (chroot, bind mounts, fake root).
No `ptrace` and no tracer process: guest paths are translated directly in the syscall layer.

| binary | guest | host |
|---|---|---|
| `pemu` | AArch64 | x86_64 (Debian/Ubuntu amd64) |
| `pemu` | x86_64 | aarch64 (Termux, Debian arm64) |
| `pemu_x64` | x86_64 | aarch64 (also builds on x86_64, for testing) |

Version: `VERSION` (`pemu --version`). License: Apache-2.0.

## Install

From a `.deb` (built on every push under *Actions → Artifacts*; a `vX.Y.Z` tag publishes them under *Releases*):

    sudo apt install ./pemu_<version>_debian_<arch>.deb        # Debian / Ubuntu (amd64, arm64)
    apt install ./pemu_<version>_termux_aarch64.deb            # Termux

From source (needs a C compiler; the end-to-end tests also need clang + lld, otherwise they are skipped):

    ./build.sh                                                 # output in ./build, runs the tests
    SKIP_TESTS=1 CC=... ARCH=aarch64 ./build.sh                # cross build

`pemu install` needs `tar` (and `xz-utils` for `.tar.xz`).

## Uninstall

    sudo apt remove pemu          # Debian / Ubuntu  (Termux: apt remove pemu)

This does **not** delete installed rootfs. Run `pemu uninstall NAME` first, or remove `~/.pemu/rootfs` by hand.

## Usage

    pemu login debian                        # enter a rootfs (bash -l, fake root, cwd /root)
    pemu login debian apt update             # run one command
    pemu login debian -b $HOME:/mnt/home -w /mnt/home
    pemu install debian rootfs.tar.xz        # unpack a tarball (strips one top-level dir automatically)
    pemu list                                # installed rootfs
    pemu uninstall debian                    # delete a rootfs (asks first; -y skips the question)

`NAME` is looked up in `$PEMU_ROOTFS_DIR`, `~/.pemu/rootfs`, `$PREFIX/var/lib/pemu/rootfs` and
`$PREFIX/var/lib/proot-distro/installed-rootfs`. `pemu install` unpacks into `$PEMU_ROOTFS_DIR`, or `~/.pemu/rootfs` if unset.

Without `login`, `pemu [-strace] [-nojit] program [args...]` just runs one program, without a rootfs.

### `login` options

Options go **after** `NAME`; the first non-option argument starts the command (`--` ends the options).
`pemu login -b x NAME` is an error. Value options also accept `--name=value`.

| option | meaning |
|---|---|
| `-b, --bind SRC[:DST]` | bind mount (repeatable) |
| `-w, --cwd DIR` | initial guest directory (default `/root`) |
| `-e, --env VAR=val` | guest environment variable (repeatable) |
| `-i, --change-id UID[:GID]` | fake uid/gid instead of root |
| `--no-root-id` | do not fake uid/gid 0 |
| `-k, --kernel-release R` | fake `uname -r` |
| `-l, --link2symlink` | emulate hard links with symlinks |
| `--kill-on-exit` | kill leftover guest processes when the main one exits |
| `--no-default-binds` | do not bind `/dev /proc /sys` |
| `-v, --verbose N` | log level |
| `-strace`, `-nojit` | trace guest syscalls / use the interpreter |

`login` also fills in `/etc/resolv.conf` and `/etc/hosts` when they are empty (or unusable), and sets
`APT::Sandbox::User "root"` so apt does not try to switch to `_apt`.

`uninstall` accepts only a plain name, never follows symlinks (a symlinked rootfs only loses the link), and leaves
directories on another device alone. Without `-y` it asks on the terminal and refuses if stdin is not one.

### Environment

| variable | effect |
|---|---|
| `PEMU_ROOTFS_DIR` | extra place to look for (and install) rootfs |
| `PEMU_SECURE_OPEN=0/1` | race-free `openat2` path walk off/on (default on, off on Android) |
| `PEMU_FAKE_PROC=1` | always serve the fake `/proc` files |
| `PEMU_FAKE_OWNER=0` | do not remember `chown` results (see "Fake owners") |
| `PEMU_JIT=0` | use the interpreter instead of the JIT |
| `PEMU_JIT_STATS=1` / `PEMU_JIT_OPT=mask` | print JIT statistics / toggle optional JIT translations (for diagnosing speed or a misbehaving guest; bit 16384 = cmp/test/sub/add fused with the jcc after them) |
| `PEMU_STRACE=1` | trace guest syscalls |
| `PEMU_SET_ENV=A=1,B=2` | extra guest environment variables |

## What is translated

- **Paths:** open/openat/openat2, stat family, access, mkdir, rmdir, unlink, rename, link, symlink, readlink, chmod, chown,
  utime*, mknod, truncate, statfs, xattr, inotify/fanotify, chdir/getcwd, `execve` (including `#!` scripts and
  `/proc/self/exe|cwd|fd/N`). Absolute symlinks and `..` are resolved inside the rootfs.
- **AF_UNIX sockets:** paths are translated in both directions. Abstract and unnamed addresses name no file and pass through
  (the abstract namespace is shared with the host, as with a plain `chroot`).
- **Race-free walk** (Linux 5.6+, needs `/proc`): the last path step uses `openat2(RESOLVE_IN_ROOT)`, also for the mutating calls.
- **`mount -o bind` / `umount`:** edit pemu's bind table (64 entries, this process tree only); other mount types are rejected.
  `chroot(dir)` works.
- **Fake `/proc`:** when the real file cannot be read (typical on Android 8+), `/proc/stat`, `loadavg`, `uptime`, `version`,
  `vmstat` and `sys/kernel/cap_last_cap` are served from memory.

### Refused on purpose

These fail with `EPERM` (the table is `linux-user/proot/deny.inc.h`, the same on both architectures):

- sandbox escapes: `ptrace`, `setns`, `unshare`/`clone` with `CLONE_NEW*`, `open_by_handle_at`, the new mount API, `pivot_root`
- acting on the host or on other processes: `process_vm_*`, `kcmp`, `pidfd_getfd`, `bpf`, `perf_event_open`, `userfaultfd`,
  `landlock_*`, `keyctl`, `add_key`, `request_key`; and, when pemu runs as real root, the calls that change the host's clock,
  name, kernel log or quotas
- `acct`, `swapon/off`, `reboot`, `kexec_*`, the module calls

Inside a rootfs `io_uring` is refused with `ENOSYS`. Programs that use these usually fall back to plain syscalls.

## Fake owners

A fake root may `chown` anything, but the host refuses. pemu keeps the requested owner in the file's xattr
`user.pemu.own` (`"UID:GID"`), and `stat`/`fstat`/`statx` show it, so `ls -l`, `tar` and `dpkg` agree with each other.

- `chown(uid, -1)` and `chown(-1, gid)` keep the other half.
- A set-uid / set-gid program gets the recorded owner as its effective and saved id (0 if the file has no record).
- Report-only: pemu does not check permissions with these ids.
- Not recorded (the old behaviour stays: `chown` "succeeds", `stat` shows the usual mapping): symlinks, FIFOs, sockets,
  device nodes, read-only files, and file systems without user xattrs. Files created by a non-root fake user are also not recorded.
- The record lives in the rootfs: copying it with a tool that drops xattrs loses it. The guest can see it with `listxattr`.
- Partial `chown`s are serialised between threads. Two forked pemu processes doing one at the same instant on the same file
  can still lose one half.
- `PEMU_FAKE_OWNER=0` switches the feature off.

## Limitations

- **Not a security boundary.** Read-only calls (stat, access, readlink, ...) can race on symlinks, `execve` resolves its file
  separately, raw binds (`/dev /proc /sys`) are resolved by the host kernel, untranslated syscalls see host paths, and the
  race-free walk is off by default on Android.
- **Fake root is mostly cosmetic.** Files owned by the host user appear as uid 0, and `chown`/`setuid` always succeed.
  One exception is not cosmetic: when a "root" open fails on permission bits of a file the host user owns (dpkg reopening a
  0440 file), pemu adds the missing bits for that open and restores them. Other processes can see them for that moment, and a
  `kill -9` of pemu in between leaves them set.
- Fake `/proc` files report mode 0444, size 0.
- Only a small freestanding guest is covered by the end-to-end test. See `REVIEW.md` for what is not verified.
