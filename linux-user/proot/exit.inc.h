/* linux-user/proot/exit.inc.h - exit handling: wait for the guest process tree. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- exit: proot waits until all the processes of the guest are gone and returns the status of the first one;
 * --kill-on-exit kills the others at once instead.  Orphans re-parent to the first process (child subreaper). */
static void pemu_kill_children(pid_t pid, int depth) {
    char d[64]; snprintf(d, sizeof d, "/proc/%d/task", (int) pid);
    DIR *dp = opendir(d); if (!dp) return;
    struct dirent *e;
    while ((e = readdir(dp))) {
        if (e->d_name[0] == '.') continue;
        char f[320]; snprintf(f, sizeof f, "/proc/%d/task/%s/children", (int) pid, e->d_name);
        FILE *fp = fopen(f, "r"); if (!fp) continue;
        int c; while (fscanf(fp, "%d", &c) == 1) { if (depth < 16) pemu_kill_children(c, depth + 1); kill(c, SIGKILL); }
        fclose(fp);
    }
    closedir(dp);
}
static void pemu_ph_cleanup(void) {
    if (pemu_ph_owner != getpid()) return;               /* only the process that owns them */
    for (int i = pemu_nph - 1; i >= 0; i--) {
        struct stat st;
        if (lstat(pemu_ph[i], &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) (void) rmdir(pemu_ph[i]);   /* rmdir only removes empty directories */
        else if (st.st_size == 0) (void) unlink(pemu_ph[i]);
    }
    pemu_nph = 0;
}
/* called just before the first guest process leaves with exit_group */
static void pemu_exit_hook(void) {
    if (pemu_wait && getpid() == pemu_top) {
        if (pemu_killall) pemu_kill_children(getpid(), 0);
        int st; pid_t w;
        do { w = waitpid(-1, &st, 0); } while (w > 0 || (w < 0 && errno == EINTR));
    }
    pemu_ph_cleanup();
}
