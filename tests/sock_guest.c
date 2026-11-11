/* tests/sock_guest.c - x86_64 guest for tests/sock.sh.
 *   sock_guest connect PATH   connect(2) a stream socket to PATH and report
 *   sock_guest names          two datagram sockets bound to /sock-a and /sock-b: every way of learning a peer's name must say /sock-b */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
static socklen_t mk(struct sockaddr_un *a, const char *p) { memset(a, 0, sizeof *a); a->sun_family = AF_UNIX; strncpy(a->sun_path, p, sizeof a->sun_path - 1); return sizeof *a; }
static void name(const char *how, const char *p) { printf("%-12s: %s\n", how, p); }
int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "connect")) {
        int s = socket(AF_UNIX, SOCK_STREAM, 0); struct sockaddr_un a; socklen_t l = mk(&a, argv[2]);
        int r = connect(s, (struct sockaddr *) &a, l);
        printf("connect(%s): %s\n", argv[2], r == 0 ? "CONNECTED" : strerror(errno));
        return 0;
    }
    int a = socket(AF_UNIX, SOCK_DGRAM, 0), b = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un sa, sb, from; socklen_t la = mk(&sa, "/sock-a"), lb = mk(&sb, "/sock-b"), fl;
    unlink("/sock-a"); unlink("/sock-b");
    if (bind(a, (struct sockaddr *) &sa, la) || bind(b, (struct sockaddr *) &sb, lb)) { printf("bind: %s\n", strerror(errno)); return 1; }
    fl = sizeof from; memset(&from, 0, sizeof from); getsockname(b, (struct sockaddr *) &from, &fl); name("getsockname", from.sun_path);
    char buf[8], cb[8]; struct iovec iv = { buf, sizeof buf };
    for (int k = 0; k < 3; k++) {
        if (sendto(b, "x", 1, 0, (struct sockaddr *) &sa, la) != 1) { printf("sendto: %s\n", strerror(errno)); return 1; }
        fl = sizeof from; memset(&from, 0, sizeof from);
        if (k == 0) { recvfrom(a, buf, sizeof buf, 0, (struct sockaddr *) &from, &fl); name("recvfrom", from.sun_path); }
        if (k == 1) { struct msghdr m; memset(&m, 0, sizeof m); m.msg_name = &from; m.msg_namelen = sizeof from; m.msg_iov = &iv; m.msg_iovlen = 1; recvmsg(a, &m, 0); name("recvmsg", from.sun_path); }
        if (k == 2) { struct mmsghdr mm; memset(&mm, 0, sizeof mm); mm.msg_hdr.msg_name = &from; mm.msg_hdr.msg_namelen = sizeof from; mm.msg_hdr.msg_iov = &iv; mm.msg_hdr.msg_iovlen = 1;
                      long n = recvmmsg(a, &mm, 1, 0, NULL); if (n != 1) { printf("recvmmsg: %s\n", strerror(errno)); return 1; } name("recvmmsg", from.sun_path); }
    }
    (void) cb; unlink("/sock-a"); unlink("/sock-b");
    return 0;
}
