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
        if (!(eid == 0 || ANY(v))) return -EPERM;
        if (id[PEMU_E] == 0) { id[PEMU_R] = v; id[PEMU_S] = v; }
        id[PEMU_E] = v; id[PEMU_F] = v; return 0; }
    case PEMU_SETREUID: case PEMU_SETREGID: {
        unsigned r = (unsigned) a0, e = (unsigned) a1;
#define UNCH(x, cur) (UNSET(x) || (x) == (cur))
        int allowed = eid == 0 || (UNCH(e, id[PEMU_E]) && UNCH(r, id[PEMU_R]))
            || (r == id[PEMU_E] && (e == id[PEMU_R] || UNCH(e, id[PEMU_E])))
            || (e == id[PEMU_R] && (r == id[PEMU_E] || UNCH(r, id[PEMU_R])))
            || (e == id[PEMU_S] && UNCH(r, id[PEMU_R]));
#undef UNCH
        if (!allowed) return -EPERM;
        if (!UNSET(e)) { if (e != id[PEMU_R]) id[PEMU_S] = e; id[PEMU_E] = e; id[PEMU_F] = e; }
        if (!UNSET(r)) { if (!UNSET(e)) id[PEMU_S] = e; id[PEMU_R] = r; }
        return 0; }
    case PEMU_SETRESUID: case PEMU_SETRESGID: {
        unsigned r = (unsigned) a0, e = (unsigned) a1, s = (unsigned) a2;
        int allowed = eid == 0 || ((UNSET(r) || ANY(r)) && (UNSET(e) || ANY(e)) && (UNSET(s) || ANY(s)));
        if (!allowed) return -EPERM;
        if (!UNSET(r)) id[PEMU_R] = r;
        if (!UNSET(e)) { id[PEMU_E] = e; id[PEMU_F] = e; }
        if (!UNSET(s)) id[PEMU_S] = s;
        return 0; }
    case PEMU_SETFSUID: case PEMU_SETFSGID: {
        unsigned v = (unsigned) a0, old = id[PEMU_F];
        if (eid == 0 || v == id[PEMU_F] || ANY(v)) id[PEMU_F] = v;
        return (long) old; }                                     /* the previous value, also on failure */
    case PEMU_GETUID: case PEMU_GETGID: return (long) id[PEMU_R];
    case PEMU_GETEUID: case PEMU_GETEGID: return (long) id[PEMU_E];
    case PEMU_GETRESUID: case PEMU_GETRESGID: {
        uint64_t pp[3] = { a0, a1, a2 };
        for (int i = 0; i < 3; i++) if (pp[i]) *(uint32_t*) pp[i] = id[i];
        return 0; }
    }
#undef UNSET
#undef ANY
    return -ENOSYS;
}
/* execve of a set-uid / set-gid file: effective and saved id become 0; undone when the exec fails */
struct pemu_idsave { unsigned u[4], g[4]; };
static void pemu_exec_ids(const char *host, struct pemu_idsave *sv) {
    memcpy(sv->u, pemu_uid, sizeof sv->u); memcpy(sv->g, pemu_gid, sizeof sv->g);
    struct stat st;
    if (!pemu_fakeroot || stat(host, &st) < 0) return;
    if (st.st_mode & S_ISUID) pemu_uid[PEMU_E] = pemu_uid[PEMU_S] = 0;
    if (st.st_mode & S_ISGID) pemu_gid[PEMU_E] = pemu_gid[PEMU_S] = 0;
}
static void pemu_exec_ids_undo(const struct pemu_idsave *sv) { memcpy(pemu_uid, sv->u, sizeof pemu_uid); memcpy(pemu_gid, sv->g, sizeof pemu_gid); }
