# Review of the syscall translation layer

Scope: `linux-user/pemu_root.h` (parts in `linux-user/proot/`) and the two syscall hooks (`pemu_a64_sys` in
`linux-user/aarch64/syscall.inc.h`, `pemu_x64_sys` in `linux-user/x86_64/syscall.inc.h`).

## Fixed in this revision

| # | Finding | Fix |
|---|---|---|
| 1 | **Symlink race on open.** The guest walk validated a path, then the host kernel walked the resulting host path again; a guest thread could swap a directory for a symlink in between and escape the rootfs. | Opens inside the rootfs now end in `openat2(rootfd, path, RESOLVE_IN_ROOT)`. Falls back to `openat` without `openat2` (Linux < 5.6, Android by default). |
| 2 | **`openat2` flags dropped** (x86_64 hook zeroed `how.resolve`); the AArch64 hook had no `openat2` at all (ENOSYS). | `RESOLVE_NO_SYMLINKS/BENEATH/IN_ROOT` enforced in `pemu_canon`; `NO_MAGICLINKS/CACHED` passed through; unknown bits give `EINVAL`; AArch64 got `openat2`. |
| 3 | **AF_UNIX `sendto`/`sendmsg`** passed guest paths to the host, so datagram sends to a path inside the rootfs failed or hit the wrong file. | Translated, plus `sendmmsg`. |
| 4 | **`getsockname`/`getpeername`/`recvfrom`/`recvmsg`/`accept` returned host paths** to the guest (leaks the rootfs location, breaks programs that compare or reuse the name). | Returned addresses are mapped back to guest paths, honouring the guest buffer size. |
| 5 | **`bind`/`connect` failed with ENAMETOOLONG** whenever the host path exceeded 108 bytes, which is common for rootfs under `/data/data/com.termux/...`. | Long paths go through `/proc/self/fd/<dirfd>/<name>`. |
| 6 | **Shell injection in `pemu install`:** tarball and destination were interpolated into `system()`/`popen()` strings inside single quotes, so a quote in the path broke out. | `fork`/`execvp` with plain arguments; `mkdir -p` done in C. |
| 7 | `fchmodat2` (glibc 2.39+ for `AT_SYMLINK_NOFOLLOW`) was not translated. | Added to both hooks. |
| 8 | Android hides `/proc/stat`, `loadavg`, ... | Fake files served from a memfd when the real open fails. |
| 9 | `-Wformat-truncation` warnings in `pemu_canon` / `pemu_install`. | Replaced `snprintf` with bounded `memcpy`. |
| 10 | **Races on the mutating path syscalls** (mkdir, unlink, rename, link, symlink, mknod, chmod, chown, utimes, truncate, xattr, chdir). | The parent directory (or the target, when the last component is followed) is opened with `openat2(RESOLVE_IN_ROOT)` and the call uses `/proc/self/fd/N[/name]`. A stress test swaps a directory and a symlink in a loop: 6944 escapes with the plain walk, 0 with this one. |
| 11 | **A `#!` script reached through a symlink ran under the wrong name.** `pemu_exec_resolve` handed the interpreter the symlink's target, the kernel hands it the path as given. `addgroup` (a symlink to `adduser`) therefore behaved as `adduser` and created a user instead of a group. | The interpreter gets the path as the guest gave it; only a path rebuilt from an fd uses the canonical name. Regression test with a symlinked script. |
| 12 | **`RESOLVE_NO_XDEV` was accepted but not enforced.** | Enforced in `pemu_canon`: stepping between two entries of the bind table (into a bind, onto its mount point, out of it through `..`) fails with `EXDEV`. Tests cover binds, `..`, symlinks, `/proc` and `openat2`. |
| 13 | **`execveat` / `execve("/proc/self/fd/N")` of a file with no guest name** (memfd, file deleted after open) died after the exec (exit 127), because the new image tried to open the name or the close-on-exec fd. | The fd is handed to the new image (`--exec-fd`, hidden), which loads from it and closes it; a nameless `#!` script returns `ENOENT`. AArch64 gained `execveat` (it was `ENOSYS`). Checked with a static x86_64 guest: memfd, deleted, named, with and without a rootfs, no leaked fd. |
| 14 | `pivot_root`, `acct`, `reboot`, `swapon/off`, `kexec_*` and the module calls were `EPERM` on x86_64 but `ENOSYS` on AArch64. | Both hooks answer `EPERM`. |

## Round 2

Found by reading the code against the claims of this file and of the README.  Items 15-20 have a test in `tests/test_pemu_fixes.c` (it also
builds against the previous code with `-DPEMU_TEST_ORIG`); items 21-23 in `tests/uring.sh` and `tests/ns.sh`, items 24-26 in `tests/sock.sh` (real x86_64 guests on an
x86_64 host).  Every test fails on the previous code except the `open_priv` race of item 19, which could not be reproduced.

| # | Finding | Fix |
|---|---|---|
| 15 | **The "openat2 off on Android" default never applied through `pemu login`.** `pemu_secure()` decides lazily from `ANDROID_ROOT`; `login` calls `clearenv()` (which drops it) before the first guest open, so on Termux the race-free path was switched **on**, where the seccomp filter may reject `openat2` and the fallback only handles `ENOSYS`. A guest running with a scrubbed environment did the same in every re-exec'd child. | The decision is taken in `pemu_login` before `clearenv()` and passed to children as the hidden `--secure-open 0/1`. Test fails on the old code (got 1). |
| 16 | **Host `O_TMPFILE` hard-coded as `020200000`** in `pemu_open`. That is the x86 value; on aarch64 hosts it means `O_TMPFILE`\|`O_DIRECT`. Worse, on x86 it also contains `O_DIRECTORY`, so *any* `O_DIRECTORY` open with a non-zero mode register (raw `openat` callers) went to `openat2` with a mode and failed with `EINVAL` (reproduced). The `NO_MAGICLINKS` fallback passed the mode unconditionally. | `PEMU_OPEN_HAS_MODE()` uses the host `O_CREAT` / `O_TMPFILE`. A guest `openat2` with a mode but no create flag is `EINVAL`, as in the kernel. |
| 17 | **Bind table edited under readers**: `chroot` replaced `g`/`gl` and `h`/`hl` in separate steps and `pemu_root` with `snprintf`, while other threads walked the table without a lock. `umount` only marked slots `off`; they were never reused, so 64 mounts over the life of a process ended in `ENOSPC` (reproduced at mount #61). | Entries are fixed buffers guarded by a seqlock (readers retry, writers hold `pemu_lock`); a freed slot is reused and `ord` keeps "later mount wins"; `umount` removes the newest mount of a path first; re-exec passes binds oldest first. `pemu_atfork_child()` releases a lock or half-done edit inherited by a fork child (both hooks; AArch64 had no such reset). |
| 18 | **`RESOLVE_CACHED` was accepted and dropped**, although the README said it goes to the host. | Forwarded on both open paths. See "Still open" for its limit. |
| 19 | **`pemu_open_priv` (fake root) chmod'ed host files by path, unserialised.** A swap of the path between the check and the restore could redirect the chmod; two threads could mistake the temporary mode for the real one. The README called fake root "cosmetic". | Inode pinned with an `O_PATH` fd, chmod / open / restore all through `/proc/self/fd/N`, threads serialised, `O_CREAT`/`O_EXCL`/`O_NOFOLLOW` dropped for the re-open. README text corrected. The race itself could **not** be reproduced on the old code (8 threads x 20000 opens), so this is a hardening, not a proven bug. |
| 20 | AArch64 hook used `pemu_force` for `fchown`, `capset`, `setgroups`; the x86_64 hook uses `pemu_force_acc` / `pemu_force_ns` for the same calls. | Same wrappers in both hooks. |
| 21 | **`io_uring` bypassed the rootfs** (x86_64 guest). `io_uring_setup/enter/register` were passed through; an `IORING_OP_OPENAT` / `STATX` / `UNLINKAT` / `RENAMEAT` / `MKDIRAT` / `SYMLINKAT` / `LINKAT` carries a guest path that the *kernel* resolves later from the ring, so pemu never translated it. Reproduced with a static guest: `open("/p")` read the rootfs file, an io_uring OPENAT of the same path read the **host** file. The AArch64 hook (a whitelist without io_uring) was not affected. | Inside a rootfs (`-r` / `login`) all three answer `ENOSYS`, which libuv, tokio, liburing users and glibc fall back from. Without a rootfs there is nothing to bypass and the rings are passed through as before. `tests/uring.sh`. |
| 22 | **Namespace calls acted on the host or lied.** `clone(CLONE_NEWUSER\|...)` (and the AArch64 `clone`) ignored the flags and returned a plain fork, so a sandbox tool (bwrap, chrome) believed it was isolated; `clone3` already refused them. `unshare` and `setns` went to the host kernel and changed the emulator process itself. | `clone` with a `CLONE_NEW*` flag is `EPERM` in both hooks (as `clone3`); `unshare` with such a flag and `setns` are `EPERM`. Later unified: one table for both hooks (`linux-user/proot/deny.inc.h`), also without a rootfs. `tests/ns.sh`, `tests/deny.sh`. |
| 23 | **`name_to_handle_at` took the path as a host path** (x86_64 whitelist); `open_by_handle_at` is the classic "shocker" container escape when pemu runs as real root. | `name_to_handle_at` goes through the path translation (the x86_64 hook used the literal `303`, wrong on an aarch64 host; now `SYS_name_to_handle_at`, and the AArch64 hook translates it too); `open_by_handle_at` is `EPERM`. `tests/ns.sh`, `tests/deny.sh`. |
| 24 | **`connect()` / `bind()` / `sendto()` on an AF_UNIX path the guest lookup refuses (symlink loop, name too long) went to the host kernel with the raw GUEST path**: `pemu_unix_prep` treated every error of the translation as "leave it alone". Reproduced: in a rootfs where `/loop` is a symlink to itself, `open()` said `ELOOP` and `connect()` reached the host's socket at the same path. | The error is returned; only "not a rootfs path" (raw `/proc /sys /dev` binds) still passes the path on. `tests/sock.sh`. |
| 25 | **`connect()` / `sendto()` to a symlink with an absolute target failed with `ENOENT`** (`/link -> /run/real.sock`): the lookup did not follow the last component, so the host kernel followed the link against the host root. | `connect` / `sendto` / `sendmsg` / `sendmmsg` resolve the path with a followed last component; `bind` still does not follow. `tests/sock.sh`. |
| 26 | **`recvmmsg` returned host paths as the sender's name** (`recvfrom`, `recvmsg`, `accept`, `getsockname` were converted, this one was a plain pass-through). | Hooked in both guests (x86_64 299, AArch64 243), every received message converted. `tests/sock.sh`. |

## Round 3

| # | Finding | Fix |
|---|---|---|
| 27 | **A mutating call on a direct child of the rootfs root used the wrong name.** In `pemu_path_s` the last component was taken from the buffer that was cut one line later, so for a parent of `/` the name was empty and the host path became `/proc/self/fd/N/`, the root itself: `mkdir("/d")` and `symlink(_, "/l")` failed with `EEXIST`, and `lchown("/l")` changed the owner of the **rootfs root directory**. Found while testing item 28 (owner of the rootfs root turned up as 5:5); the same code ran before. Only the race-free walk is affected (off on Android by default). | The name is taken from the untouched canonical path. `t_top_level` in `tests/test_pemu_fixes.c` fails on the previous code (4 checks) and `tests/owner.sh` covers it with a real guest. |
| 28 | **`chown` of a fake root was reported as done but never recorded** (`pemu_force` turned the host's `EPERM` into success; `stat` then showed root again). | Fake owners: uid/gid kept in the xattr `user.pemu.own`, shown by `stat`/`fstat`/`statx`; see README "Fake owners" for what it does not cover. `linux-user/proot/fake_owner.inc.h`; checks in `tests/test_pemu_fixes.c` (`t_fake_owner`) and `tests/owner.sh` (x86_64 guest; as an unprivileged host user the old code answers 0:0 after `chown 1000:100`). |

Item 28 is deliberately small: report-only (no permission check uses the ids), uid/gid only, best effort (any xattr failure keeps the old
behaviour), active only for a fake root inside a rootfs, `PEMU_FAKE_OWNER=0` turns it off. Modes (`chmod`), `mknod` and capabilities are not
recorded yet; `pemu_open_priv` is unchanged.

## Still open

* **Races on read-only path syscalls** (stat, access, readlink, getxattr, statfs) remain by design: they stay on the fast path. `execve` also resolves its file separately.
* `pemu_fchdir`: if the fd points outside every bind, the host cwd changes but the tracked guest cwd stays stale.
* `getsockname` on a socket bound through the long-path trick reports the `/proc/self/fd/N/...` form (the kernel stores that string).
* Raw binds (`/dev /proc /sys`) are resolved by the host kernel by design.
* `pivot_root` is refused on purpose (`EPERM`): it needs a private mount namespace and real mount types, and pemu only edits a bind table. `unshare` with a `CLONE_NEW*` flag, `setns`, `ptrace`, `open_by_handle_at` and the new mount API are refused with `EPERM` on both architectures by one table (item 22).
* `RESOLVE_NO_XDEV` sees pemu's mounts (the bind table), not host mounts inside the rootfs directory.
* `RESOLVE_CACHED`: pemu's own walk `lstat`s every component first, so the dentries are cached by the time `openat2` runs and the kernel
  practically never answers `EAGAIN`. The flag is validated (`EINVAL` on kernels before 5.12) but its "do not block" meaning is lost.
* Another process can see the temporary mode bits of `pemu_open_priv` for the length of one open; a `kill -9` of pemu in that moment
  leaves them set.
* The x86_64 hook is a whitelist of pass-through syscalls (`pass_nr`, `pass_nr2`); every entry that takes a path, a handle or a namespace is a
  potential bypass of the rootfs and was only checked for the ones listed above.  Entries not looked at in detail: `mq_open` / `mq_unlink` (the
  host's message-queue namespace is shared), `io_submit` (fd based, believed fine).  `process_vm_readv/writev`, `kcmp`, `pidfd_getfd`, `bpf`, `perf_event_open`, `userfaultfd`, `landlock_*`, `statmount` / `listmount` the key calls, the clock / hostname / syslog calls and `migrate_pages` / `move_pages` / `process_madvise` are now refused by `linux-user/proot/deny.inc.h` (`tests/deny.sh`).
* `bind` / `connect` still resolve the path with the plain walk, not the race-free one: a guest thread that swaps a parent directory for a
  symlink at the right moment could make `bind` create the socket outside the rootfs.  Using the parent-directory fd (as `mkdir` does) would
  leave `/proc/self/fd/N/name` as the socket's name, which `getsockname` and datagram senders would then show to every program, so it was
  not done.
* The fallback for a missing `openat2` only reacts to `ENOSYS`. A seccomp filter that answers `EPERM` or kills the process is not
  detected; on Android the path stays off by default (item 15) and `PEMU_SECURE_OPEN=0` forces it.

## Not verified here

* Round 3: built and tested on an x86_64 host only (gcc), as root and as an unprivileged user (`setpriv`). The aarch64 JIT side was not
  compiled; nothing there was changed. Fake owners were not run on Termux / Android (user xattrs under SELinux, tmpfs before Linux 6.6).

* A full distribution rootfs: only a small freestanding guest (`tests/e2e.sh`) has been run, as AArch64 under `pemu` and as x86_64 under `pemu_x64`.
* The Termux / bionic build and `openat2` under Android's seccomp filter.
* Round 2 was built and tested on an x86_64 host only (gcc; also with ASan/UBSan). The aarch64-host build (`pemu_x64`) was not compiled
  here; item 16 for that host follows from the header values, not from a run. The end-to-end test was skipped (no clang/lld).

### Audit of fake root and the loaders (round 4)
* `fake_id`: `setgid` decided "privileged" from the effective **gid** (`id[E] == 0`) instead of the effective uid, so a fake root with a non-zero egid only changed two of the four gids and a non-root with egid 0 changed all of them; `setuid` / `setgid(-1)` succeeded (kernel: `EINVAL`); an unprivileged `setuid(effective)` was allowed; `setreuid(real_or_effective, saved)` was refused although the kernel allows it; `setreuid(r, -1)` did not copy effective to saved; `setresuid` did not reset the fs id; `setfsuid(-1)` stored -1; `getresuid` with a bad pointer crashed the emulator instead of `EFAULT`.  `tests/test_proot_compat.c` (`ids_kernel`).
* ELF loaders (both guests): the program headers were trusted.  A file size larger than the memory size wrote past the mapping, a wrapped `p_vaddr + p_memsz` gave a bogus `mmap`, a file with no `PT_LOAD` was "loaded" and jumped to, `e_phentsize` and `e_type` were not checked, and error paths leaked the fd and the header copy.  `linux-user/elf_check.inc.h`; `tests/elf_bad.sh`.
* `pemu_open_priv` (fake root's temporary chmod) pinned the file by its host NAME again after the race-free walk had already reached it; it now pins the same file through the same `RESOLVE_IN_ROOT` walk.
* Fixed (formerly "not changed, known"): (a) two `PT_LOAD` segments that share one page (hand-made or `ld -N` files) lost the tail of the first one, because the second segment's anonymous `mmap` replaced the shared page; both loaders now map only the pages after the previous segment, make the shared page writable for the `pread`, and then give it the later segment's protection, as the kernel does (`tests/elf_bad.sh`, file `shared_page`: exit 90 on the old loader, 0 now). (b) A setuid / setgid file always gave id 0, even when its fake owner (see "Fake owners") was someone else; `pemu_exec_ids` now uses the recorded owner and keeps 0 for a file with no record or with `PEMU_FAKE_OWNER=0` (`t_owner_suid_lock` in `tests/test_pemu_fixes.c`). (c) The read-modify-write of a partial `chown` (`-1` for one id) is serialised between threads by `pemu_ownlk` (released in a fork child by `pemu_atfork_child`); a plain read is one atomic `getxattr` and needs no lock. The same test chowns `1000:-1` and `-1:50` from two threads 20000 times: 2 lost updates before on a single core, 0 now. Still best effort: two separate pemu processes (after a fork) that make a partial `chown` of the same file at the same instant share only the xattr, so one half can still be lost there.
