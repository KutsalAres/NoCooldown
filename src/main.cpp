#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>
#include <string>
#include <thread>
#include <chrono>

#define LOG_TAG "NoCooldownPro"
#define LOG_PATH "/storage/emulated/0/games/NoCooldown"
#define LOG_FILE "/storage/emulated/0/games/NoCooldown/mod_log.txt"

void WriteLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    mkdir("/storage/emulated/0/games", 0777);
    mkdir(LOG_PATH, 0777);
    FILE* f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(0);
        struct tm* tstruct = localtime(&now);
        char timeBuf[80];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tstruct);
        fprintf(f, "[%s] %s\n", timeBuf, buf);
        fclose(f);
    }
}

bool SafeUnprotect(uintptr_t addr) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    if (mprotect((void*)aligned, pagesize * 4, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) return true;
    return mprotect((void*)aligned, pagesize * 4, PROT_READ | PROT_WRITE) == 0;
}

uintptr_t GetLibSection(const char* libname, const char* section_name, size_t* out_size) {
    uintptr_t base_addr = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    char lib_path[512] = {0};
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libname)) {
            char path[256] = {0};
            if (sscanf(line, "%lx-%*x %*s %*x %*s %*d %255s", &base_addr, path) >= 1) {
                if (path[0] == '/') { strncpy(lib_path, path, sizeof(lib_path)-1); break; }
            }
        }
    }
    fclose(maps);
    if (base_addr == 0) return 0;
    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    fstat(fd, &st);
    void* map_base = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    uintptr_t section_addr = 0;
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)map_base;
    Elf64_Shdr* shdr = (Elf64_Shdr*)((uintptr_t)map_base + ehdr->e_shoff);
    const char* shstrtab = (const char*)((uintptr_t)map_base + shdr[ehdr->e_shstrndx].sh_offset);
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (strcmp(shstrtab + shdr[i].sh_name, section_name) == 0) {
            section_addr = base_addr + shdr[i].sh_addr;
            if (out_size) *out_size = shdr[i].sh_size;
            break;
        }
    }
    munmap(map_base, st.st_size);
    return section_addr;
}

void** FindVtable(const char* typeStr) {
    size_t rodataSize, drrSize;
    uintptr_t rodata = GetLibSection("libminecraftpe.so", ".rodata", &rodataSize);
    uintptr_t drr = GetLibSection("libminecraftpe.so", ".data.rel.ro", &drrSize);
    if (!rodata || !drr) return nullptr;
    char* ztsPtr = (char*)memmem((void*)rodata, rodataSize, typeStr, strlen(typeStr) + 1);
    if (!ztsPtr) return nullptr;
    uintptr_t zts = (uintptr_t)ztsPtr;
    uintptr_t zti = 0;
    for (size_t i = 0; i < drrSize; i += sizeof(uintptr_t)) {
        if (*(uintptr_t*)(drr + i) == zts) {
            zti = drr + i - sizeof(uintptr_t);
            break;
        }
    }
    if (!zti) return nullptr;
    for (size_t i = 0; i < drrSize; i += sizeof(uintptr_t)) {
        if (*(uintptr_t*)(drr + i) == zti) {
            return (void**)(drr + i + sizeof(uintptr_t));
        }
    }
    return nullptr;
}

// 0 döndüren cooldown fonksiyonu
int hooked_zero(void* a) { return 0; }

void ApplyPatch() {
    WriteLog("--- [v7.0 MULTI-SLOT] Baslatildi ---");
    void** vt = FindVtable("21CooldownItemComponent");
    if (!vt) {
        WriteLog("HATA: Vtable bulunamadi.");
        return;
    }
    WriteLog("Vtable Adresi: %p", vt);

    // Kritik cooldown slotları: 12, 13, 14, 15
    // Minecraft sürümüne göre değişebileceği için bu bloğu toplu yamalıyoruz
    int slots[] = {12, 13, 14, 15};
    
    for(int slot : slots) {
        uintptr_t slotAddr = (uintptr_t)&vt[slot];
        if (SafeUnprotect(slotAddr)) {
            *(uintptr_t*)slotAddr = (uintptr_t)hooked_zero;
            WriteLog("TAMAM: Slot %d yamalandi.", slot);
        }
    }

    WriteLog("--- YAMA TAMAMLANDI ---");
}

__attribute__((constructor))
void init() {
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(25));
        ApplyPatch();
    }).detach();
}
