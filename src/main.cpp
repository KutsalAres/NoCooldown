#include "main.hpp"

#ifdef __ANDROID__

static uintptr_t s_rodata = 0, s_drr = 0, s_libBase = 0;
static size_t s_rodataSize = 0, s_drrSize = 0;
static FILE* g_log = nullptr;

// Log yazma fonksiyonu
void WriteLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOG("[NoCooldown] %s", buf);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

// Bellek izinlerini ayarlama (Patch atmak için şart)
bool SetMemoryPermission(uintptr_t addr, size_t len, int prot) {
    if (!addr || !len) return false;
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    size_t aligned_len = ((addr + len + pagesize - 1) & ~(pagesize - 1)) - aligned;
    return mprotect((void*)aligned, aligned_len, prot) == 0;
}

// libminecraftpe.so içindeki bölümleri (.rodata vb.) bulma
uintptr_t GetLibSection(const char* libname, const char* section_name, size_t* out_size) {
    uintptr_t base_addr = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libname)) {
            if (sscanf(line, "%llx", (unsigned long long*)&base_addr) >= 1) break;
        }
    }
    fclose(maps);

    Dl_info info;
    if (dladdr((void*)base_addr, &info)) {
        int fd = open(info.dli_fname, O_RDONLY);
        if (fd >= 0) {
            struct stat st;
            fstat(fd, &st);
            void* map_base = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (map_base != MAP_FAILED) {
                ElfW(Ehdr)* ehdr = (ElfW(Ehdr)*)map_base;
                ElfW(Shdr)* shdr = (ElfW(Shdr)*)((uintptr_t)map_base + ehdr->e_shoff);
                const char* shstrtab = (const char*)((uintptr_t)map_base + shdr[ehdr->e_shstrndx].sh_offset);
                for (int i = 0; i < ehdr->e_shnum; i++) {
                    if (strcmp(shstrtab + shdr[i].sh_name, section_name) == 0) {
                        uintptr_t res = base_addr + shdr[i].sh_addr;
                        if (out_size) *out_size = shdr[i].sh_size;
                        munmap(map_base, st.st_size);
                        return res;
                    }
                }
                munmap(map_base, st.st_size);
            }
        }
    }
    return 0;
}

// VTable adresini bulma algoritması
void** FindVtable(const char* typeStr) {
    if (!s_rodata) {
        s_rodata = GetLibSection("libminecraftpe.so", ".rodata", &s_rodataSize);
        s_drr    = GetLibSection("libminecraftpe.so", ".data.rel.ro", &s_drrSize);
        Dl_info info;
        if (dladdr((void*)s_rodata, &info)) s_libBase = (uintptr_t)info.dli_fbase;
        WriteLog("LibBase: 0x%lX | rodata: 0x%lX", s_libBase, s_rodata);
    }

    char* ztsPtr = (char*)memmem((void*)s_rodata, s_rodataSize, typeStr, strlen(typeStr) + 1);
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

    for (size_t i = 0; i < s_drrSize; i += sizeof(uintptr_t)) {
        if (*(uintptr_t*)(s_drr + i) == zti) {
            return (void**)(s_drr + i + sizeof(uintptr_t));
        }
    }
    return nullptr;
}

// --- MOD MANTIĞI ---

// Oyun cooldown sorduğunda her zaman 0 (tick) kaldı diyeceğiz.
int Hook_AlwaysZero(void* instance) {
    return 0;
}

void ApplyNoCooldown() {
    // Termux strings çıktısında en güvenilir görünen sınıf
    const char* target = "21CooldownItemComponent";
    
    void** vt = FindVtable(target);
    if (!vt) {
        WriteLog("Hata: %s vtable bulunamadi!", target);
        return;
    }

    WriteLog("%s bulundu, yamalaniyor...", target);

    // Tehlikeli (0,1,2) slotları atlayıp, cooldown ile ilgili olabilecek 
    // slotları (9, 10, 12) hedefliyoruz.
    int targets[] = {9, 10, 12}; 

    for (int slot : targets) {
        uintptr_t slotAddr = (uintptr_t)&vt[slot];
        if (SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ | PROT_WRITE)) {
            vt[slot] = (void*)Hook_AlwaysZero;
            SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ);
            WriteLog("Slot %d yamalandi (0 döndürecek)", slot);
        }
    }
}

__attribute__((constructor))
void Init() {
    g_log = fopen("/storage/emulated/0/nocooldown_log.txt", "w");
    WriteLog("--- NoCooldown Mod Baslatildi ---");
    ApplyNoCooldown();
    WriteLog("--- Islem Tamam ---");
}

#endif
