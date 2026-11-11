/* linux-user/proot/fake_owner.inc.h - fake file owners: chown remembered in an xattr. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- fake file owners.  A fake root may chown anything, but the host refuses (the file stays owned by the host user, and
 * pemu used to answer "success" and forget).  Now the owner the guest asked for is kept in the file's own xattr
 * "user.pemu.own" = "UID:GID" (decimal text, so a rootfs can move between hosts and architectures), and stat shows it.
 *
 * Deliberately small and best effort:
 *   - only uid/gid; modes, device nodes and capabilities are not recorded;
 *   - nothing is ever enforced: pemu only reports what the guest set, it does not check permissions with it;
 *   - any failure (no xattr support on the file system, a read-only file, a symlink, a device) leaves the old behaviour:
 *     the chown still "succeeds" and stat shows the usual mapping of the host user's files;
 *   - active only for a fake root inside a rootfs; PEMU_FAKE_OWNER=0 switches it off;
 *   - the xattr belongs to the inode, so rename, hard links and unlink need no extra work, and every pemu process of the
 *     guest sees the same data; only the read-modify-write of a partial chown (-1 for one id) takes a lock, between threads.
 * Known limit: files the guest creates as a non-root fake user are still shown with the usual mapping. */
#include <sys/xattr.h>
#define PEMU_OWN_XA "user.pemu.own"
struct pemu_own_ov { int valid; uint32_t uid, gid; };      /* owner found by a stat: valid = 0 means "use the normal mapping" */
static int pemu_own_off = -1;                              /* -1: not decided yet (the environment is read once) */
static int pemu_own_on(void) {
    if (!pemu_fakeroot || !pemu_on) return 0;
    if (pemu_own_off < 0) { const char *e = getenv("PEMU_FAKE_OWNER"); pemu_own_off = (e && *e == '0'); }
    return !pemu_own_off;
}
/* "UID:GID", digits only; anything else is ignored as if there were no record */
static int pemu_own_parse(const char *b, ssize_t n, uint32_t *u, uint32_t *g) {
    char t[40], *e1, *e2; unsigned long a, c;
    if (n <= 2 || n >= (ssize_t) sizeof t) return -1;
    memcpy(t, b, (size_t) n); t[n] = 0;
    if (t[0] < '0' || t[0] > '9') return -1;
    errno = 0; a = strtoul(t, &e1, 10);
    if (errno || *e1 != ':' || e1[1] < '0' || e1[1] > '9') return -1;
    c = strtoul(e1 + 1, &e2, 10);
    if (errno || *e2 || a >= 0xffffffffUL || c >= 0xffffffffUL) return -1;
    *u = (uint32_t) a; *g = (uint32_t) c; return 0;
}
/* the recorded owner of a file: by descriptor (fd >= 0) or by host path.  0 = found */
static int pemu_own_get(const char *host, int follow, int fd, uint32_t *u, uint32_t *g) {
    char b[40]; ssize_t n;
    if (fd >= 0) n = fgetxattr(fd, PEMU_OWN_XA, b, sizeof b);
    else if (!host) return -1;
    else n = follow ? getxattr(host, PEMU_OWN_XA, b, sizeof b) : lgetxattr(host, PEMU_OWN_XA, b, sizeof b);
    return n < 0 ? -1 : pemu_own_parse(b, n, u, g);
}
static int pemu_own_set(const char *host, int follow, int fd, uint32_t u, uint32_t g) {
    char b[40]; int n = snprintf(b, sizeof b, "%u:%u", (unsigned) u, (unsigned) g);
    if (fd >= 0) return fsetxattr(fd, PEMU_OWN_XA, b, (size_t) n, 0);
    return follow ? setxattr(host, PEMU_OWN_XA, b, (size_t) n, 0) : lsetxattr(host, PEMU_OWN_XA, b, (size_t) n, 0);
}
/* Called after a chown that a fake root was allowed to do (uid / gid as the guest gave them, -1 = leave alone).
 * `host` must still be a usable path (or fd >= 0).  Never fails: a file that cannot take the xattr keeps the old behaviour. */
static void pemu_own_chown(const char *host, int follow, int fd, unsigned long uid, unsigned long gid) {
    uint32_t nu = (uint32_t) uid, ng = (uint32_t) gid, u, g; struct stat st; int rc;
    if (!pemu_own_on() || !PEMU_PRIV() || (nu == (uint32_t) -1 && ng == (uint32_t) -1)) return;
    rc = fd >= 0 ? fstat(fd, &st) : fstatat(AT_FDCWD, host, &st, follow ? 0 : AT_SYMLINK_NOFOLLOW);
    if (rc < 0) return;
    /* chown(uid, -1) and chown(-1, gid) read the record, change one half and write it back: two threads doing that at once
     * would lose one half.  The lock covers that read-modify-write (a plain read, as stat does, is one atomic getxattr and
     * needs none).  Between separate pemu processes (a fork) only the inode's xattr is shared, so there it stays best effort. */
    while (__sync_lock_test_and_set(&pemu_ownlk, 1)) sched_yield();
    if (pemu_own_get(host, follow, fd, &u, &g) != 0) { u = PEMU_UID(st.st_uid); g = PEMU_GID(st.st_gid); }   /* what the guest saw until now */
    if (nu != (uint32_t) -1) u = nu;
    if (ng != (uint32_t) -1) g = ng;
    (void) pemu_own_set(host, follow, fd, u, g);
    __sync_lock_release(&pemu_ownlk);
}
/* stat by descriptor: fill *ov when the file has a record */
static void pemu_own_fstat(int fd, struct pemu_own_ov *ov) {
    ov->valid = pemu_own_on() && pemu_own_get(NULL, 0, fd, &ov->uid, &ov->gid) == 0;
}
