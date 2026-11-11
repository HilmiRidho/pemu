/* linux-user/elf_check.inc.h - program header sanity for both ELF loaders (AArch64 and x86_64). SPDX-License-Identifier: Apache-2.0
 * The loaders copy each PT_LOAD with pread() into memory they map from the header's own numbers.  A damaged or hostile file must be
 * refused up front, as the kernel does (ENOEXEC), not half loaded: a file size larger than the memory size wrote past the mapping, a
 * wrapped p_vaddr + p_memsz made mmap() map a huge or empty range, and a file with no PT_LOAD "loaded" and then jumped to its entry. */
static int elf_check(const Elf64_Ehdr *eh, const Elf64_Phdr *ph, const char *path) {
    const char *why = NULL; int loads = 0;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) why = "not an executable or a shared object";
    else if (eh->e_phentsize != sizeof(Elf64_Phdr)) why = "bad program header size";
    for (int k = 0; !why && k < eh->e_phnum; k++) {
        const Elf64_Phdr *p = &ph[k];
        if (p->p_type != PT_LOAD) continue;
        loads++;
        if (p->p_filesz > p->p_memsz) why = "segment is larger in the file than in memory";
        else if (p->p_vaddr >= (1ULL << 47) || p->p_memsz >= (1ULL << 47)) why = "segment address out of range";   /* the sum below cannot wrap */
    }
    if (!why && !loads) why = "no loadable segment";
    if (why) fprintf(stderr, "pemu: %s: %s\n", path, why);
    return why ? -1 : 0;
}
