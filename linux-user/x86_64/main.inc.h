/* linux-user/x86_64/main.inc.h - command line, process setup, main. SPDX-License-Identifier: Apache-2.0 */
int main(int argc, char**argv) {
    const char*argv0 = NULL; int a = 1, pr, nojit = 0;
    { int rc = pemu_subcmd(&argc, &argv); if (rc >= 0) return rc; }       /* login / install / uninstall / list */
#ifdef HAVE_JIT
    { const char*ev = getenv("PEMU_JIT"); if (ev && !strcmp(ev, "0")) nojit = 1; }
#else
    (void) nojit;
#endif
    for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++) {
        const char*o = argv[a];
        if (!strcmp(o, "--")) { a++; break; }
        if (!strcmp(o, "-0") && a + 1 < argc) argv0 = argv[++a];
        else if (!strcmp(o, "-strace")) g_trace = 1;
        else if (!strcmp(o, "-nojit")) nojit = 1;
        else if (!strcmp(o, "--version")) { printf("pemu %s (x86_64 user-mode emulator with AArch64 JIT and built-in proot)\n", PEMU_RELEASE); return 0; }
        else if ((pr = pemu_opt(argc, argv, &a)) != 0) { if (pr < 0) return 2; }
        else { fprintf(stderr, "pemu: unknown option %s\n", o); return 2; }
    }
    if (a >= argc) { fprintf(stderr, "usage: pemu login NAME|DIR [options] [command [args...]]\n       pemu [-strace] [-nojit] program [args...]\n       (pemu --help)\n"); return 2; }
    if (pemu_finish() < 0) return 1;
    if (getenv("PEMU_STRACE")) g_trace = 1;
    ssize_t sl = readlink("/proc/self/exe", g_self, sizeof g_self - 1);
    if (sl > 0) g_self[sl] = 0; else strcpy(g_self, argv[0]);
    const char*prog = argv[a]; char**pav = argv + a; int pac = argc - a;
    char hostprog[PEMU_HP], gprog[PATH_MAX]; const char*loadp = prog;
    char xp[64];
    if (pemu_execfd >= 0) {                              /* --exec-fd: the file was handed over open (execveat of a memfd or deleted file) */
        snprintf(xp, sizeof xp, "/proc/self/fd/%d", pemu_execfd); loadp = xp;
        if (pemu_on) snprintf(pemu_exe, sizeof pemu_exe, "%s", prog);
    } else if (pemu_on) {
        char**nv; long e = pemu_exec_resolve_cmd(prog, pav, &nv, hostprog, gprog);
        if (e < 0) { fprintf(stderr, "pemu: %s: %s\n", prog, strerror((int) -e)); return 127; }
        pav = nv; pac = 0; while (pav[pac]) pac++;
        loadp = hostprog; prog = gprog; snprintf(pemu_exe, sizeof pemu_exe, "%s", gprog);
    }
    char*sete = getenv("PEMU_SET_ENV"); if (sete) { sete = strdup(sete); char*sp1; for (char*t = strtok_r(sete, ",", &sp1); t; t = strtok_r(NULL, ",", &sp1)) { char*eq = strchr(t, '='); if (eq) { *eq = 0; setenv(t, eq + 1, 1); } } }
    ElfInfo prg = {0}, itp = {0};
    { int lr = load_elf(loadp, &prg); if (pemu_execfd >= 0) { close(pemu_execfd); pemu_execfd = -1; } if (lr < 0) return 127; }
    u64 entry = prg.entry;
    if (prg.has_interp) {
        const char*ip = prg.interp; char ih[PEMU_HP];
        if (pemu_on) { long e = pemu_path(AT_FDCWD, ip, 1, ih, sizeof ih, NULL); if (e < 0) { fprintf(stderr, "pemu: %s: %s\n", ip, strerror((int) -e)); return 127; } if (e == 0) ip = ih; }
        if (load_elf(ip, &itp) < 0) return 127;
    }
    if (prg.has_interp) entry = itp.entry;
    brk_end = (u64) mmap(NULL, 1ULL << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    brk_base = brk_cur = brk_end; brk_end += 1ULL << 30;
    g_tramp = (u64) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy((void*) g_tramp, "\xb8\x0f\x00\x00\x00\x0f\x05", 7);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = hsegv; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    { struct sigaction ss; memset(&ss, 0, sizeof ss); ss.sa_sigaction = hsys; ss.sa_flags = SA_SIGINFO; sigemptyset(&ss.sa_mask); sigaction(SIGSYS, &ss, NULL); }
    size_t ssz = 8UL << 20;
    u64 top = (u64) mmap(NULL, ssz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0) + ssz;
    u64 sp = top;
    u64 execfn = push_str(&sp, prog);
    int ac = pac, ec = 0; while (environ[ec]) ec++;
    u64*as = calloc((size_t) ac + 1, 8), *es = calloc((size_t) ec + 1, 8);
    for (int k = ec - 1; k >= 0; k--) es[k] = push_str(&sp, environ[k]);
    for (int k = ac - 1; k >= 0; k--) as[k] = push_str(&sp, k == 0 && argv0 ? argv0 : pav[k]);
    u64 plat = push_str(&sp, "x86_64");
    sp &= ~15ULL; sp -= 16; u64 rnd = sp; if (syscall(SYS_getrandom, (void*) rnd, 16, 0) < 0) memset((void*) rnd, 0x5a, 16);
    u64 aux[][2] = {
        {AT_PHDR, prg.phdr}, {AT_PHENT, sizeof(Elf64_Phdr)}, {AT_PHNUM, prg.phnum}, {AT_PAGESZ, 4096},
        {AT_BASE, itp.base}, {AT_FLAGS, 0}, {AT_ENTRY, prg.entry}, {AT_UID, pemu_at_uid()}, {AT_EUID, pemu_at_euid()},
        {AT_GID, pemu_at_gid()}, {AT_EGID, pemu_at_egid()}, {AT_HWCAP, 0x078bfbff}, {AT_CLKTCK, 100}, {AT_SECURE, 0},
        {AT_RANDOM, rnd}, {AT_PLATFORM, plat}, {AT_EXECFN, execfn}, {AT_HWCAP2, 0}, {AT_NULL, 0}};
    int na = (int)(sizeof aux / sizeof aux[0]);
    u64 words = 1 + (u64) ac + 1 + (u64) ec + 1 + 2 * (u64) na;
    sp -= words * 8; sp &= ~15ULL;
    u64*w = (u64*) sp; int k = 0;
    w[k++] = (u64) ac;
    for (int j = 0; j < ac; j++) w[k++] = as[j];
    w[k++] = 0;
    for (int j = 0; j < ec; j++) w[k++] = es[j];
    w[k++] = 0;
    for (int j = 0; j < na; j++) { w[k++] = aux[j][0]; w[k++] = aux[j][1]; }
    cpu.r[RSP] = sp; cpu.rip = entry; cpu.rf = 0x202; cpu.mxcsr = 0x1f80; cpu.fcw = 0x37f; cpu.fsw = 0; cpu.top = 0;
#ifdef HAVE_JIT
    if (!nojit && jit_init() == 0) g_jit_on = 1;
#endif
    run(&cpu);
    return 0;
}
