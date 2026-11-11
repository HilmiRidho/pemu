/* tests/uring_guest.c - x86_64 guest for tests/uring.sh: opens the same absolute path with open(2) and with an io_uring OPENAT.
 * usage: guest [PATH].  Natively both read the same file.  Under pemu with a rootfs the first must read the rootfs's copy; the second must not reach the host's
 * (io_uring_setup has to fail, ENOSYS), because the kernel would resolve the path itself, outside the rootfs. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>
static void show(const char *how, int fd) {
    char b[64] = {0};
    if (fd < 0) { printf("%-9s: error %d\n", how, fd); return; }
    ssize_t n = read(fd, b, sizeof b - 1); if (n < 0) n = 0; b[n] = 0;
    for (char *q = b; *q; q++) if (*q == '\n') *q = 0;
    printf("%-9s: opened '%s'\n", how, b); close(fd);
}
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/pemu-io/marker";
    show("open()", open(path, O_RDONLY));
    struct io_uring_params p; memset(&p, 0, sizeof p);
    int fd = (int) syscall(425, 4, &p);
    if (fd < 0) { printf("io_uring_setup: errno %d (%s)\n", errno, strerror(errno)); return 2; }
    size_t sqsz = p.sq_off.array + p.sq_entries * sizeof(unsigned), cqsz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    if (p.features & IORING_FEAT_SINGLE_MMAP) { if (cqsz > sqsz) sqsz = cqsz; cqsz = sqsz; }
    char *sq = mmap(0, sqsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    char *cq = (p.features & IORING_FEAT_SINGLE_MMAP) ? sq : mmap(0, cqsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    struct io_uring_sqe *sqes = mmap(0, p.sq_entries * sizeof *sqes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sq == MAP_FAILED || cq == MAP_FAILED || sqes == MAP_FAILED) { printf("mmap of the rings failed\n"); return 3; }
    unsigned *tail = (unsigned *) (sq + p.sq_off.tail), *mask = (unsigned *) (sq + p.sq_off.ring_mask), *arr = (unsigned *) (sq + p.sq_off.array);
    unsigned *chead = (unsigned *) (cq + p.cq_off.head), *ctail = (unsigned *) (cq + p.cq_off.tail), *cmask = (unsigned *) (cq + p.cq_off.ring_mask);
    struct io_uring_cqe *cqes = (struct io_uring_cqe *) (cq + p.cq_off.cqes);
    unsigned t = *tail, idx = t & *mask;
    memset(&sqes[idx], 0, sizeof sqes[idx]);
    sqes[idx].opcode = IORING_OP_OPENAT; sqes[idx].fd = AT_FDCWD; sqes[idx].addr = (uint64_t) (uintptr_t) path; sqes[idx].open_flags = O_RDONLY;
    arr[idx] = idx; __atomic_store_n(tail, t + 1, __ATOMIC_RELEASE);
    long r = syscall(426, fd, 1, 1, IORING_ENTER_GETEVENTS, 0, 0);
    if (r < 0) { printf("io_uring_enter: errno %d\n", errno); return 4; }
    unsigned h = *chead; if (h == __atomic_load_n(ctail, __ATOMIC_ACQUIRE)) { printf("no completion\n"); return 5; }
    int res = cqes[h & *cmask].res; __atomic_store_n(chead, h + 1, __ATOMIC_RELEASE);
    show("io_uring", res);
    return 0;
}
