/* linux-user/aarch64/syscall.inc.h - guest syscall translation. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ syscalls */
static short tbl[512];
static u64 brk_base, brk_cur, brk_end;

static void init_tbl(void) {
    memset(tbl, -1, sizeof tbl);
#define T(a,b) tbl[a] = (b)
    T(5,SYS_setxattr); T(6,SYS_lsetxattr); T(7,SYS_fsetxattr); T(8,SYS_getxattr); T(9,SYS_lgetxattr);
    T(10,SYS_fgetxattr); T(11,SYS_listxattr); T(12,SYS_llistxattr); T(13,SYS_flistxattr);
    T(14,SYS_removexattr); T(15,SYS_lremovexattr); T(16,SYS_fremovexattr);
    T(17,SYS_getcwd); T(19,SYS_eventfd2); T(20,SYS_epoll_create1); T(23,SYS_dup); T(24,SYS_dup3);
    T(26,SYS_inotify_init1); T(27,SYS_inotify_add_watch); T(28,SYS_inotify_rm_watch); T(29,SYS_ioctl);
    T(32,SYS_flock); T(33,SYS_mknodat); T(34,SYS_mkdirat); T(35,SYS_unlinkat); T(36,SYS_symlinkat);
    T(37,SYS_linkat); T(38,SYS_renameat); T(40,SYS_mount); T(39,SYS_umount2);
    T(43,SYS_statfs); T(44,SYS_fstatfs); T(45,SYS_truncate); T(46,SYS_ftruncate); T(47,SYS_fallocate);
    T(48,SYS_faccessat); T(49,SYS_chdir); T(50,SYS_fchdir); T(51,SYS_chroot); T(52,SYS_fchmod);
    T(53,SYS_fchmodat); T(54,SYS_fchownat); T(55,SYS_fchown); T(57,SYS_close); T(59,SYS_pipe2);
    T(61,SYS_getdents64); T(62,SYS_lseek); T(63,SYS_read); T(64,SYS_write); T(65,SYS_readv);
    T(66,SYS_writev); T(67,SYS_pread64); T(68,SYS_pwrite64); T(69,SYS_preadv); T(70,SYS_pwritev);
    T(71,SYS_sendfile); T(72,SYS_pselect6); T(73,SYS_ppoll); T(76,SYS_splice); T(78,SYS_readlinkat);
    T(81,SYS_sync); T(97,SYS_unshare); T(264,SYS_name_to_handle_at); T(82,SYS_fsync); T(83,SYS_fdatasync); T(84,SYS_sync_file_range);
    T(85,SYS_timerfd_create); T(86,SYS_timerfd_settime); T(87,SYS_timerfd_gettime); T(88,SYS_utimensat);
    T(90,SYS_capget); T(91,SYS_capset); T(92,SYS_personality); T(95,SYS_waitid); T(96,SYS_set_tid_address);
    T(98,SYS_futex); T(99,SYS_set_robust_list); T(100,SYS_get_robust_list); T(101,SYS_nanosleep);
    T(102,SYS_getitimer); T(103,SYS_setitimer); T(113,SYS_clock_gettime);
    T(114,SYS_clock_getres); T(115,SYS_clock_nanosleep); 
    T(118,SYS_sched_setparam); T(119,SYS_sched_setscheduler); T(120,SYS_sched_getscheduler);
    T(121,SYS_sched_getparam); T(122,SYS_sched_setaffinity); T(123,SYS_sched_getaffinity);
    T(124,SYS_sched_yield); T(125,SYS_sched_get_priority_max); T(126,SYS_sched_get_priority_min);
    T(129,SYS_kill); T(130,SYS_tkill); T(131,SYS_tgkill); T(135,SYS_rt_sigprocmask); T(136,SYS_rt_sigpending);
    T(137,SYS_rt_sigtimedwait); T(138,SYS_rt_sigqueueinfo); T(133,SYS_rt_sigsuspend);
    T(140,SYS_setpriority); T(141,SYS_getpriority); T(143,SYS_setregid); T(144,SYS_setgid);
    T(145,SYS_setreuid); T(146,SYS_setuid); T(147,SYS_setresuid); T(148,SYS_getresuid);
    T(149,SYS_setresgid); T(150,SYS_getresgid); T(151,SYS_setfsuid); T(152,SYS_setfsgid);
    T(153,SYS_times); T(154,SYS_setpgid); T(155,SYS_getpgid); T(156,SYS_getsid); T(157,SYS_setsid);
    T(158,SYS_getgroups); T(159,SYS_setgroups); T(163,SYS_getrlimit);
    T(164,SYS_setrlimit); T(165,SYS_getrusage); T(166,SYS_umask); T(167,SYS_prctl); T(168,SYS_getcpu);
    T(169,SYS_gettimeofday); T(172,SYS_getpid); T(173,SYS_getppid); T(174,SYS_getuid);
    T(175,SYS_geteuid); T(176,SYS_getgid); T(177,SYS_getegid); T(178,SYS_gettid); T(179,SYS_sysinfo);
    T(198,SYS_socket); T(199,SYS_socketpair); T(200,SYS_bind); T(201,SYS_listen); T(202,SYS_accept);
    T(203,SYS_connect); T(204,SYS_getsockname); T(205,SYS_getpeername); T(206,SYS_sendto);
    T(207,SYS_recvfrom); T(208,SYS_setsockopt); T(209,SYS_getsockopt); T(210,SYS_shutdown);
    T(211,SYS_sendmsg); T(212,SYS_recvmsg); T(213,SYS_readahead); T(215,SYS_munmap); T(216,SYS_mremap);
    T(222,SYS_mmap); T(223,SYS_fadvise64); T(226,SYS_mprotect); T(227,SYS_msync); T(228,SYS_mlock);
    T(229,SYS_munlock); T(230,SYS_mlockall); T(231,SYS_munlockall); T(232,SYS_mincore); T(233,SYS_madvise);
    T(242,SYS_accept4); T(243,SYS_recvmmsg); T(260,SYS_wait4); T(261,SYS_prlimit64); T(267,SYS_syncfs);
    T(269,SYS_sendmmsg); T(276,SYS_renameat2); T(278,SYS_getrandom); T(279,SYS_memfd_create);
    T(285,SYS_copy_file_range); T(291,SYS_statx); T(439,SYS_faccessat2);
#undef T
}

/* open(2) flag differences between asm-generic (aarch64) and x86_64 */
static long fl_g2h(long f) {
    long r = f & ~(040000L | 0100000L | 0200000L | 0400000L);
    if (f & 040000)  r |= O_DIRECTORY;
    if (f & 0100000) r |= O_NOFOLLOW;
    if (f & 0200000) r |= O_DIRECT;
    return r;                    /* O_LARGEFILE dropped (always 64-bit) */
}
static long fl_h2g(long f) {
    long r = f & ~(O_DIRECTORY | O_NOFOLLOW | O_DIRECT | 0100000L);
    if (f & O_DIRECTORY) r |= 040000;
    if (f & O_NOFOLLOW)  r |= 0100000;
    if (f & O_DIRECT)    r |= 0200000;
    return r;
}
static void cv_stat(const struct stat*s, u8*g, const struct pemu_own_ov*ov) {   /* ov: fake owner of the file, or NULL */
    memset(g, 0, 128);
    *(u64*)(g + 0) = s->st_dev; *(u64*)(g + 8) = s->st_ino;
    *(u32*)(g + 16) = s->st_mode; *(u32*)(g + 20) = (u32) s->st_nlink;
    *(u32*)(g + 24) = ov && ov->valid ? ov->uid : PEMU_UID(s->st_uid); *(u32*)(g + 28) = ov && ov->valid ? ov->gid : PEMU_GID(s->st_gid);
    *(u64*)(g + 32) = s->st_rdev; *(s64*)(g + 48) = s->st_size;
    *(s32*)(g + 56) = (s32) s->st_blksize; *(s64*)(g + 64) = s->st_blocks;
    *(s64*)(g + 72) = s->st_atim.tv_sec; *(u64*)(g + 80) = (u64) s->st_atim.tv_nsec;
    *(s64*)(g + 88) = s->st_mtim.tv_sec; *(u64*)(g + 96) = (u64) s->st_mtim.tv_nsec;
    *(s64*)(g + 104) = s->st_ctim.tv_sec; *(u64*)(g + 112) = (u64) s->st_ctim.tv_nsec;
}
static int is_aarch64_elf(const char*p) {
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[20];
    ssize_t n = read(fd, h, 20);
    close(fd);
    return n == 20 && !memcmp(h, "\177ELF", 4) && h[4] == 2 && *(u16*)(h + 18) == EM_AARCH64;
}
/* ---- built-in proot hook: guest (asm-generic) syscalls that name a path or an identity.
 * Returns 1 and sets *res when it handled the call, 0 to let the normal translation run. */
static int pemu_a64_sys(u64 n, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, long *res) {
#define RET(x) do { *res = (x); return 1; } while (0)
    (void) a5;
    switch (n) {                                       /* hostname / domainname with -k */
    case 161: case 162: { long r; if (pemu_set_uts_name(n == 162, (const char*) a0, (size_t) a1, &r)) RET(r);
                          RET(pemu_force(hs(n == 161 ? SYS_sethostname : SYS_setdomainname, (long) a0, (long) a1, 0, 0, 0, 0))); }
    }
    if (pemu_fakeroot) switch (n) {                    /* identity: proot's fake_id0 */
    case 174: RET(pemu_idcall(PEMU_GETUID, 0, 0, 0));      case 175: RET(pemu_idcall(PEMU_GETEUID, 0, 0, 0));
    case 176: RET(pemu_idcall(PEMU_GETGID, 0, 0, 0));      case 177: RET(pemu_idcall(PEMU_GETEGID, 0, 0, 0));
    case 148: RET(pemu_idcall(PEMU_GETRESUID, a0, a1, a2)); case 150: RET(pemu_idcall(PEMU_GETRESGID, a0, a1, a2));
    case 146: RET(pemu_idcall(PEMU_SETUID, a0, 0, 0));     case 144: RET(pemu_idcall(PEMU_SETGID, a0, 0, 0));
    case 145: RET(pemu_idcall(PEMU_SETREUID, a0, a1, 0));  case 143: RET(pemu_idcall(PEMU_SETREGID, a0, a1, 0));
    case 147: RET(pemu_idcall(PEMU_SETRESUID, a0, a1, a2)); case 149: RET(pemu_idcall(PEMU_SETRESGID, a0, a1, a2));
    case 151: RET(pemu_idcall(PEMU_SETFSUID, a0, 0, 0));   case 152: RET(pemu_idcall(PEMU_SETFSGID, a0, 0, 0));
    case 55: { long r = pemu_force_acc(hs(SYS_fchown, (long) a0, (long) pemu_chown_id(a1, pemu_uid, getuid()), (long) pemu_chown_id(a2, pemu_gid, getgid()), 0, 0, 0));
               if (r == 0) pemu_own_chown(NULL, 0, (int) a0, a1, a2);
               RET(r); }
    case 52: RET(pemu_force(hs(SYS_fchmod, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 7:  RET(pemu_force(hs(SYS_fsetxattr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0)));
    case 91: RET(pemu_force_ns(hs(SYS_capset, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 159: RET(pemu_force_ns(hs(SYS_setgroups, (long) a0, (long) a1, 0, 0, 0, 0)));
    }
    if (!pemu_on) {                                    /* no rootfs: only what a fake root needs forced */
        if (!pemu_fakeroot) return 0;
        const char *q = (const char*) a1;
        switch (n) {
        case 54: RET(pemu_callat(SYS_fchownat, (long) a0, q, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
        case 33: RET(pemu_callat(SYS_mknodat, (long) a0, q, 0, (long) a2, (long) a3, 0, 0));
        case 53: RET(pemu_callat(SYS_fchmodat, (long) a0, q, 1, (long) a2, 0, 0, 0));
        case 5:  RET(pemu_call1(SYS_setxattr, (const char*) a0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        case 6:  RET(pemu_call1(SYS_lsetxattr, (const char*) a0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        }
        return 0;
    }
    const char *p1 = (const char*) a1;
    switch (n) {
    case 56: RET(pemu_open((long) a0, p1, fl_g2h((long) a2), (long) a3, 0));
    case 437: { u64 how[8] = {0};                    /* openat2: how->flags holds guest O_* flags */
                if (a3 < 24) RET(-EINVAL);
                if (a3 > sizeof how) RET(-E2BIG);
                memcpy(how, (const void*) a2, (size_t) a3); how[0] = (u64) fl_g2h((long) how[0]);
                RET(pemu_openat2((long) a0, p1, how, a3)); }
    case 33: RET(pemu_callat(SYS_mknodat, (long) a0, p1, 0, (long) a2, (long) a3, 0, 0));
    case 34: RET(pemu_callat(SYS_mkdirat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 35: RET(pemu_callat(SYS_unlinkat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 36: RET(pemu_symlinkat((const char*) a0, (long) a1, (const char*) a2));
    case 37: RET(pemu_call2at(SYS_linkat, (long) a0, (const char*) a1, !!(a4 & 0x400), (long) a2, (const char*) a3, 0, (long) a4));
    case 38: RET(pemu_call2at(SYS_renameat, (long) a0, (const char*) a1, 0, (long) a2, (const char*) a3, 0, 0));
    case 276: RET(pemu_call2at(SYS_renameat2, (long) a0, (const char*) a1, 0, (long) a2, (const char*) a3, 0, (long) a4));
    case 48: RET(pemu_callat(SYS_faccessat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 439: RET(pemu_callat(SYS_faccessat2, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 53: RET(pemu_callat(SYS_fchmodat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 452: RET(pemu_callat(452, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));   /* fchmodat2 */
    case 54: RET(pemu_callat(SYS_fchownat, (long) a0, p1, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
    case 78: RET(pemu_readlinkat((long) a0, p1, (char*) a2, (long) a3));
    case 79: { struct stat st; struct pemu_own_ov ov = {0}; long r = pemu_callat_ov(SYS_newfstatat, (long) a0, p1, !(a3 & 0x100), (long) &st, (long) a3, 0, 0, &ov);
               if (r == 0) cv_stat(&st, (u8*) a2, &ov);
               RET(r); }
    case 88: RET(pemu_callat(SYS_utimensat, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 291: { struct pemu_own_ov ov = {0}; long r = pemu_callat_ov(SYS_statx, (long) a0, p1, !(a2 & 0x100), (long) a2, (long) a3, (long) a4, 0, &ov);
                if (r == 0 && pemu_fakeroot) { u32 *b = (u32*) a4; b[5] = ov.valid ? ov.uid : PEMU_UID(b[5]); b[6] = ov.valid ? ov.gid : PEMU_GID(b[6]); } RET(r); }
    case 17: RET(pemu_getcwd((char*) a0, (long) a1));
    case 49: RET(pemu_chdir((const char*) a0));
    case 50: RET(pemu_fchdir((long) a0));
    case 51: RET(pemu_chroot((const char*) a0));
    case 40: RET(pemu_mount((const char*) a0, (const char*) a1, (unsigned long) a3));   /* mount: only MS_BIND (edits the bind table) */
    case 39: RET(pemu_umount((const char*) a0));                                            /* umount2 */
#ifdef SYS_fanotify_mark
    case 262: RET(pemu_fanotify_mark(SYS_fanotify_mark, (long) a0, (long) a1, (long) a2, (long) a3, (const char*) a4));
#endif
    case 43: RET(pemu_call1(SYS_statfs, (const char*) a0, 1, (long) a1, 0, 0, 0, 0));
    case 45: RET(pemu_call1(SYS_truncate, (const char*) a0, 1, (long) a1, 0, 0, 0, 0));
    case 5: RET(pemu_call1(SYS_setxattr, (const char*) a0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 6: RET(pemu_call1(SYS_lsetxattr, (const char*) a0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 8: RET(pemu_call1(SYS_getxattr, (const char*) a0, 1, (long) a1, (long) a2, (long) a3, 0, 0));
    case 9: RET(pemu_call1(SYS_lgetxattr, (const char*) a0, 0, (long) a1, (long) a2, (long) a3, 0, 0));
    case 11: RET(pemu_call1(SYS_listxattr, (const char*) a0, 1, (long) a1, (long) a2, 0, 0, 0));
    case 12: RET(pemu_call1(SYS_llistxattr, (const char*) a0, 0, (long) a1, (long) a2, 0, 0, 0));
    case 14: RET(pemu_call1(SYS_removexattr, (const char*) a0, 1, (long) a1, 0, 0, 0, 0));
    case 15: RET(pemu_call1(SYS_lremovexattr, (const char*) a0, 0, (long) a1, 0, 0, 0, 0));
    case 27: { char host[PEMU_HP]; long r = pemu_path(AT_FDCWD, p1, !(a2 & 0x02000000), host, sizeof host, NULL);
               if (r < 0) RET(r);
               RET(hs(SYS_inotify_add_watch, (long) a0, (long) (r == 0 ? host : p1), (long) a2, 0, 0, 0)); }
    case 200: return pemu_sock_hook(PEMU_S_BIND, a0, a1, a2, a3, a4, a5, res);
    case 203: return pemu_sock_hook(PEMU_S_CONNECT, a0, a1, a2, a3, a4, a5, res);
    case 204: return pemu_sock_hook(PEMU_S_GETSOCKNAME, a0, a1, a2, a3, a4, a5, res);
    case 205: return pemu_sock_hook(PEMU_S_GETPEERNAME, a0, a1, a2, a3, a4, a5, res);
    case 206: return pemu_sock_hook(PEMU_S_SENDTO, a0, a1, a2, a3, a4, a5, res);
    case 207: return pemu_sock_hook(PEMU_S_RECVFROM, a0, a1, a2, a3, a4, a5, res);
    case 211: return pemu_sock_hook(PEMU_S_SENDMSG, a0, a1, a2, a3, a4, a5, res);
    case 212: return pemu_sock_hook(PEMU_S_RECVMSG, a0, a1, a2, a3, a4, a5, res);
    case 243: return pemu_sock_hook(PEMU_S_RECVMMSG, a0, a1, a2, a3, a4, a5, res);
    case 269: return pemu_sock_hook(PEMU_S_SENDMMSG, a0, a1, a2, a3, a4, a5, res);
    case 202: return pemu_sock_hook(PEMU_S_ACCEPT, a0, a1, a2, a3, a4, a5, res);
    case 242: return pemu_sock_hook(PEMU_S_ACCEPT4, a0, a1, a2, a3, a4, a5, res);
    }
    return 0;
#undef RET
}

static long do_execve(const char*path, char**argv, char**envp) {
    char host[PEMU_HP], gprog[PATH_MAX]; char**av = argv; const char*hp = path, *gp = path;
    int xfd = -1;                                          /* fd of a file the new image cannot open by name (memfd, deleted file) */
    if (pemu_on) {                                         /* guest path -> host file, #! scripts expanded */
        long e = pemu_exec_resolve(path, argv, &av, host, gprog);
        if (e < 0) return e;
        hp = host; gp = gprog;
        if (pemu_exec_unnamed) xfd = pemu_exec_fdnum;
    } else xfd = pemu_exec_fdpath(path);
    if (!is_aarch64_elf(hp)) return hs(SYS_execve, (long) hp, (long) av, (long) envp, 0, 0, 0);
    int n = 0; while (av && av[n]) n++;
    char**nv = calloc((size_t) n + 8 + (size_t) pemu_reexec_count(), sizeof *nv);
    int k = 0, keep = -1; char xs[16];
    nv[k++] = g_self;
    if (pemu_on) k = pemu_reexec_opts(nv, k);
    if (xfd >= 0) {                                        /* hand the open file to the new image: it is closed there once loaded */
        keep = (int) fcntl(xfd, F_DUPFD, 3);               /* F_DUPFD: not close-on-exec */
        if (keep < 0) { int er = errno; free(nv); return -er; }
        snprintf(xs, sizeof xs, "%d", keep); nv[k++] = "--exec-fd"; nv[k++] = xs;
    }
    nv[k++] = "-0"; nv[k++] = n ? av[0] : (char*) gp; nv[k++] = (char*) gp;
    for (int i = 1; i < n; i++) nv[k++] = av[i];
    struct pemu_idsave ids; pemu_exec_ids(hp, &ids);          /* set-uid file: effective id 0 for the new image */
    long r = hs(SYS_execve, (long) g_self, (long) nv, (long) envp, 0, 0, 0);
    pemu_exec_ids_undo(&ids);                                  /* still here: the exec failed */
    if (keep >= 0) close(keep);
    free(nv);
    return r;
}
/* execveat(dirfd, path, argv, envp, flags) on top of do_execve (same as the x86_64 hook) */
static long do_execveat(int dirfd, const char*path, char**argv, char**envp, long flags) {
    char buf[4096 + 64];
    if (flags & ~(long)(0x1000 | 0x100)) return -EINVAL;                       /* AT_EMPTY_PATH, AT_SYMLINK_NOFOLLOW */
    if (!path) return -EFAULT;
    if (!*path) {
        if (!(flags & 0x1000)) return -ENOENT;
        if (snprintf(buf, sizeof buf, "/proc/self/fd/%d", dirfd) >= (int) sizeof buf) return -ENAMETOOLONG;
        path = buf;
    } else {
        if (flags & 0x100) {
            struct stat st;
            if (hs(SYS_newfstatat, dirfd, (long) path, (long) &st, AT_SYMLINK_NOFOLLOW, 0, 0) == 0 && S_ISLNK(st.st_mode)) return -ELOOP;
        }
        if (path[0] != '/' && dirfd != AT_FDCWD) {
            if (snprintf(buf, sizeof buf, "/proc/self/fd/%d/%s", dirfd, path) >= (int) sizeof buf) return -ENAMETOOLONG;
            path = buf;
        }
    }
    return do_execve(path, argv, envp);
}

static long gsys(CPU*c, int*noset) {
    u64 n = c->x[8], a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3], a4 = c->x[4], a5 = c->x[5];
    long r;
    (void) noset;
    if (g_jit) jit_syscall_hook(n, a0, a1, a2, a3);
    if (pemu_policy(PEMU_ARCH_A64, n, a0, a1, a2, a3, a4, &r)) return r;     /* the one refusal table (proot/deny.inc.h), shared with the x86_64 hook; with or without a rootfs */
    if ((pemu_on || pemu_fakeroot || pemu_kset) && pemu_a64_sys(n, a0, a1, a2, a3, a4, a5, &r)) return r;
    switch (n) {
    case 93: case 94: pemu_exit_hook(); return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 56: return hs(SYS_openat, (long) a0, (long) a1, fl_g2h((long) a2), (long) a3, 0, 0);
    case 59: return hs(SYS_pipe2, (long) a0, (long) a1, 0, 0, 0, 0);
    case 25:
        if (a1 == F_GETFL) { r = hs(SYS_fcntl, (long) a0, F_GETFL, 0, 0, 0, 0); return r < 0 ? r : fl_h2g(r); }
        if (a1 == F_SETFL) return hs(SYS_fcntl, (long) a0, F_SETFL, fl_g2h((long) a2), 0, 0, 0);
        return hs(SYS_fcntl, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 79: { struct stat st; r = hs(SYS_newfstatat, (long) a0, (long) a1, (long) &st, (long) a3, 0, 0); if (r == 0) cv_stat(&st, (u8*) a2, NULL); return r; }
    case 80: { struct stat st; r = hs(SYS_fstat, (long) a0, (long) &st, 0, 0, 0, 0); if (r == 0) { struct pemu_own_ov ov = {0}; l2s_fix_fd((int) a0, &st); pemu_own_fstat((int) a0, &ov); cv_stat(&st, (u8*) a1, &ov); } return r; }
    case 160: { struct utsname u; r = hs(SYS_uname, (long) &u, 0, 0, 0, 0, 0); if (r == 0) { pemu_uname_fix(&u, "aarch64"); memcpy((void*) a0, &u, sizeof u); } return r; }
    case 214: /* brk */
        if (a0 >= brk_base && a0 <= brk_end) brk_cur = a0;
        return (long) brk_cur;
    case 132: return 0;                              /* sigaltstack: accepted, ignored */
    case 134: {                                      /* rt_sigaction */
        int s = (int) a0;
        if (s < 1 || s > 64 || s == SIGKILL || s == SIGSTOP) return -EINVAL;
        struct gsa old = gsa[s];
        if (a1) {
            struct gsa nw = *(struct gsa*) a1;
            struct sigaction sa; memset(&sa, 0, sizeof sa);
            sigfillset(&sa.sa_mask);
            if (nw.handler == 0) sa.sa_handler = SIG_DFL;
            else if (nw.handler == 1) sa.sa_handler = SIG_IGN;
            else { sa.sa_sigaction = hsig; sa.sa_flags = SA_SIGINFO | (int)(nw.flags & SA_RESTART); }
            if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE) { if (nw.handler > 1) sa.sa_sigaction = hsegv; sa.sa_flags |= SA_NODEFER; }
            if (sigaction(s, &sa, NULL) < 0) return -errno;
            gsa[s] = nw;
        }
        if (a2) *(struct gsa*) a2 = old;
        return 0;
    }
    case 139: g_sigreturn(c); *noset = 1; return 0;
    case 21: {                                       /* epoll_ctl: packed host struct */
        struct epoll_event ev; struct epoll_event*p = NULL;
        if (a3) { ev.events = *(u32*) a3; ev.data.u64 = *(u64*)(a3 + 8); p = &ev; }
        return hs(SYS_epoll_ctl, (long) a0, (long) a1, (long) a2, (long) p, 0, 0);
    }
    case 22: {                                       /* epoll_pwait */
        int max = (int) a2; if (max <= 0 || max > 4096) return -EINVAL;
        struct epoll_event*ev = malloc(sizeof *ev * (size_t) max);
        r = hs(SYS_epoll_pwait, (long) a0, (long) ev, max, (long) a3, (long) a4, (long) a5);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 16 * i; *(u64*) g = 0; *(u32*) g = ev[i].events; *(u64*)(g + 8) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 220: {                                      /* clone: fork-style only */
        u64 flags = a0;
        if (flags & 0x10000 /*CLONE_THREAD*/) return -ENOSYS;
        if (flags & PEMU_CLONE_NEWMASK) return -EPERM;       /* no namespaces; and no silent success (see PEMU_CLONE_NEWMASK) */
        long pid = hs(SYS_fork, 0, 0, 0, 0, 0, 0);
        if (pid == 0) {
            pemu_atfork_child();                     /* a lock (or half-done table edit) of another thread at fork time would stay locked in the child */
            if (a1) c->sp = a1;
            if (flags & 0x01000000) *(u32*) a4 = (u32) getpid();     /* CHILD_SETTID */
            if (flags & 0x00080000 /*SETTLS*/) c->tpidr = a3;
            c->x[0] = 0; *noset = 1;
        } else if (pid > 0 && (flags & 0x00100000)) *(u32*) a2 = (u32) pid;   /* PARENT_SETTID */
        return pid;
    }
    case 221: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 281: return do_execveat((int) a0, (const char*) a1, (char**) a2, (char**) a3, (long) a4);
    case 41: case 89: case 104: case 105: case 106: case 142: case 224: case 225: case 273: case 294:
        return -EPERM;                                /* pivot_root, acct, kexec_load, init/delete/finit_module, reboot, swapon/off, kexec_file_load: never act on the host (as the x86_64 hook) */
    case 435: case 293: case 277: return -ENOSYS;
    default:
        if (n < 512 && tbl[n] >= 0) return hs(tbl[n], (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        if (g_trace) fprintf(stderr, "pemu: unsupported syscall %llu\n", (unsigned long long) n);
        return -ENOSYS;
    }
}
static void do_svc(CPU*c) {
    int noset = 0;
    u64 n = c->x[8];
    long r = gsys(c, &noset);
    if (g_trace) fprintf(stderr, "pemu: syscall %llu -> %ld\n", (unsigned long long) n, r);
    if (!noset) c->x[0] = (u64) r;
}

