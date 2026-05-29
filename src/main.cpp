#include "main.hpp"

#ifdef __ANDROID__

static uintptr_t s_rodata = 0, s_drr = 0, s_libBase = 0;
static size_t s_rodataSize = 0, s_drrSize = 0;

uintptr_t GetLibSection(const char* libname, const char* section_name, size_t* out_size) {
    if (!libname) return 0;
    if (!section_name) section_name = ".text";
    uintptr_t base_addr = 0;
    char lib_path[512] = {0};
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libname)) {
            char path[256] = {0};
            if (sscanf(line, "%llx-%*x %*s %*x %*s %*d %255s",
                (unsigned long long*)&base_addr, path) >= 1) {
                if (path[0] == '/') {
                    strncpy(lib_path, path, sizeof(lib_path) - 1);
                    break;
                }
            }
        }
    }
    fclose(maps);
    if (lib_path[0] == '\0' || base_addr == 0) return 0;

    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    void* map_base = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_base == MAP_FAILED) return 0;

    uintptr_t section_runtime_addr = 0;
    ElfW(Ehdr)* ehdr = (ElfW(Ehdr)*)map_base;
    if (ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
        ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
        ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
        ehdr->e_ident[EI_MAG3] == ELFMAG3) {
        ElfW(Shdr)* shdr = (ElfW(Shdr)*)((uintptr_t)map_base + ehdr->e_shoff);
        const char* shstrtab = (const char*)((uintptr_t)map_base + shdr[ehdr->e_shstrndx].sh_offset);
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const char* cur = shstrtab + shdr[i].sh_name;
            if (strcasecmp(cur, section_name) == 0) {
                section_runtime_addr = base_addr + shdr[i].sh_addr;
                if (out_size) *out_size = shdr[i].sh_size;
                break;
            }
        }
    }
    munmap(map_base, st.st_size);
    return section_runtime_addr;
}

bool SetMemoryPermission(uintptr_t addr, size_t len, int prot) {
    if (!addr || !len) return false;
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    size_t aligned_len = ((addr + len + pagesize - 1) & ~(pagesize - 1)) - aligned;
    return mprotect((void*)aligned, aligned_len, prot) == 0;
}

void** FindVtable(const char* typeStr) {
    if (!s_rodata) {
        s_rodata = GetLibSection("libminecraftpe.so", ".rodata", &s_rodataSize);
        s_drr    = GetLibSection("libminecraftpe.so", ".data.rel.ro", &s_drrSize);
        Dl_info info;
        if (dladdr((void*)s_rodata, &info))
            s_libBase = (uintptr_t)info.dli_fbase;
    }

    char* ztsPtr = nullptr;
    size_t classLen = strlen(typeStr);
    size_t offset = 0;
    while (offset < s_rodataSize) {
        char* match = (char*)memmem((void*)(s_rodata + offset),
                                     s_rodataSize - offset, typeStr, classLen + 1);
        if (!match) break;
        if (match == (char*)s_rodata || *(match - 1) == '\0') {
            ztsPtr = match; break;
        }
        offset = (uintptr_t)match - s_rodata + 1;
    }
    if (!ztsPtr) return nullptr;

    uintptr_t zts = (uintptr_t)ztsPtr;
    uintptr_t zti = 0;
    for (size_t i = 0; i < s_drrSize; i += sizeof(uintptr_t)) {
        if (*(uintptr_t*)(s_drr + i) == zts) {
            zti = s_drr + i - sizeof(uintptr_t);
            break;
        }
    }
    if (!zti) return nullptr;

    uintptr_t vtable = 0;
    for (size_t i = 0; i < s_drrSize; i += sizeof(uintptr_t)) {
        if (*(uintptr_t*)(s_drr + i) == zti) {
            uintptr_t potential = s_drr + i + sizeof(uintptr_t);
            if (i >= sizeof(uintptr_t) &&
                *(uintptr_t*)(s_drr + i - sizeof(uintptr_t)) == 0) {
                vtable = potential; break;
            }
            if (!vtable) vtable = potential;
        }
    }
    if (!vtable) return nullptr;

    LOG("[NoCooldown] %s -> ZTV: 0x%lX", typeStr, vtable - s_libBase);
    return (void**)vtable;
}

static void Hook_startCooldown(void*, void*, void*) {}
static void Hook_startItemCooldown(void*, void*, void*, int) {}

void HookCooldowns() {
    if (!s_drr)
        s_drr = GetLibSection("libminecraftpe.so", ".data.rel.ro", &s_drrSize);

    // CooldownItemComponent::startCooldown - slot 13
    void** vt1 = FindVtable("20CooldownItemComponent");
    if (vt1) {
        uintptr_t slotAddr = (uintptr_t)&vt1[13];
        if (SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ | PROT_WRITE)) {
            vt1[13] = (void*)Hook_startCooldown;
            SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ);
            LOG("[NoCooldown] Patched CooldownItemComponent slot 13");
        }
    } else {
        LOG("[NoCooldown] CooldownItemComponent vtable not found");
    }

    // ScriptItemCooldownComponent::startCooldown - slot 13
    void** vt2 = FindVtable("27ScriptItemCooldownComponent");
    if (vt2) {
        uintptr_t slotAddr = (uintptr_t)&vt2[13];
        if (SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ | PROT_WRITE)) {
            vt2[13] = (void*)Hook_startCooldown;
            SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ);
            LOG("[NoCooldown] Patched ScriptItemCooldownComponent slot 13");
        }
    } else {
        LOG("[NoCooldown] ScriptItemCooldownComponent vtable not found");
    }
}

__attribute__((constructor))
void Init() {
    LOG("[NoCooldown] Loaded");
    HookCooldowns();
}

#endif
