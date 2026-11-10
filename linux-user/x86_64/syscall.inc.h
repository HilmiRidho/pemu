/* linux-user/x86_64/syscall.inc.h - guest syscall translation. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ syscalls */
static u64 brk_base, brk_cur, brk_end;

struct fl { long g, h; };
static const struct fl oflags[] = {
    {0100, O_CREAT}, {0200, O_EXCL}, {0400, O_NOCTTY}, {01000, O_TRUNC}, {02000, O_APPEND}, {04000, O_NONBLOCK},
    {010000, O_DSYNC}, {020000, O_ASYNC}, {040000, O_DIRECT}, {0200000, O_DIRECTORY}, {0400000, O_NOFOLLOW},
    {01000000, O_NOATIME}, {02000000, O_CLOEXEC}, {04000000, O_SYNC & ~O_DSYNC}, {010000000, O_PATH},
};
static long fl_g2h(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].g) == oflags[k].g) r |= oflags[k].h;
    if ((f & 020200000) == 020200000) r |= O_TMPFILE;
    return r;
}
static long fl_h2g(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].h) == oflags[k].h && oflags[k].h) r |= oflags[k].g;
    return r;
}
static void cv_stat(const struct stat*s, u8*g) {
    memset(g, 0, 144);
    *(u64*)(g + 0) = s->st_dev; *(u64*)(g + 8) = s->st_ino; *(u64*)(g + 16) = s->st_nlink;
    *(u32*)(g + 24) = s->st_mode; *(u32*)(g + 28) = PEMU_UID(s->st_uid); *(u32*)(g + 32) = PEMU_GID(s->st_gid);
    *(u64*)(g + 40) = s->st_rdev; *(s64*)(g + 48) = s->st_size; *(s64*)(g + 56) = s->st_blksize; *(s64*)(g + 64) = s->st_blocks;
    *(s64*)(g + 72) = s->st_atim.tv_sec; *(s64*)(g + 80) = s->st_atim.tv_nsec;
    *(s64*)(g + 88) = s->st_mtim.tv_sec; *(s64*)(g + 96) = s->st_mtim.tv_nsec;
    *(s64*)(g + 104) = s->st_ctim.tv_sec; *(s64*)(g + 112) = s->st_ctim.tv_nsec;
}
static long g_stat(long dirfd, const char*p, u8*out, long flags) {
    struct stat st;
    long r = pemu_callat(SYS_newfstatat, dirfd, p, !(flags & 0x100), (long) &st, flags, 0, 0);
    if (r == 0) cv_stat(&st, out);
    return r;
}
static int is_x64_elf(const char*p) {
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[20];
    ssize_t n = read(fd, h, 20);
    close(fd);
    return n == 20 && !memcmp(h, "\177ELF", 4) && h[4] == 2 && *(u16*)(h + 18) == EM_X86_64;
}
static long do_execve(const char*path, char**argv, char**envp) {
    char host[PEMU_HP], gprog[PATH_MAX]; char**av = argv; const char*hp = path, *gp = path;
    if (pemu_on) {                                         /* guest path -> host file, #! scripts expanded */
        long e = pemu_exec_resolve(path, argv, &av, host, gprog);
        if (e < 0) return e;
        hp = host; gp = gprog;
    }
    if (!is_x64_elf(hp)) return hs(SYS_execve, (long) hp, (long) av, (long) envp, 0, 0, 0);
    int n = 0; while (av && av[n]) n++;
    char**nv = calloc((size_t) n + 6 + (size_t) pemu_reexec_count(), sizeof *nv);
    int k = 0;
    nv[k++] = g_self;
    if (pemu_on) k = pemu_reexec_opts(nv, k);
    nv[k++] = "-0"; nv[k++] = n ? av[0] : (char*) gp; nv[k++] = (char*) gp;
    for (int i = 1; i < n; i++) nv[k++] = av[i];
    /* g_self is a HOST binary (bionic on Android). If the guest's LD_LIBRARY_PATH / LD_PRELOAD
       reach it, the host linker picks up guest libs (e.g. rootfs libm.so, a GNU ld script) and
       dies with "bad ELF magic". Hide those two from the host exec and hand them over through
       PEMU_SET_ENV, which main() applies to the guest environment. */
    int ne = 0; while (envp && envp[ne]) ne++;
    char**nenv = calloc((size_t) ne + 2, sizeof *nenv);
    const char*ldl = NULL, *ldp = NULL, *oldset = NULL;
    int m = 0;
    for (int i = 0; i < ne; i++) {
        if (!strncmp(envp[i], "LD_LIBRARY_PATH=", 16)) ldl = envp[i] + 16;
        else if (!strncmp(envp[i], "LD_PRELOAD=", 11)) ldp = envp[i] + 11;
        else if (!strncmp(envp[i], "PEMU_SET_ENV=", 13)) oldset = envp[i] + 13;
        else nenv[m++] = envp[i];
    }
    char*setv = NULL;
    if (ldl || ldp || oldset) {
        size_t cap = 32 + (ldl ? strlen(ldl) : 0) + (ldp ? strlen(ldp) : 0) + (oldset ? strlen(oldset) : 0);
        setv = malloc(cap);
        if (setv) {
            int o = snprintf(setv, cap, "PEMU_SET_ENV=");
            if (oldset && *oldset) o += snprintf(setv + o, cap - (size_t) o, "%s,", oldset);
            if (ldp) o += snprintf(setv + o, cap - (size_t) o, "LD_PRELOAD=%s,", ldp);
            if (ldl) o += snprintf(setv + o, cap - (size_t) o, "LD_LIBRARY_PATH=%s,", ldl);
            if (o > 0 && setv[o - 1] == ',') setv[o - 1] = 0;
            nenv[m++] = setv;
        }
    }
    nenv[m] = NULL;
    struct pemu_idsave ids; pemu_exec_ids(hp, &ids);          /* set-uid file: effective id 0 for the new image */
    long r = hs(SYS_execve, (long) g_self, (long) nv, (long) nenv, 0, 0, 0);
    pemu_exec_ids_undo(&ids);                                  /* still here: the exec failed */
    free(nv); free(nenv); free(setv);
    return r;
}

/* direct pass-through: guest x86_64 number -> host number (same arguments) */
/* less common pass-through syscalls; each guarded because headers differ per host */
#define P2(g, name) case g: return name;
static long pass_nr2(u64 n) {
    switch (n) {
#ifdef SYS_setxattr
    P2(188, SYS_setxattr) P2(189, SYS_lsetxattr) P2(190, SYS_fsetxattr) P2(191, SYS_getxattr) P2(192, SYS_lgetxattr)
    P2(193, SYS_fgetxattr) P2(194, SYS_listxattr) P2(195, SYS_llistxattr) P2(196, SYS_flistxattr)
    P2(197, SYS_removexattr) P2(198, SYS_lremovexattr) P2(199, SYS_fremovexattr)
#endif
    P2(140, SYS_getpriority) P2(141, SYS_setpriority) P2(142, SYS_sched_setparam) P2(143, SYS_sched_getparam)
    P2(144, SYS_sched_setscheduler) P2(145, SYS_sched_getscheduler) P2(146, SYS_sched_get_priority_max)
    P2(147, SYS_sched_get_priority_min) P2(148, SYS_sched_rr_get_interval) P2(149, SYS_mlock) P2(150, SYS_munlock)
    P2(151, SYS_mlockall) P2(152, SYS_munlockall) P2(135, SYS_personality) P2(165, SYS_mount) P2(166, SYS_umount2)
    P2(170, SYS_sethostname) P2(227, SYS_clock_settime) P2(254, SYS_inotify_add_watch) P2(255, SYS_inotify_rm_watch)
    P2(275, SYS_splice) P2(276, SYS_tee) P2(277, SYS_sync_file_range) P2(278, SYS_vmsplice) P2(283, SYS_timerfd_create)
    P2(286, SYS_timerfd_settime) P2(287, SYS_timerfd_gettime) P2(289, SYS_signalfd4) P2(309, SYS_getcpu)
    P2(222, SYS_timer_create) P2(223, SYS_timer_settime) P2(224, SYS_timer_gettime) P2(225, SYS_timer_getoverrun)
    P2(226, SYS_timer_delete) P2(131 + 1000, SYS_sync)
#ifdef SYS_close_range
    P2(436, SYS_close_range)
#endif
#ifdef SYS_pidfd_open
    P2(434, SYS_pidfd_open) P2(424, SYS_pidfd_send_signal)
#endif
/* more pass-through syscalls (structures have the same layout on x86_64 and the generic aarch64 ABI) */
#ifdef SYS_shmget
    P2(29, SYS_shmget)
#endif
#ifdef SYS_shmat
    P2(30, SYS_shmat)
#endif
#ifdef SYS_shmdt
    P2(67, SYS_shmdt)
#endif
#ifdef SYS_semget
    P2(64, SYS_semget)
#endif
#ifdef SYS_semop
    P2(65, SYS_semop)
#endif
#ifdef SYS_semtimedop
    P2(220, SYS_semtimedop)
#endif
#ifdef SYS_msgget
    P2(68, SYS_msgget)
#endif
#ifdef SYS_msgsnd
    P2(69, SYS_msgsnd)
#endif
#ifdef SYS_msgrcv
    P2(70, SYS_msgrcv)
#endif
#ifdef SYS_rt_sigqueueinfo
    P2(129, SYS_rt_sigqueueinfo)
#endif
#ifdef SYS_rt_tgsigqueueinfo
    P2(297, SYS_rt_tgsigqueueinfo)
#endif
#ifdef SYS_settimeofday
    P2(164, SYS_settimeofday)
#endif
#ifdef SYS_adjtimex
    P2(159, SYS_adjtimex)
#endif
#ifdef SYS_syslog
    P2(103, SYS_syslog)
#endif
#ifdef SYS_setdomainname
    P2(171, SYS_setdomainname)
#endif
#ifdef SYS_vhangup
    P2(153, SYS_vhangup)
#endif
#ifdef SYS_readahead
    P2(187, SYS_readahead)
#endif
#ifdef SYS_ioprio_set
    P2(251, SYS_ioprio_set)
#endif
#ifdef SYS_ioprio_get
    P2(252, SYS_ioprio_get)
#endif
#ifdef SYS_unshare
    P2(272, SYS_unshare)
#endif
#ifdef SYS_setns
    P2(308, SYS_setns)
#endif
#ifdef SYS_process_vm_readv
    P2(310, SYS_process_vm_readv)
#endif
#ifdef SYS_process_vm_writev
    P2(311, SYS_process_vm_writev)
#endif
#ifdef SYS_sched_setattr
    P2(314, SYS_sched_setattr)
#endif
#ifdef SYS_sched_getattr
    P2(315, SYS_sched_getattr)
#endif
#ifdef SYS_fanotify_init
    P2(300, SYS_fanotify_init)
#endif
#ifdef SYS_fanotify_mark
    P2(301, SYS_fanotify_mark)
#endif
#ifdef SYS_name_to_handle_at
    P2(303, SYS_name_to_handle_at)
#endif
#ifdef SYS_open_by_handle_at
    P2(304, SYS_open_by_handle_at)
#endif
#ifdef SYS_clock_adjtime
    P2(305, SYS_clock_adjtime)
#endif
#ifdef SYS_membarrier
    P2(324, SYS_membarrier)
#endif
#ifdef SYS_mlock2
    P2(325, SYS_mlock2)
#endif
#ifdef SYS_preadv2
    P2(327, SYS_preadv2)
#endif
#ifdef SYS_pwritev2
    P2(328, SYS_pwritev2)
#endif
#ifdef SYS_userfaultfd
    P2(323, SYS_userfaultfd)
#endif
#ifdef SYS_io_setup
    P2(206, SYS_io_setup)
#endif
#ifdef SYS_io_destroy
    P2(207, SYS_io_destroy)
#endif
#ifdef SYS_io_getevents
    P2(208, SYS_io_getevents)
#endif
#ifdef SYS_io_submit
    P2(209, SYS_io_submit)
#endif
#ifdef SYS_io_cancel
    P2(210, SYS_io_cancel)
#endif
#ifdef SYS_mq_open
    P2(240, SYS_mq_open)
#endif
#ifdef SYS_mq_unlink
    P2(241, SYS_mq_unlink)
#endif
#ifdef SYS_mq_timedsend
    P2(242, SYS_mq_timedsend)
#endif
#ifdef SYS_mq_timedreceive
    P2(243, SYS_mq_timedreceive)
#endif
#ifdef SYS_mq_notify
    P2(244, SYS_mq_notify)
#endif
#ifdef SYS_mq_getsetattr
    P2(245, SYS_mq_getsetattr)
#endif
#ifdef SYS_add_key
    P2(248, SYS_add_key)
#endif
#ifdef SYS_request_key
    P2(249, SYS_request_key)
#endif
#ifdef SYS_keyctl
    P2(250, SYS_keyctl)
#endif
#ifdef SYS_mbind
    P2(237, SYS_mbind)
#endif
#ifdef SYS_set_mempolicy
    P2(238, SYS_set_mempolicy)
#endif
#ifdef SYS_get_mempolicy
    P2(239, SYS_get_mempolicy)
#endif
#ifdef SYS_migrate_pages
    P2(256, SYS_migrate_pages)
#endif
#ifdef SYS_move_pages
    P2(279, SYS_move_pages)
#endif
#ifdef SYS_faccessat2
    P2(439, SYS_faccessat2)
#endif
#ifdef SYS_epoll_pwait2
    P2(441, SYS_epoll_pwait2)
#endif
#ifdef SYS_pidfd_getfd
    P2(438, SYS_pidfd_getfd)
#endif
#ifdef SYS_setfsuid
    P2(122, SYS_setfsuid)
#endif
#ifdef SYS_setfsgid
    P2(123, SYS_setfsgid)
#endif
#ifdef SYS_perf_event_open
    P2(298, SYS_perf_event_open)                  /* perf_event_attr has the same layout on every 64-bit arch; counters measure the emulator */
#endif
#ifdef SYS_kcmp
    P2(312, SYS_kcmp)
#endif
#ifdef SYS_bpf
    P2(321, SYS_bpf)                              /* union bpf_attr is arch independent and guest pointers are host pointers */
#endif
#ifdef SYS_io_pgetevents
    P2(333, SYS_io_pgetevents)
#endif
#ifdef SYS_open_tree
    P2(428, SYS_open_tree)
#endif
#ifdef SYS_move_mount
    P2(429, SYS_move_mount)
#endif
#ifdef SYS_fsopen
    P2(430, SYS_fsopen)
#endif
#ifdef SYS_fsconfig
    P2(431, SYS_fsconfig)
#endif
#ifdef SYS_fsmount
    P2(432, SYS_fsmount)
#endif
#ifdef SYS_fspick
    P2(433, SYS_fspick)
#endif
#ifdef SYS_process_madvise
    P2(440, SYS_process_madvise)
#endif
#ifdef SYS_mount_setattr
    P2(442, SYS_mount_setattr)
#endif
#ifdef SYS_quotactl_fd
    P2(443, SYS_quotactl_fd)
#endif
#ifdef SYS_landlock_create_ruleset
    P2(444, SYS_landlock_create_ruleset)
#endif
#ifdef SYS_landlock_add_rule
    P2(445, SYS_landlock_add_rule)
#endif
#ifdef SYS_landlock_restrict_self
    P2(446, SYS_landlock_restrict_self)
#endif
#ifdef SYS_memfd_secret
    P2(447, SYS_memfd_secret)
#endif
#ifdef SYS_process_mrelease
    P2(448, SYS_process_mrelease)
#endif
#ifdef SYS_futex_waitv
    P2(449, SYS_futex_waitv)
#endif
#ifdef SYS_set_mempolicy_home_node
    P2(450, SYS_set_mempolicy_home_node)
#endif
#ifdef SYS_cachestat
    P2(451, SYS_cachestat)
#endif
#ifdef SYS_fchmodat2
    P2(452, SYS_fchmodat2)
#endif
#ifdef SYS_futex_wake
    P2(454, SYS_futex_wake)
#endif
#ifdef SYS_futex_wait
    P2(455, SYS_futex_wait)
#endif
#ifdef SYS_futex_requeue
    P2(456, SYS_futex_requeue)
#endif
#ifdef SYS_statmount
    P2(457, SYS_statmount)
#endif
#ifdef SYS_listmount
    P2(458, SYS_listmount)
#endif
#ifdef SYS_lsm_get_self_attr
    P2(459, SYS_lsm_get_self_attr)
#endif
#ifdef SYS_lsm_set_self_attr
    P2(460, SYS_lsm_set_self_attr)
#endif
#ifdef SYS_lsm_list_modules
    P2(461, SYS_lsm_list_modules)
#endif
    default: return -1;
    }
}

/* direct pass-through: see pass_nr2 for the rest */
static long pass_nr(u64 n) {
    switch (n) {
    case 0: return SYS_read; case 1: return SYS_write; case 3: return SYS_close; case 8: return SYS_lseek;
    case 9: return SYS_mmap; case 10: return SYS_mprotect; case 11: return SYS_munmap; case 16: return SYS_ioctl;
    case 17: return SYS_pread64; case 18: return SYS_pwrite64; case 19: return SYS_readv; case 20: return SYS_writev;
    case 24: return SYS_sched_yield; case 25: return SYS_mremap; case 26: return SYS_msync; case 27: return SYS_mincore;
    case 28: return SYS_madvise; case 32: return SYS_dup; case 35: return SYS_nanosleep; case 36: return SYS_getitimer;
    case 38: return SYS_setitimer; case 39: return SYS_getpid; case 40: return SYS_sendfile; case 41: return SYS_socket;
    case 42: return SYS_connect; case 43: return SYS_accept; case 44: return SYS_sendto; case 45: return SYS_recvfrom;
    case 46: return SYS_sendmsg; case 47: return SYS_recvmsg; case 48: return SYS_shutdown; case 49: return SYS_bind;
    case 50: return SYS_listen; case 51: return SYS_getsockname; case 52: return SYS_getpeername;
    case 53: return SYS_socketpair; case 54: return SYS_setsockopt; case 55: return SYS_getsockopt;
    case 61: return SYS_wait4; case 62: return SYS_kill; case 73: return SYS_flock; case 74: return SYS_fsync;
    case 75: return SYS_fdatasync; case 76: return SYS_truncate; case 77: return SYS_ftruncate; case 79: return SYS_getcwd;
    case 80: return SYS_chdir; case 81: return SYS_fchdir; case 91: return SYS_fchmod; case 93: return SYS_fchown;
    case 95: return SYS_umask; case 96: return SYS_gettimeofday; case 97: return SYS_getrlimit; case 98: return SYS_getrusage;
    case 99: return SYS_sysinfo; case 100: return SYS_times; case 102: return SYS_getuid; case 104: return SYS_getgid;
    case 105: return SYS_setuid; case 106: return SYS_setgid; case 107: return SYS_geteuid; case 108: return SYS_getegid;
    case 109: return SYS_setpgid; case 110: return SYS_getppid; case 112: return SYS_setsid; case 113: return SYS_setreuid;
    case 114: return SYS_setregid; case 115: return SYS_getgroups; case 116: return SYS_setgroups;
    case 117: return SYS_setresuid; case 118: return SYS_getresuid; case 119: return SYS_setresgid;
    case 120: return SYS_getresgid; case 121: return SYS_getpgid; case 124: return SYS_getsid; case 125: return SYS_capget;
    case 126: return SYS_capset; case 127: return SYS_rt_sigpending; case 128: return SYS_rt_sigtimedwait;
    case 130: return SYS_rt_sigsuspend; case 137: return SYS_statfs; case 138: return SYS_fstatfs; case 157: return SYS_prctl;
    case 160: return SYS_setrlimit; case 161: return SYS_chroot; case 162: return SYS_sync; case 186: return SYS_gettid;
    case 200: return SYS_tkill; case 202: return SYS_futex; case 203: return SYS_sched_setaffinity;
    case 204: return SYS_sched_getaffinity; case 217: return SYS_getdents64; case 218: return SYS_set_tid_address;
    case 221: return SYS_fadvise64; case 228: return SYS_clock_gettime; case 229: return SYS_clock_getres;
    case 230: return SYS_clock_nanosleep; case 233: return SYS_epoll_ctl; case 234: return SYS_tgkill;
    case 258: return SYS_mkdirat; case 259: return SYS_mknodat; case 260: return SYS_fchownat; case 263: return SYS_unlinkat;
    case 264: return SYS_renameat; case 265: return SYS_linkat; case 266: return SYS_symlinkat;
    case 267: return SYS_readlinkat; case 268: return SYS_fchmodat; case 269: return SYS_faccessat;
    case 270: return SYS_pselect6; case 271: return SYS_ppoll; case 273: return SYS_set_robust_list;
    case 274: return SYS_get_robust_list; case 280: return SYS_utimensat; case 285: return SYS_fallocate;
    case 288: return SYS_accept4; case 290: return SYS_eventfd2; case 291: return SYS_epoll_create1;
    case 293: return SYS_pipe2; case 294: return SYS_inotify_init1; case 302: return SYS_prlimit64;
    case 316: return SYS_renameat2; case 318: return SYS_getrandom; case 319: return SYS_memfd_create;
    case 332: return SYS_statx; case 247: return SYS_waitid; case 306: return SYS_syncfs; case 326: return SYS_copy_file_range;
    case 295: return SYS_preadv; case 296: return SYS_pwritev; case 299: return SYS_recvmmsg; case 307: return SYS_sendmmsg;
    case 5: return -2;
    default: return pass_nr2(n);
    }
}

/* ---- threads: every guest thread is one host pthread running its own interpreter loop ---- */
static void run(CPU*c);
typedef struct { CPU cpu; u64 flags, ptid, ctid; volatile int*tidout; } ThArg;
static void thread_exit(CPU*c) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_thread_exit();
#endif
    if (g_clear_tid) {
        __atomic_store_n((u32*) g_clear_tid, 0, __ATOMIC_SEQ_CST);
        syscall(SYS_futex, g_clear_tid, FUTEX_WAKE, 1, NULL, NULL, 0);
    }
    free(c);                                              /* c is the ThArg (cpu is its first member) */
    pthread_exit(NULL);
}
static void*thread_main(void*p) {
    ThArg*t = p; CPU*c = &t->cpu;
    g_cur = c; g_is_thread = 1;
    int tid = (int) syscall(SYS_gettid);
    if (t->flags & 0x00100000) *(u32*) t->ptid = (u32) tid;            /* PARENT_SETTID, before the guest runs */
    if (t->flags & 0x01000000) *(u32*) t->ctid = (u32) tid;            /* CHILD_SETTID */
    if (t->flags & 0x00200000) g_clear_tid = t->ctid;                  /* CHILD_CLEARTID */
    __atomic_store_n(t->tidout, tid, __ATOMIC_SEQ_CST);                 /* t may be freed once the thread exits: do not touch it after run() */
    run(c);
    return NULL;
}
/* From now on emulate x86 memory ordering: another agent (a second guest thread, or the kernel writing an io_uring ring) is
 * about to touch guest memory concurrently, and an aarch64 host would otherwise reorder the interpreted and translated accesses. */
static void mt_barriers(void) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_mt_enable();                        /* translated code from now on carries the memory barriers of x86 ordering */
#endif
    __atomic_store_n(&g_mt, 1, __ATOMIC_SEQ_CST);        /* see mrd/mwr */
}
static long clone_thread(CPU*c, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls) {
    if ((flags & 0x100) == 0 || (flags & 0x800) == 0) return -EINVAL;   /* threads need CLONE_VM | CLONE_SIGHAND */
    ThArg*t = malloc(sizeof *t);
    if (!t) return -ENOMEM;
    mt_barriers();
    t->cpu = *c; t->flags = flags; t->ptid = ptid; t->ctid = ctid; volatile int tidv = 0; t->tidout = &tidv;
    if (stack) t->cpu.r[RSP] = stack;
    if (flags & 0x00080000) t->cpu.fs = tls;                            /* SETTLS */
    t->cpu.r[RAX] = 0;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 1UL << 20);
    pthread_t th; int e = pthread_create(&th, &at, thread_main, t);
    pthread_attr_destroy(&at);
    if (e) { free(t); return -e; }
    int tid;
    while (!(tid = __atomic_load_n(&tidv, __ATOMIC_SEQ_CST))) sched_yield();
    return tid;
}

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
/* MAP_32BIT: x86-64 guests ask for an address in the first 2 GiB (JIT code, some allocators). The aarch64 host has no such
 * flag, so look for a free range there ourselves; the guest is mapped 1:1, so any address that mmap accepts is valid. */
static long mmap32(u64 len, u64 prot, u64 flags, u64 fd, u64 off) {
    static u64 next = 0x10000000ULL;
    const u64 lo = 0x10000000ULL, hi = 0x80000000ULL, pg = (u64) sysconf(_SC_PAGESIZE);
    u64 n = (len + pg - 1) & ~(pg - 1), a = __atomic_load_n(&next, __ATOMIC_RELAXED);
    if (!n) return -EINVAL;
    for (int k = 0; k < 8192; k++) {
        if (a < lo || a + n > hi) a = lo;
        long r = hs(SYS_mmap, (long) a, (long) len, (long) prot, (long) (flags | MAP_FIXED_NOREPLACE), (long) fd, (long) off);
        if (r == (long) a) { __atomic_store_n(&next, a + n, __ATOMIC_RELAXED); return r; }
        if (r >= 0) munmap((void*) r, n);                 /* a kernel without MAP_FIXED_NOREPLACE took it as a hint */
        else if (r != -EEXIST) return r;
        a += n < 0x10000 ? 0x10000 : n;
    }
    return -ENOMEM;
}

/* /proc/cpuinfo as an x86 CPU. The host's file lists ARM flags (asimd, fp, ...), so a guest that decides what the CPU can do by
 * parsing it finds no sse3/pni and refuses to run. The text is consistent with cpuid (see cpuid()). */
static const char cpuinfo_flags[] = "fpu tsc cx8 cmov mmx fxsr sse sse2 syscall nx lm constant_tsc nopl pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm";
static long cpuinfo_fd(long hostflags) {
    long n = sysconf(_SC_NPROCESSORS_ONLN); if (n < 1) n = 1; if (n > 256) n = 256;
    size_t cap = (size_t) n * 1100 + 64, len = 0; char*b = malloc(cap);
    if (!b) return -ENOMEM;
    for (int i = 0; i < n; i++)
        len += (size_t) snprintf(b + len, cap - len,
            "processor\t: %d\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 26\nmodel name\t: " PEMU_BRAND "\n"
            "stepping\t: 5\ncpu MHz\t\t: " PEMU_MHZ_STR "\ncache size\t: 8192 KB\nphysical id\t: 0\nsiblings\t: %ld\ncore id\t\t: %d\ncpu cores\t: %ld\n"
            "apicid\t\t: %d\ninitial apicid\t: %d\nfpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: 4\nwp\t\t: yes\nflags\t\t: %s\n"
            "bogomips\t: " PEMU_BOGO_STR "\nclflush size\t: 64\ncache_alignment\t: 64\naddress sizes\t: 40 bits physical, 48 bits virtual\npower management:\n\n",
            i, n, i, n, i, i, cpuinfo_flags);
    int cloexec = (hostflags & O_CLOEXEC) != 0; long fd = -1;
#ifdef SYS_memfd_create
    fd = syscall(SYS_memfd_create, "pemu-cpuinfo", cloexec ? 1 /*MFD_CLOEXEC*/ : 0);
    if (fd >= 0) {
        if (write((int) fd, b, len) != (ssize_t) len || lseek((int) fd, 0, SEEK_SET) < 0) { close((int) fd); fd = -1; }
    }
#endif
    if (fd < 0 && len <= 60000) {                       /* no memfd (blocked, old kernel): a pipe is enough for reading it once */
        int p[2];
        if (pipe2(p, cloexec ? O_CLOEXEC : 0) == 0) {
            if (write(p[1], b, len) == (ssize_t) len) { close(p[1]); fd = p[0]; } else { close(p[0]); close(p[1]); }
        }
    }
    free(b);
    return fd >= 0 ? fd : -EACCES;
}
static int is_cpuinfo(const char*p, long hostflags) { return p && !strcmp(p, "/proc/cpuinfo") && (hostflags & O_ACCMODE) == O_RDONLY; }

/* getdents(2): the host has only getdents64. A struct linux_dirent record has the same length as a linux_dirent64 one (the name
 * starts one byte earlier and d_type moves to the last byte of the record), so the host buffer is converted in place. */
static long g_getdents(long fd, u8*buf, long count) {
    long r = hs(SYS_getdents64, fd, (long) buf, count, 0, 0, 0);
    for (long off = 0; r > 0 && off < r; ) {
        u8*p = buf + off; u32 len = *(u16u*)(p + 16); u8 type = p[18];
        if (len < 24 || off + len > r) break;
        memmove(p + 18, p + 19, strlen((char*) p + 19) + 1);
        p[len - 1] = type;
        off += len;
    }
    return r;
}

/* execveat(dirfd, path, argv, envp, flags) on top of do_execve: a path is needed to tell guest ELF files from host ones */
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

/* clone / fork / vfork / clone3 for a guest that does not share the address space (CLONE_THREAD goes to clone_thread). isclone: the
 * stack, tls and tid arguments are meaningful. pidfd: address for CLONE_PIDFD (clone3 only), or 0. */
static long clone_common(CPU*c, int isclone, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls, u64 pidfd, int*noset) {
    if (flags & 0x10000 /*CLONE_THREAD*/) return clone_thread(c, flags, stack, ptid, ctid, tls);
#ifdef HAVE_JIT
    if (g_jit_on) jit_atfork(0);                  /* no other thread may hold the JIT lock across the fork */
#endif
    long pid = fork(); int fe = errno;
#ifdef HAVE_JIT
    if (g_jit_on) jit_atfork(pid == 0 ? 2 : 1);
#endif
    if (pid < 0) return -fe;
    if (pid == 0) {
        pemu_lk = 0;                                                                 /* a lock held by another thread at fork time stays locked in the child */
        if (isclone && stack) c->r[RSP] = stack;
        if (isclone && (flags & 0x01000000)) *(u32*) ctid = (u32) getpid();      /* CHILD_SETTID */
        if (isclone && (flags & 0x00080000)) c->fs = tls;                         /* SETTLS */
        if (flags & (1ULL << 32)) {                                               /* CLONE_CLEAR_SIGHAND (clone3): handled signals go back to SIG_DFL */
            for (int sg = 1; sg <= 64; sg++) {
                if (sg == SIGKILL || sg == SIGSTOP || sg == SIGSYS || gsa[sg].handler <= 1) continue;
                struct sigaction sd; memset(&sd, 0, sizeof sd); sd.sa_handler = SIG_DFL;
                sigaction(sg, &sd, NULL); memset(&gsa[sg], 0, sizeof gsa[sg]);
            }
        }
        c->r[RAX] = 0; *noset = 1;
    } else {
        if (isclone && (flags & 0x00100000)) *(u32*) ptid = (u32) pid;           /* PARENT_SETTID */
        if (pidfd) {
#ifdef SYS_pidfd_open
            long fd = hs(SYS_pidfd_open, pid, 0, 0, 0, 0, 0);
            if (fd < 0) { kill((pid_t) pid, SIGKILL); waitpid((pid_t) pid, NULL, 0); return fd; }
            *(int*) pidfd = (int) fd;
#else
            kill((pid_t) pid, SIGKILL); waitpid((pid_t) pid, NULL, 0); return -ENOSYS;
#endif
        }
    }
    return pid;
}

/* clone3(struct clone_args *args, size_t size): translated to the same paths as clone. Things the emulator cannot give are refused
 * instead of silently ignored: namespaces (EPERM, as for an unprivileged process), set_tid, CLONE_INTO_CGROUP. CLONE_CLEAR_SIGHAND is
 * what glibc's posix_spawn passes. */
static long do_clone3(CPU*c, const u8*uargs, u64 size, int*noset) {
    struct { u64 flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup; } ca;
    if (!uargs) return -EFAULT;
    if (size < 64 /*CLONE_ARGS_SIZE_VER0*/) return -EINVAL;
    if (size > 4096) return -E2BIG;
    memset(&ca, 0, sizeof ca);
    memcpy(&ca, uargs, size < sizeof ca ? (size_t) size : sizeof ca);
    for (u64 i = sizeof ca; i < size; i++) if (uargs[i]) return -E2BIG;       /* unknown trailing fields must be zero */
    if ((ca.flags >> 32) & ~1ULL) return -EINVAL;                              /* CLONE_INTO_CGROUP and unknown flags */
    if ((ca.flags & (1ULL << 32)) && (ca.flags & 0x800 /*CLONE_SIGHAND*/)) return -EINVAL;
    if (ca.exit_signal > 0xff || ca.set_tid_size) return -EINVAL;
    if (ca.flags & 0x7e0200a0ULL) return -EPERM;                               /* CLONE_NEW{NS,CGROUP,UTS,IPC,USER,PID,NET,TIME} */
    if ((ca.stack != 0) != (ca.stack_size != 0)) return -EINVAL;
    u64 sp = ca.stack ? ca.stack + ca.stack_size : 0;                          /* clone3 passes the lowest address, clone the top */
    if (ca.flags & 0x10000 /*CLONE_THREAD*/) {
        if (ca.exit_signal || (ca.flags & 0x1000 /*CLONE_PIDFD*/)) return -EINVAL;
        return clone_thread(c, ca.flags, sp, ca.parent_tid, ca.child_tid, ca.tls);
    }
    return clone_common(c, 1, ca.flags, sp, ca.parent_tid, ca.child_tid, ca.tls, (ca.flags & 0x1000) ? ca.pidfd : 0, noset);
}

/* ---- built-in proot hook: guest x86_64 syscalls that name a path or an identity.
 * Returns 1 and sets *res when it handled the call, 0 to let the normal translation run. */
static int pemu_x64_sys(u64 n, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, long *res) {
#define RET(x) do { *res = (x); return 1; } while (0)
    switch (n) {                                       /* hostname / domainname with -k */
    case 41: if ((int) a0 == 16 && (int) a2 == 9) RET(-EPROTONOSUPPORT);   /* NETLINK_AUDIT: shadow (useradd) and sudo treat this as "no audit" and go on */
             break;
    case 170: case 171: { long r; if (pemu_set_uts_name(n == 171, (const char*) a0, (size_t) a1, &r)) RET(r);
                          RET(pemu_force(hs(n == 170 ? SYS_sethostname : SYS_setdomainname, (long) a0, (long) a1, 0, 0, 0, 0))); }
    }
    if (pemu_fakeroot) switch (n) {                    /* identity: proot's fake_id0 */
    case 102: RET(pemu_idcall(PEMU_GETUID, 0, 0, 0));      case 107: RET(pemu_idcall(PEMU_GETEUID, 0, 0, 0));
    case 104: RET(pemu_idcall(PEMU_GETGID, 0, 0, 0));      case 108: RET(pemu_idcall(PEMU_GETEGID, 0, 0, 0));
    case 118: RET(pemu_idcall(PEMU_GETRESUID, a0, a1, a2)); case 120: RET(pemu_idcall(PEMU_GETRESGID, a0, a1, a2));
    case 105: RET(pemu_idcall(PEMU_SETUID, a0, 0, 0));     case 106: RET(pemu_idcall(PEMU_SETGID, a0, 0, 0));
    case 113: RET(pemu_idcall(PEMU_SETREUID, a0, a1, 0));  case 114: RET(pemu_idcall(PEMU_SETREGID, a0, a1, 0));
    case 117: RET(pemu_idcall(PEMU_SETRESUID, a0, a1, a2)); case 119: RET(pemu_idcall(PEMU_SETRESGID, a0, a1, a2));
    case 122: RET(pemu_idcall(PEMU_SETFSUID, a0, 0, 0));   case 123: RET(pemu_idcall(PEMU_SETFSGID, a0, 0, 0));
    case 93: RET(pemu_force_acc(hs(SYS_fchown, (long) a0, (long) pemu_chown_id(a1, pemu_uid, getuid()), (long) pemu_chown_id(a2, pemu_gid, getgid()), 0, 0, 0)));
    case 91: RET(pemu_force(hs(SYS_fchmod, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 190: RET(pemu_force(hs(SYS_fsetxattr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0)));
    case 126: RET(pemu_force_ns(hs(SYS_capset, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 116: RET(pemu_force_ns(hs(SYS_setgroups, (long) a0, (long) a1, 0, 0, 0, 0)));
    }
    if (!pemu_on) {                                    /* no rootfs: only what a fake root needs forced */
        if (!pemu_fakeroot) return 0;
        const char *q0 = (const char*) a0, *q1 = (const char*) a1;
        switch (n) {
        case 92:  RET(pemu_callat(SYS_fchownat, AT_FDCWD, q0, 1, (long) a1, (long) a2, 0, 0));
        case 94:  RET(pemu_callat(SYS_fchownat, AT_FDCWD, q0, 0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0));
        case 260: RET(pemu_callat(SYS_fchownat, (long) a0, q1, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
        case 133: RET(pemu_callat(SYS_mknodat, AT_FDCWD, q0, 0, (long) a1, (long) a2, 0, 0));
        case 259: RET(pemu_callat(SYS_mknodat, (long) a0, q1, 0, (long) a2, (long) a3, 0, 0));
        case 90:  RET(pemu_callat(SYS_fchmodat, AT_FDCWD, q0, 1, (long) a1, 0, 0, 0));
        case 268: RET(pemu_callat(SYS_fchmodat, (long) a0, q1, 1, (long) a2, 0, 0, 0));
        case 188: RET(pemu_call1(SYS_setxattr, q0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        case 189: RET(pemu_call1(SYS_lsetxattr, q0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        }
        return 0;
    }
    const char *p0 = (const char*) a0, *p1 = (const char*) a1;
    switch (n) {
    case 2:   { long hf = fl_g2h((long) a1); if (is_cpuinfo(p0, hf)) return 0;
                RET(pemu_open(AT_FDCWD, p0, hf, (long) a2, 0)); }
    case 85:  RET(pemu_open(AT_FDCWD, p0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0));
    case 257: { long hf = fl_g2h((long) a2); if (is_cpuinfo(p1, hf)) return 0;
                RET(pemu_open((long) a0, p1, hf, (long) a3, 0)); }
    case 437: { u64 how[8] = {0}; u64 sz = a3;
                if (sz < 24) RET(-EINVAL);
                if (sz > sizeof how) RET(-E2BIG);
                memcpy(how, (const void*) a2, (size_t) sz); how[0] = (u64) fl_g2h((long) how[0]);
                if (is_cpuinfo(p1, (long) how[0])) return 0;
                RET(pemu_openat2((long) a0, p1, how, sz)); }
    case 452: RET(pemu_callat(452, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));   /* fchmodat2 */
    case 21:  RET(pemu_callat(SYS_faccessat, AT_FDCWD, p0, 1, (long) a1, 0, 0, 0));
    case 269: RET(pemu_callat(SYS_faccessat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 439: RET(pemu_callat(SYS_faccessat2, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 76:  RET(pemu_call1(SYS_truncate, p0, 1, (long) a1, 0, 0, 0, 0));
    case 79:  RET(pemu_getcwd((char*) a0, (long) a1));
    case 80:  RET(pemu_chdir(p0));
    case 81:  RET(pemu_fchdir((long) a0));
    case 161: RET(pemu_chroot(p0));
    case 165: RET(pemu_mount(p0, p1, (unsigned long) a3));       /* mount: only MS_BIND (edits the bind table) */
    case 166: RET(pemu_umount(p0));                              /* umount2 */
#ifdef SYS_fanotify_mark
    case 301: RET(pemu_fanotify_mark(SYS_fanotify_mark, (long) a0, (long) a1, (long) a2, (long) a3, (const char*) a4));
#endif
    case 82:  RET(pemu_call2at(SYS_renameat, AT_FDCWD, p0, 0, AT_FDCWD, p1, 0, 0));
    case 264: RET(pemu_call2at(SYS_renameat, (long) a0, p1, 0, (long) a2, (const char*) a3, 0, 0));
    case 316: RET(pemu_call2at(SYS_renameat2, (long) a0, p1, 0, (long) a2, (const char*) a3, 0, (long) a4));
    case 83:  RET(pemu_callat(SYS_mkdirat, AT_FDCWD, p0, 0, (long) a1, 0, 0, 0));
    case 258: RET(pemu_callat(SYS_mkdirat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 84:  RET(pemu_callat(SYS_unlinkat, AT_FDCWD, p0, 0, AT_REMOVEDIR, 0, 0, 0));
    case 87:  RET(pemu_callat(SYS_unlinkat, AT_FDCWD, p0, 0, 0, 0, 0, 0));
    case 263: RET(pemu_callat(SYS_unlinkat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 86:  RET(pemu_call2at(SYS_linkat, AT_FDCWD, p0, 0, AT_FDCWD, p1, 0, 0));
    case 265: RET(pemu_call2at(SYS_linkat, (long) a0, p1, !!(a4 & 0x400), (long) a2, (const char*) a3, 0, (long) a4));
    case 88:  RET(pemu_symlinkat(p0, AT_FDCWD, p1));
    case 266: RET(pemu_symlinkat(p0, (long) a1, (const char*) a2));
    case 89:  RET(pemu_readlinkat(AT_FDCWD, p0, (char*) a1, (long) a2));
    case 267: RET(pemu_readlinkat((long) a0, p1, (char*) a2, (long) a3));
    case 90:  RET(pemu_callat(SYS_fchmodat, AT_FDCWD, p0, 1, (long) a1, 0, 0, 0));
    case 268: RET(pemu_callat(SYS_fchmodat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 133: RET(pemu_callat(SYS_mknodat, AT_FDCWD, p0, 0, (long) a1, (long) a2, 0, 0));
    case 259: RET(pemu_callat(SYS_mknodat, (long) a0, p1, 0, (long) a2, (long) a3, 0, 0));
    case 280: RET(pemu_callat(SYS_utimensat, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 332: { long r = pemu_callat(SYS_statx, (long) a0, p1, !(a2 & 0x100), (long) a2, (long) a3, (long) a4, 0);
                if (r == 0 && pemu_fakeroot) { u32 *b = (u32*) a4; b[5] = PEMU_UID(b[5]); b[6] = PEMU_GID(b[6]); }
                RET(r); }
    case 137: RET(pemu_call1(SYS_statfs, p0, 1, (long) a1, 0, 0, 0, 0));
#ifdef SYS_setxattr
    case 188: RET(pemu_call1(SYS_setxattr, p0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 189: RET(pemu_call1(SYS_lsetxattr, p0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 191: RET(pemu_call1(SYS_getxattr, p0, 1, (long) a1, (long) a2, (long) a3, 0, 0));
    case 192: RET(pemu_call1(SYS_lgetxattr, p0, 0, (long) a1, (long) a2, (long) a3, 0, 0));
    case 194: RET(pemu_call1(SYS_listxattr, p0, 1, (long) a1, (long) a2, 0, 0, 0));
    case 195: RET(pemu_call1(SYS_llistxattr, p0, 0, (long) a1, (long) a2, 0, 0, 0));
    case 197: RET(pemu_call1(SYS_removexattr, p0, 1, (long) a1, 0, 0, 0, 0));
    case 198: RET(pemu_call1(SYS_lremovexattr, p0, 0, (long) a1, 0, 0, 0, 0));
#endif
    case 92:  RET(pemu_callat(SYS_fchownat, AT_FDCWD, p0, 1, (long) a1, (long) a2, 0, 0));
    case 94:  RET(pemu_callat(SYS_fchownat, AT_FDCWD, p0, 0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0));
    case 260: RET(pemu_callat(SYS_fchownat, (long) a0, p1, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
    case 254: { char host[PEMU_HP]; long r = pemu_path(AT_FDCWD, p1, !(a2 & 0x02000000), host, sizeof host, NULL);
                if (r < 0) RET(r);
                RET(hs(SYS_inotify_add_watch, (long) a0, (long) (r == 0 ? host : p1), (long) a2, 0, 0, 0)); }
    case 49:  return pemu_sock_hook(PEMU_S_BIND, a0, a1, a2, a3, a4, a5, res);
    case 42:  return pemu_sock_hook(PEMU_S_CONNECT, a0, a1, a2, a3, a4, a5, res);
    case 51:  return pemu_sock_hook(PEMU_S_GETSOCKNAME, a0, a1, a2, a3, a4, a5, res);
    case 52:  return pemu_sock_hook(PEMU_S_GETPEERNAME, a0, a1, a2, a3, a4, a5, res);
    case 44:  return pemu_sock_hook(PEMU_S_SENDTO, a0, a1, a2, a3, a4, a5, res);
    case 45:  return pemu_sock_hook(PEMU_S_RECVFROM, a0, a1, a2, a3, a4, a5, res);
    case 46:  return pemu_sock_hook(PEMU_S_SENDMSG, a0, a1, a2, a3, a4, a5, res);
    case 47:  return pemu_sock_hook(PEMU_S_RECVMSG, a0, a1, a2, a3, a4, a5, res);
    case 307: return pemu_sock_hook(PEMU_S_SENDMMSG, a0, a1, a2, a3, a4, a5, res);
    case 43:  return pemu_sock_hook(PEMU_S_ACCEPT, a0, a1, a2, a3, a4, a5, res);
    case 288: return pemu_sock_hook(PEMU_S_ACCEPT4, a0, a1, a2, a3, a4, a5, res);
    }
    return 0;
#undef RET
}

static long gsys(CPU*c, int*noset) {
    u64 n = c->r[RAX], a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10], a4 = c->r[8], a5 = c->r[9];
    long r, nr;
    if ((pemu_on || pemu_fakeroot || pemu_kset) && pemu_x64_sys(n, a0, a1, a2, a3, a4, a5, &r)) return r;
    switch (n) {
#ifdef HAVE_JIT
    case 231: jit_report(); pemu_exit_hook(); return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#else
    case 231: pemu_exit_hook(); return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#endif
    case 60:
        if (g_is_thread) thread_exit(c);                /* does not return */
#ifdef HAVE_JIT
        jit_report();
#endif
        pemu_exit_hook();
        return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 2: { long hf = fl_g2h((long) a1); if (is_cpuinfo((const char*) a0, hf)) return cpuinfo_fd(hf);
              return hs(SYS_openat, AT_FDCWD, (long) a0, hf, (long) a2, 0, 0); }
    case 85: return hs(SYS_openat, AT_FDCWD, (long) a0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0, 0);
    case 257: { long hf = fl_g2h((long) a2); if (is_cpuinfo((const char*) a1, hf)) return cpuinfo_fd(hf);
                return hs(SYS_openat, (long) a0, (long) a1, hf, (long) a3, 0, 0); }
#ifdef SYS_openat2
    case 437: {                                                                                             /* openat2: how->flags holds O_* flags, which differ between x86_64 and arm64 */
        u64 how[8] = {0}; u64 sz = a3;
        if (sz < 24) return -EINVAL;
        if (sz > sizeof how) return -E2BIG;
        memcpy(how, (const void*) a2, (size_t) sz); how[0] = (u64) fl_g2h((long) how[0]);
        if (is_cpuinfo((const char*) a1, (long) how[0])) return cpuinfo_fd((long) how[0]);
        return hs(SYS_openat2, (long) a0, (long) a1, (long) how, (long) sz, 0, 0); }
#endif
    case 78: return g_getdents((long) a0, (u8*) a1, (long) a2);
    case 101: return -EPERM;                          /* ptrace: the host would trace the emulator (aarch64 registers, no guest view); refused like a container whose seccomp profile blocks it */
    case 155: case 163: case 167: case 168: case 169: case 172: case 173: case 175: case 176: case 246: case 313: case 320:
        return -EPERM;                                /* pivot_root, acct, swapon/off, reboot, iopl, ioperm, init/finit/delete_module, kexec_load/kexec_file_load: never act on the host */
    case 4: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, 0);
    case 6: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, AT_SYMLINK_NOFOLLOW);
    case 262: return g_stat((long) a0, (const char*) a1, (u8*) a2, (long) a3);
    case 5: { struct stat st; r = hs(SYS_fstat, (long) a0, (long) &st, 0, 0, 0, 0); if (r == 0) { l2s_fix_fd((int) a0, &st); cv_stat(&st, (u8*) a1); } return r; }
    case 7: { struct timespec ts, *tp = NULL; if ((int) a2 >= 0) { ts.tv_sec = (int) a2 / 1000; ts.tv_nsec = ((int) a2 % 1000) * 1000000L; tp = &ts; }
              return hs(SYS_ppoll, (long) a0, (long) a1, (long) tp, 0, 8, 0); }
    case 21: return hs(SYS_faccessat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 22: return hs(SYS_pipe2, (long) a0, 0, 0, 0, 0, 0);
    case 43: return hs(SYS_accept4, (long) a0, (long) a1, (long) a2, 0, 0, 0);   /* accept == accept4 with flags 0. A plain accept (aarch64 202) is not in Android's app seccomp allowlist (bionic uses accept4), so it would come back ENOSYS and wineserver would spin on a ready socket */
    case 23: { struct timespec ts, *tp = NULL; if (a4) { struct timeval*tv = (struct timeval*) a4; ts.tv_sec = tv->tv_sec; ts.tv_nsec = tv->tv_usec * 1000L; tp = &ts; }
               return hs(SYS_pselect6, (long) a0, (long) a1, (long) a2, (long) a3, (long) tp, 0); }
    case 33: if (a0 == a1) return hs(SYS_fcntl, (long) a0, F_GETFD, 0, 0, 0, 0) < 0 ? -EBADF : (long) a1;
             return hs(SYS_dup3, (long) a0, (long) a1, 0, 0, 0, 0);
    case 292: return hs(SYS_dup3, (long) a0, (long) a1, fl_g2h((long) a2) & O_CLOEXEC, 0, 0, 0);
    case 34: return hs(SYS_ppoll, 0, 0, 0, 0, 8, 0);
    case 37: { struct itimerval it = {{0, 0}, {(time_t) a0, 0}}; return hs(SYS_setitimer, ITIMER_REAL, (long) &it, 0, 0, 0, 0); }
    case 82: return hs(SYS_renameat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 83: return hs(SYS_mkdirat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 84: return hs(SYS_unlinkat, AT_FDCWD, (long) a0, AT_REMOVEDIR, 0, 0, 0);
    case 86: return hs(SYS_linkat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 87: return hs(SYS_unlinkat, AT_FDCWD, (long) a0, 0, 0, 0, 0);
    case 88: return hs(SYS_symlinkat, (long) a0, AT_FDCWD, (long) a1, 0, 0, 0);
    case 89: return hs(SYS_readlinkat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 90: return hs(SYS_fchmodat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 92: return hs(SYS_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 94: return hs(SYS_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0);
    case 235: { struct timespec ts[2], *tp = NULL; if (a1) { struct timeval*tv = (struct timeval*) a1; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return pemu_callat(SYS_utimensat, AT_FDCWD, (const char*) a0, 1, (long) tp, 0, 0, 0); }
    case 201: { struct timespec ts; r = hs(SYS_clock_gettime, CLOCK_REALTIME, (long) &ts, 0, 0, 0, 0); if (r == 0 && a0) *(s64*) a0 = ts.tv_sec; return r == 0 ? (long) ts.tv_sec : r; }
    case 72:
        if (a1 == F_GETFL) { r = hs(SYS_fcntl, (long) a0, F_GETFL, 0, 0, 0, 0); return r < 0 ? r : fl_h2g(r); }
        if (a1 == F_SETFL) return hs(SYS_fcntl, (long) a0, F_SETFL, fl_g2h((long) a2), 0, 0, 0);
        return hs(SYS_fcntl, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 63: { struct utsname u; r = hs(SYS_uname, (long) &u, 0, 0, 0, 0, 0); if (r == 0) { pemu_uname_fix(&u, "x86_64"); memcpy((void*) a0, &u, sizeof u); } return r; }
    case 12:
        if (a0 >= brk_base && a0 <= brk_end) brk_cur = a0;
        return (long) brk_cur;
    case 131: {                                       /* sigaltstack(const stack_t *ss, stack_t *old): sp@0 flags@8 size@16 */
        int on = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;
        u64 osp = g_ss_sp, osz = g_ss_size; u32 ofl = !g_ss_size ? 2 : on ? 1 : 0;
        if (a0) {
            u64 nsp = *(u64u*) a0, nsz = *(u64u*)(a0 + 16); u32 nfl = *(u32u*)(a0 + 8);
            if (on) return -EPERM;                                    /* cannot change it while running on it */
            if (nfl & ~(u32)(2 | 0x80000000u)) return -EINVAL;        /* SS_DISABLE, SS_AUTODISARM */
            if (nfl & 2) { g_ss_sp = 0; g_ss_size = 0; }
            else { if (nsz < 2048 /*MINSIGSTKSZ*/) return -ENOMEM; g_ss_sp = nsp; g_ss_size = nsz; }
        }
        if (a1) { *(u64u*) a1 = osp; *(u32u*)(a1 + 8) = ofl; *(u32u*)(a1 + 12) = 0; *(u64u*)(a1 + 16) = osz; }
        return 0; }
    case 111: return hs(SYS_getpgid, 0, 0, 0, 0, 0, 0);
    case 158:                                         /* arch_prctl */
        switch (a0) {
        case 0x1002: c->fs = a1; return 0;
        case 0x1001: c->gs = a1; return 0;
        case 0x1003: *(u64*) a1 = c->fs; return 0;
        case 0x1004: *(u64*) a1 = c->gs; return 0;
        case 0x1011: return 1;                        /* ARCH_GET_CPUID: cpuid is always enabled */
        case 0x1012: return a1 ? 0 : -ENODEV;         /* ARCH_SET_CPUID: cpuid faulting cannot be emulated, like a CPU without it */
        default: return -EINVAL;
        }
    case 157:                                         /* prctl: PR_SET_SYSCALL_USER_DISPATCH (Wine) would arm the host, not the guest: refuse like an old kernel */
        if ((int) a0 == 59) return -EINVAL;
        return hs(SYS_prctl, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0);
    case 13: {                                        /* rt_sigaction */
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
            if (s != SIGSYS && sigaction(s, &sa, NULL) < 0) return -errno;     /* SIGSYS stays with hsys() */
            gsa[s] = nw;
        }
        if (a2) *(struct gsa*) a2 = old;
        return 0;
    }
    case 14: {                                        /* rt_sigprocmask: never let the guest block SIGSYS on the host, or a seccomp kill cannot be turned into ENOSYS (see hsys) */
        long set = (long) a1; u64 nm;
        if (a1 && (int) a0 != 1) { nm = *(u64*) a1 & ~(1ULL << (SIGSYS - 1)); set = (long) &nm; }
        return hs(SYS_rt_sigprocmask, (long) a0, set, (long) a2, 8, 0, 0); }
    case 15: g_sigreturn(c); *noset = 1; return 0;
    case 232: case 281: {                             /* epoll_wait / epoll_pwait: guest events are packed (12 bytes) */
        int max = (int) a2; if (max <= 0 || max > 4096) return -EINVAL;
        struct epoll_event*ev = malloc(sizeof *ev * (size_t) max);
        if (!ev) return -ENOMEM;
        r = hs(SYS_epoll_pwait, (long) a0, (long) ev, max, (long) a3, n == 281 ? (long) a4 : 0, 8);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 12 * i; *(u32*) g = ev[i].events; *(u64u*)(g + 4) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 213: return hs(SYS_epoll_create1, 0, 0, 0, 0, 0, 0);
    case 233: {                                       /* epoll_ctl: the x86_64 guest struct is packed (events@0, data@4, 12 bytes); the aarch64 host struct is not (data@8) */
        struct epoll_event ev, *p = NULL;
        if (a3) { memset(&ev, 0, sizeof ev); ev.events = *(u32u*) a3; ev.data.u64 = *(u64u*)(a3 + 4); p = &ev; }
        return hs(SYS_epoll_ctl, (long) a0, (long) a1, (long) a2, (long) p, 0, 0); }
    case 284: return hs(SYS_eventfd2, (long) a0, 0, 0, 0, 0, 0);
    case 56: return clone_common(c, 1, a0 & 0xffffffffULL, a1, a2, a3, a4, 0, noset);                                      /* clone */
    case 57: case 58: return clone_common(c, 0, SIGCHLD, 0, 0, 0, 0, 0, noset);                            /* fork / vfork */
    case 435: return do_clone3(c, (const u8*) a0, a1, noset);
    case 322: return do_execveat((int) a0, (const char*) a1, (char**) a2, (char**) a3, (long) a4);
    case 59: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 9:
        if (a3 & 0x40 /*MAP_32BIT*/ && !a0) return mmap32(a1, a2, a3 & ~0x40UL & ~(u64) MAP_FIXED, a4, a5);
        return hs(SYS_mmap, (long) a0, (long) a1, (long) a2, (long)(a3 & ~0x40UL), (long) a4, (long) a5);
#ifdef SYS_shmctl
    case 31: return hs(SYS_shmctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);                   /* IPC_64 is implied on 64-bit; the generic ABI rejects the flag */
#endif
#ifdef SYS_semctl
    case 66: return hs(SYS_semctl, (long) a0, (long) a1, (long)(a2 & ~0x100UL), (long) a3, 0, 0);
#endif
#ifdef SYS_msgctl
    case 71: return hs(SYS_msgctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);
#endif
    case 132: { struct timespec ts[2], *tp = NULL;                                                          /* utime(path, struct utimbuf*) */
                if (a1) { const s64*u = (const s64*) a1; ts[0].tv_sec = (time_t) u[0]; ts[0].tv_nsec = 0; ts[1].tv_sec = (time_t) u[1]; ts[1].tv_nsec = 0; tp = ts; }
                return pemu_callat(SYS_utimensat, AT_FDCWD, (const char*) a0, 1, (long) tp, 0, 0, 0); }
    case 133: return hs(SYS_mknodat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 261: { struct timespec ts[2], *tp = NULL;                                                          /* futimesat(dirfd, path, struct timeval[2]) */
                if (a2) { struct timeval*tv = (struct timeval*) a2; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return pemu_callat(SYS_utimensat, (long) a0, (const char*) a1, 1, (long) tp, 0, 0, 0); }
    case 253: return hs(SYS_inotify_init1, 0, 0, 0, 0, 0, 0);
    case 282: return hs(SYS_signalfd4, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 334: case 317: return -ENOSYS;                                                                      /* rseq, seccomp: glibc and others fall back */
    case 425: { long r = hs(425 /*io_uring_setup; same number on x86_64 and aarch64*/, (long) a0, (long) a1, 0, 0, 0, 0);
                if (r >= 0) mt_barriers();                                                                  /* the kernel now writes the rings behind our back */
                return r; }
    case 426: return hs(426, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);         /* io_uring_enter */
    case 427: return hs(427, (long) a0, (long) a1, (long) a2, (long) a3, 0, 0);                         /* io_uring_register */
    default:
        nr = pass_nr(n);
        if (nr >= 0) return hs(nr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        if (g_trace) fprintf(stderr, "pemu: unsupported syscall %llu\n", (unsigned long long) n);
        return -ENOSYS;
    }
}
static void do_syscall(CPU*c) {
    int noset = 0;
    u64 n = c->r[RAX];
    u64 ta0 = c->r[RDI], ta1 = c->r[RSI], ta2 = c->r[RDX];
    if (g_trace) fprintf(stderr, "pemu: syscall %llu (%lld, %llx, %llx) ...\n", (unsigned long long) n, (long long) ta0, (unsigned long long) ta1, (unsigned long long) ta2);
    if (g_trace) {   /* show the path argument of path-taking syscalls */
        u64 pp = 0;
        switch (n) { case 2: case 4: case 6: case 21: case 59: case 80: case 83: case 84: case 87: case 89: case 90: case 92: pp = ta0; break;
                     case 257: case 258: case 262: case 263: case 267: case 268: case 269: case 322: pp = ta1; break; default: break; }
        if (pp > 0x10000 && pp < 0x7fffffffffffULL) fprintf(stderr, "pemu:   path=\"%.200s\"\n", (const char*) (uintptr_t) pp);
    }
    long r = gsys(c, &noset);
    if (g_trace) fprintf(stderr, "pemu: syscall %llu -> %ld\n", (unsigned long long) n, r);
    if (g_trace && n == 7 && r > 0 && ta0 > 0x10000) { const short*pf = (const short*) (uintptr_t) ta0; fprintf(stderr, "pemu:   poll fd0=%d events=%#x revents=%#x nfds=%llu\n", *(const int*) pf, pf[2], pf[3], (unsigned long long) ta1); }
    if (g_trace && n == 53 && r == 0) { const int*sv = (const int*) (uintptr_t) c->r[10]; if (sv) fprintf(stderr, "pemu:   socketpair -> fds %d %d\n", sv[0], sv[1]); }
    if (!noset) c->r[RAX] = (u64) r;
#ifdef HAVE_JIT
    if (g_jit_on) jit_post_syscall(c, n, r);
#endif
}

