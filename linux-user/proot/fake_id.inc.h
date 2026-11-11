/* linux-user/proot/fake_id.inc.h - fake identity (-0 / -i): fake_id0 state machine. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- fake identity (-0 / -i UID:GID): the state machine of proot's fake_id0 extension.
 * set*id / get*id work on pemu_uid[] / pemu_gid[] (real, effective, saved, fs); the rules of who may change what are
 * those of the kernel for an unprivileged process, and "root" (effective uid 0) may change anything. */
enum { PEMU_SETUID = 1, PEMU_SETGID, PEMU_SETREUID, PEMU_SETREGID, PEMU_SETRESUID, PEMU_SETRESGID, PEMU_SETFSUID, PEMU_SETFSGID,
       PEMU_GETUID, PEMU_GETGID, PEMU_GETEUID, PEMU_GETEGID, PEMU_GETRESUID, PEMU_GETRESGID };
static long pemu_idcall(int op, uint64_t a0, uint64_t a1, uint64_t a2) {
    unsigned *id; unsigned eid = pemu_uid[PEMU_E];            /* the effective *user* id decides about privilege */
    switch (op) {
    case PEMU_SETUID: case PEMU_SETREUID: case PEMU_SETRESUID: case PEMU_SETFSUID: case PEMU_GETUID: case PEMU_GETEUID: case PEMU_GETRESUID: id = pemu_uid; break;
    default: id = pemu_gid; break;
    }
#define UNSET(x)     ((unsigned) (x) == (unsigned) -1)
#define ANY(x)       ((x) == id[PEMU_R] || (x) == id[PEMU_E] || (x) == id[PEMU_S])
    switch (op) {
    case PEMU_SETUID: case PEMU_SETGID: {
        unsigned v = (unsigned) a0;
        if (UNSET(v)) return -EINVAL;                            /* the kernel: (uid_t) -1 is not an id */
        if (!(eid == 0 || v == id[PEMU_R] || v == id[PEMU_S])) return -EPERM;    /* unprivileged: the real or the saved id, not the effective one */
        if (eid == 0) { id[PEMU_R] = v; id[PEMU_S] = v; }        /* privilege (CAP_SETUID and CAP_SETGID alike) follows the effective USER id, also for setgid */
        id[PEMU_E] = v; id[PEMU_F] = v; return 0; }
    case PEMU_SETREUID: case PEMU_SETREGID: {
        unsigned r = (unsigned) a0, e = (unsigned) a1, oldr = id[PEMU_R];
        /* the kernel: the new real id must be the old real or effective one, the new effective id any of the three */
        int allowed = eid == 0 || ((UNSET(r) || r == id[PEMU_R] || r == id[PEMU_E]) && (UNSET(e) || ANY(e)));
        if (!allowed) return -EPERM;
        if (!UNSET(r)) id[PEMU_R] = r;
        if (!UNSET(e)) id[PEMU_E] = e;
        if (!UNSET(r) || (!UNSET(e) && e != oldr)) id[PEMU_S] = id[PEMU_E];      /* saved follows the effective id when the real id was set, or the effective one left it */
        id[PEMU_F] = id[PEMU_E];
        return 0; }
    case PEMU_SETRESUID: case PEMU_SETRESGID: {
        unsigned r = (unsigned) a0, e = (unsigned) a1, s = (unsigned) a2;
        int allowed = eid == 0 || ((UNSET(r) || ANY(r)) && (UNSET(e) || ANY(e)) && (UNSET(s) || ANY(s)));
        if (!allowed) return -EPERM;
        if (!UNSET(r)) id[PEMU_R] = r;
        if (!UNSET(e)) id[PEMU_E] = e;
        if (!UNSET(s)) id[PEMU_S] = s;
        id[PEMU_F] = id[PEMU_E];                                 /* the kernel always resets the fs id to the effective one */
        return 0; }
    case PEMU_SETFSUID: case PEMU_SETFSGID: {
        unsigned v = (unsigned) a0, old = id[PEMU_F];
        if (!UNSET(v) && (eid == 0 || v == id[PEMU_F] || ANY(v))) id[PEMU_F] = v;
        return (long) old; }                                     /* the previous value, also on failure */
    case PEMU_GETUID: case PEMU_GETGID: return (long) id[PEMU_R];
    case PEMU_GETEUID: case PEMU_GETEGID: return (long) id[PEMU_E];
    case PEMU_GETRESUID: case PEMU_GETRESGID: {
        uint64_t pp[3] = { a0, a1, a2 }; uint32_t val[3] = { id[0], id[1], id[2] };
        for (int i = 0; i < 3; i++) if (pemu_guest_copy(1, (void*) pp[i], &val[i], 4) < 0) return -EFAULT;     /* a bad pointer is EFAULT, not a crash of the emulator */
        return 0; }
    }
#undef UNSET
#undef ANY
    return -ENOSYS;
}
/* execve of a set-uid / set-gid file: effective and saved id become the file's (fake) owner, 0 when none is recorded; undone when the exec fails */
struct pemu_idsave { unsigned u[4], g[4]; };
static void pemu_exec_ids(const char *host, struct pemu_idsave *sv) {
    memcpy(sv->u, pemu_uid, sizeof sv->u); memcpy(sv->g, pemu_gid, sizeof sv->g);
    struct stat st;
    if (!pemu_fakeroot || stat(host, &st) < 0) return;
    /* the new id is the file's owner: the recorded fake owner (see fake_owner.inc.h) when there is one, otherwise 0 as before */
    uint32_t ou = 0, og = 0, ru, rg;
    if (pemu_own_on() && pemu_own_get(host, 1, -1, &ru, &rg) == 0) { ou = ru; og = rg; }
    if (st.st_mode & S_ISUID) pemu_uid[PEMU_E] = pemu_uid[PEMU_S] = ou;
    if (st.st_mode & S_ISGID) pemu_gid[PEMU_E] = pemu_gid[PEMU_S] = og;
}
static void pemu_exec_ids_undo(const struct pemu_idsave *sv) { memcpy(pemu_uid, sv->u, sizeof pemu_uid); memcpy(pemu_gid, sv->g, sizeof pemu_gid); }
