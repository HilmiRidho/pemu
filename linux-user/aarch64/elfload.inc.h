/* linux-user/aarch64/elfload.inc.h - ELF loading. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ ELF loading */
typedef struct { u64 entry, phdr, phnum, base; char interp[4096]; int has_interp; } ElfInfo;

static int load_elf(const char*path, u64 hint, ElfInfo*ei) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "pemu: %s: %s\n", path, strerror(errno)); return -1; }
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, "\177ELF", 4) || eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_AARCH64) {
        fprintf(stderr, "pemu: %s: not an aarch64 ELF executable\n", path); close(fd); return -1;
    }
    Elf64_Phdr*ph = malloc((size_t) eh.e_phnum * sizeof *ph);
    if (pread(fd, ph, (size_t) eh.e_phnum * sizeof *ph, (off_t) eh.e_phoff) != (ssize_t)(eh.e_phnum * sizeof *ph)) { close(fd); return -1; }
    u64 lo = ~0ULL, hi = 0;
    for (int k = 0; k < eh.e_phnum; k++) if (ph[k].p_type == PT_LOAD) {
        if (ph[k].p_vaddr < lo) lo = ph[k].p_vaddr;
        if (ph[k].p_vaddr + ph[k].p_memsz > hi) hi = ph[k].p_vaddr + ph[k].p_memsz;
    }
    lo &= ~4095ULL; hi = (hi + 4095) & ~4095ULL;
    u64 base = 0;
    if (eh.e_type == ET_DYN) {
        void*m = mmap(hint ? (void*) hint : NULL, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (hint ? MAP_FIXED_NOREPLACE : 0), -1, 0);
        if (m == MAP_FAILED) { perror("pemu: mmap"); return -1; }
        base = (u64) m - lo;
    }
    u64 phdr_addr = 0;
    for (int k = 0; k < eh.e_phnum; k++) {
        Elf64_Phdr*p = &ph[k];
        if (p->p_type == PT_INTERP && pread(fd, ei->interp, p->p_filesz < 4095 ? p->p_filesz : 4095, (off_t) p->p_offset) > 0) ei->has_interp = 1;
        if (p->p_type == PT_PHDR) phdr_addr = base + p->p_vaddr;
        if (p->p_type != PT_LOAD) continue;
        u64 s = (base + p->p_vaddr) & ~4095ULL, e = (base + p->p_vaddr + p->p_memsz + 4095) & ~4095ULL;
        void*m = mmap((void*) s, e - s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | (eh.e_type == ET_EXEC ? 0 : 0), -1, 0);
        if (m == MAP_FAILED) { perror("pemu: mmap segment"); return -1; }
        if (p->p_filesz && pread(fd, (void*)(base + p->p_vaddr), p->p_filesz, (off_t) p->p_offset) != (ssize_t) p->p_filesz) { fprintf(stderr, "pemu: short read\n"); return -1; }
        int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        mprotect((void*) s, e - s, prot | PROT_READ);
        if (!phdr_addr && p->p_offset <= eh.e_phoff && eh.e_phoff < p->p_offset + p->p_filesz) phdr_addr = base + p->p_vaddr + (eh.e_phoff - p->p_offset);
    }
    ei->entry = base + eh.e_entry; ei->phdr = phdr_addr; ei->phnum = eh.e_phnum; ei->base = base;
    free(ph); close(fd);
    return 0;
}

static u64 push_str(u64*sp, const char*s) { size_t n = strlen(s) + 1; *sp -= n; memcpy((void*)*sp, s, n); return *sp; }

