/* tests/owner_guest.c - x86_64 guest for tests/owner.sh: chown as a fake root, then look at the owner through stat, fstat and statx.
 * usage: guest write   (creates /f /d /l and chowns them)     guest show   (only prints what stat says) */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
static void show(const char *tag, const char *p, int nofollow) {
    struct stat st; int r = nofollow ? lstat(p, &st) : stat(p, &st);
    if (r < 0) { printf("%s: stat: %s\n", tag, strerror(errno)); return; }
    printf("%s: %u:%u", tag, (unsigned) st.st_uid, (unsigned) st.st_gid);
    struct statx sx; if (syscall(SYS_statx, AT_FDCWD, p, nofollow ? AT_SYMLINK_NOFOLLOW : 0, STATX_UID | STATX_GID, &sx) == 0) printf(" statx %u:%u", sx.stx_uid, sx.stx_gid);
    printf("\n");
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "show";
    if (!strcmp(mode, "write")) {
        int fd = open("/f", O_CREAT | O_RDWR, 0644); if (fd < 0) { printf("open: %s\n", strerror(errno)); return 1; }
        mkdir("/d", 0755); symlink("/f", "/l");
        printf("chown 1000:100: %d\n", chown("/f", 1000, 100));
        show("after chown", "/f", 0);
        printf("chown -1:50: %d\n", chown("/f", (uid_t) -1, 50));
        show("keeps uid", "/f", 0);
        printf("fchown 7:8: %d\n", fchown(fd, 7, 8));
        struct stat st; fstat(fd, &st); printf("fstat: %u:%u\n", (unsigned) st.st_uid, (unsigned) st.st_gid);
        printf("chown dir 33:33: %d\n", chown("/d", 33, 33));
        show("dir", "/d", 0);
        printf("lchown link 5:5: %d\n", lchown("/l", 5, 5));      /* a symlink cannot hold the xattr: must still "succeed" */
        show("link (target)", "/l", 0);
        printf("chown 0:0: %d\n", chown("/f", 0, 0));
        show("back to root", "/f", 0);
        printf("chown 1000:100 again: %d\n", chown("/f", 1000, 100));
        rename("/f", "/g"); show("renamed", "/g", 0);
        close(fd);
    } else show("f", "/g", 0), show("d", "/d", 0);
    return 0;
}
