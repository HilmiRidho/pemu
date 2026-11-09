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

## Still open

* **Races on read-only path syscalls** (stat, access, readlink, getxattr, statfs) remain by design: they stay on the fast path. `execve` also resolves its file separately.
* `pemu_fchdir`: if the fd points outside every bind, the host cwd changes but the tracked guest cwd stays stale.
* `getsockname` on a socket bound through the long-path trick reports the `/proc/self/fd/N/...` form (the kernel stores that string).
* Raw binds (`/dev /proc /sys`) are resolved by the host kernel by design.
* Untranslated: `fanotify_mark`, `pivot_root`, `execveat` through a closed fd, abstract AF_UNIX addresses.
* `RESOLVE_NO_XDEV` is accepted but not enforced.

## Not verified here

* A full distribution rootfs: only a small freestanding guest (`tests/e2e.sh`) has been run, as AArch64 under `pemu` and as x86_64 under `pemu_x64`.
* The Termux / bionic build and `openat2` under Android's seccomp filter.
