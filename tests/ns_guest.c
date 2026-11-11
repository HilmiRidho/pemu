/* tests/ns_guest.c - x86_64 guest for tests/ns.sh: namespace calls and name_to_handle_at, which pemu used to pass through or ignore.
 * usage: guest PATH   (PATH exists only inside the rootfs of the test) */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
int main(int argc, char **argv) {
    long r = syscall(SYS_unshare, CLONE_NEWUTS);
    printf("unshare(NEWUTS): %s\n", r == 0 ? "ok" : strerror(errno));
    long pid = syscall(SYS_clone, CLONE_NEWUSER | SIGCHLD, 0, 0, 0, 0);
    if (pid == 0) _exit(0);
    if (pid < 0) printf("clone(NEWUSER): %s\n", strerror(errno)); else { waitpid((pid_t) pid, NULL, 0); printf("clone(NEWUSER): ok\n"); }
    struct { unsigned hb; int ht; unsigned char f[128]; } h; memset(&h, 0, sizeof h); h.hb = 128; int mid = 0;
    r = argc > 1 ? syscall(SYS_name_to_handle_at, AT_FDCWD, argv[1], &h, &mid, 0) : -1;
    /* EOVERFLOW / EOPNOTSUPP: the path was found, the file system just cannot (fully) give a handle */
    printf("name_to_handle_at: %s\n", (r == 0 || errno == EOVERFLOW || errno == EOPNOTSUPP) ? "found" : strerror(errno));
    errno = 0; r = syscall(SYS_setns, -1, 0);
    printf("setns: %s\n", r == 0 ? "ok" : strerror(errno));
    return 0;
}
