/* linux-user/proot/fake_proc.inc.h - fake /proc files for hosts that hide them. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- fake /proc files: Android hides some of them (/proc/stat, loadavg, vmstat, ...). They are only
 * served when the real file cannot be opened (or always, with PEMU_FAKE_PROC=1). */
#include <sys/times.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
static int pemu_fakeproc(const char *g, char *b, size_t sz) {
    struct timespec ts; double up = 0;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) up = (double) ts.tv_sec + ts.tv_nsec / 1e9;
    long ncpu = sysconf(_SC_NPROCESSORS_CONF); if (ncpu < 1) ncpu = 1;
    if (!strcmp(g, "/proc/uptime")) return snprintf(b, sz, "%.2f %.2f\n", up, up * (double) ncpu * 0.9);
    if (!strcmp(g, "/proc/loadavg")) { double l[3] = { 0, 0, 0 }; struct sysinfo si; if (sysinfo(&si) == 0) for (int i = 0; i < 3; i++) l[i] = (double) si.loads[i] / 65536.0; /* getloadavg() is bionic API 29+ only */ return snprintf(b, sz, "%.2f %.2f %.2f 1/1 %d\n", l[0], l[1], l[2], (int) getpid()); }
    if (!strcmp(g, "/proc/version")) { struct utsname u; if (uname(&u)) strcpy(u.release, "6.1.0"); return snprintf(b, sz, "Linux version %s (pemu@localhost) (gcc) #1 SMP PREEMPT\n", u.release); }
    if (!strcmp(g, "/proc/sys/kernel/cap_last_cap")) return snprintf(b, sz, "40\n");
    if (!strcmp(g, "/proc/vmstat")) return snprintf(b, sz, "nr_free_pages 0\nnr_inactive_anon 0\nnr_active_anon 0\nnr_inactive_file 0\nnr_active_file 0\npgpgin 0\npgpgout 0\npswpin 0\npswpout 0\npgfault 0\npgmajfault 0\n");
    if (!strcmp(g, "/proc/stat")) {
        struct tms t; clock_t k = times(&t); (void) k;
        long hz = sysconf(_SC_CLK_TCK); if (hz < 1) hz = 100;
        unsigned long u = (unsigned long) t.tms_utime + (unsigned long) t.tms_cutime, sy = (unsigned long) t.tms_stime + (unsigned long) t.tms_cstime;
        unsigned long tot = (unsigned long) (up * (double) hz), idle = tot > u + sy ? tot - u - sy : 0;
        size_t o = (size_t) snprintf(b, sz, "cpu  %lu 0 %lu %lu 0 0 0 0 0 0\n", u, sy, idle * (unsigned long) ncpu);
        for (long i = 0; i < ncpu && o < sz; i++) o += (size_t) snprintf(b + o, sz - o, "cpu%ld %lu 0 %lu %lu 0 0 0 0 0 0\n", i, u / (unsigned long) ncpu, sy / (unsigned long) ncpu, idle);
        if (o < sz) o += (size_t) snprintf(b + o, sz - o, "intr 0\nctxt 0\nbtime %ld\nprocesses 1\nprocs_running 1\nprocs_blocked 0\n", (long) time(NULL) - (long) up);
        return (int) (o < sz ? o : sz - 1);
    }
    return -1;
}
/* memfd with the fake content; LONG_MIN = `g` is not a fake file */
static long pemu_fakeproc_open(const char *g, long hf) {
    if (strncmp(g, "/proc/", 6)) return LONG_MIN;
    char b[4096]; int n = pemu_fakeproc(g, b, sizeof b);
    if (n < 0) return LONG_MIN;
    if (hf & (O_WRONLY | O_RDWR)) return -EACCES;
    long fd = hs(SYS_memfd_create, (long) "pemu-proc", (hf & O_CLOEXEC) ? 1 : 0, 0, 0, 0, 0);   /* 1 = MFD_CLOEXEC */
    if (fd < 0) return fd;
    if (write((int) fd, b, (size_t) n) != n || lseek((int) fd, 0, SEEK_SET) < 0) { close((int) fd); return -EIO; }
    return fd;
}
