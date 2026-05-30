#include "main.hpp"
#include <fstream>
#include <sys/stat.h>

#ifdef __ANDROID__

static uintptr_t s_rodata = 0, s_drr = 0, s_libBase = 0;
static size_t s_rodataSize = 0, s_drrSize = 0;
static FILE* g_log = nullptr;

// Klasör ve dosya yolları
const char* baseDir = "/storage/emulated/0/games/NoCooldown";
const char* logPath = "/storage/emulated/0/games/NoCooldown/latest_log.txt";
const char* configPath = "/storage/emulated/0/games/NoCooldown/Config.json";

void WriteLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Zaman damgası ekleyelim
    time_t now = time(0);
    struct tm tstruct = *localtime(&now);
    char timeBuf[80];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tstruct);

    LOG("[NoCooldown] %s", buf);
    if (g_log) {
        fprintf(g_log, "[%s] %s\n", timeBuf, buf);
        fflush(g_log);
    }
}

// Klasör yoksa oluştur
void EnsureDirectoryExists(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0700);
    }
}

// Basit bir JSON okuyucu (Harici kütüphane eklememek için manuel)
int ReadSlotFromConfig() {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        // Dosya yoksa varsayılan oluştur
        std::ofstream outfile(configPath);
        outfile << "{\n  \"target_slot\": 14\n}";
        outfile.close();
        return 14;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("target_slot") != std::string::npos) {
            size_t colon = line.find(":");
            return std::stoi(line.substr(colon + 1));
        }
    }
    return 14;
}

// Bellek izinleri
bool SetMemoryPermission(uintptr_t addr, size_t len, int prot) {
    if (!addr || !len) return false;
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    size_t aligned_len = ((addr + len + pagesize - 1) & ~(pagesize - 1)) - aligned;
    return mprotect((void*)aligned, aligned_len, prot) == 0;
}

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
        if (fd < 0) return 0;
        struct stat st;
        fstat(fd, &st);
        void* map_base = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map_base == MAP_FAILED) return 0;

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
    return 0;
}

void** FindVtable(const char* typeStr) {
    if (!s_rodata) {
        s_rodata = GetLibSection("libminecraftpe.so", ".rodata", &s_rodataSize);
        s_drr    = GetLibSection("libminecraftpe.so", ".data.rel.ro", &s_drrSize);
        Dl_info info;
        if (dladdr((void*)s_rodata, &info)) s_libBase = (uintptr_t)info.dli_fbase;
        WriteLog("Hafıza Haritası: Base=0x%lX | Rodata=0x%lX", s_libBase, s_rodata);
    }

    char* ztsPtr = (char*)memmem((void*)s_rodata, s_rodataSize, typeStr, strlen(typeStr) + 1);
    if (!ztsPtr) {
        WriteLog("Kritik Hata: %s (ZTS) bulunamadı!", typeStr);
        return nullptr;
    }

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

// Hook Fonksiyonu
int Hook_ReturnZero(void* instance) {
    return 0;
}

void ApplyPatch() {
    int targetSlot = ReadSlotFromConfig();
    WriteLog("Config okundu. Hedef Slot: %d", targetSlot);

    const char* className = "21CooldownItemComponent";
    void** vt = FindVtable(className);

    if (vt) {
        uintptr_t slotAddr = (uintptr_t)&vt[targetSlot];
        uintptr_t originalFn = (uintptr_t)vt[targetSlot];

        WriteLog("Orijinal Fonksiyon Ofseti (vt[%d]): 0x%lX", targetSlot, originalFn - s_libBase);

        if (SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ | PROT_WRITE)) {
            vt[targetSlot] = (void*)Hook_ReturnZero;
            SetMemoryPermission(slotAddr, sizeof(uintptr_t), PROT_READ);
            WriteLog("BAŞARILI: Slot %d yamalandı.", targetSlot);
        } else {
            WriteLog("HATA: Bellek yazma izni alınamadı!");
        }
    }
}

__attribute__((constructor))
void Init() {
    EnsureDirectoryExists(baseDir);
    g_log = fopen(logPath, "w");
    
    WriteLog("=== NoCooldown Mod Pro v1.0 ===");
    WriteLog("Geliştirici: KutsalAres");
    
    ApplyPatch();
    
    WriteLog("=== Başlatma Tamamlandı ===");
}

#endif
