#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#define LOG_TAG "NoCooldownPro"
#define LOG_FILE "/storage/emulated/0/games/NoCooldown/mod_log.txt"

// --- LOG SİSTEMİ ---
void WriteLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    
    mkdir("/storage/emulated/0/games", 0777);
    mkdir("/storage/emulated/0/games/NoCooldown", 0777);
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

// --- BELLEK KORUMASI ---
bool Unprotect(uintptr_t addr, size_t len) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    return mprotect((void*)aligned, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// --- SCANNER MANTIĞI (EnchantUnbound Stilinde) ---

struct LibRegion { uintptr_t start, end; };

std::vector<LibRegion> GetLibRegions(const char* libname) {
    std::vector<LibRegion> regions;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, libname)) {
                uintptr_t s, e;
                sscanf(line, "%lx-%lx", &s, &e);
                regions.push_back({s, e});
            }
        }
        fclose(maps);
    }
    return regions;
}

uintptr_t FindAddressInRegions(const std::vector<LibRegion>& regions, uintptr_t targetAddr) {
    for (const auto& reg : regions) {
        for (uintptr_t p = reg.start; p < reg.end - sizeof(uintptr_t); p += sizeof(uintptr_t)) {
            if (*(uintptr_t*)p == targetAddr) return p;
        }
    }
    return 0;
}

int hooked_getCooldown(void* instance) { return 0; }

void StartProfessionalPatch() {
    WriteLog("--- Yama Islemi Baslatildi (Walker Mode) ---");

    auto regions = GetLibRegions("libminecraftpe.so");
    if (regions.empty()) {
        WriteLog("HATA: libminecraftpe.so bulunamadi.");
        return;
    }

    // 1. ADIM: Sınıf ismini bul (ZTS)
    const char* targetClassName = "21CooldownItemComponent";
    uintptr_t classStrAddr = 0;
    for (const auto& reg : regions) {
        for (uintptr_t p = reg.start; p < reg.end - strlen(targetClassName); p++) {
            if (memcmp((void*)p, targetClassName, strlen(targetClassName)) == 0) {
                classStrAddr = p;
                break;
            }
        }
        if (classStrAddr) break;
    }

    if (!classStrAddr) {
        WriteLog("HATA: Sinif ismi hafizada bulunamadi.");
        return;
    }
    WriteLog("Sinif ismi bulundu: 0x%lx", classStrAddr);

    // 2. ADIM: TypeInfo'yu (ZTI) bul (İsmi işaret eden pointer)
    uintptr_t ztiAddr = FindAddressInRegions(regions, classStrAddr);
    if (!ztiAddr) {
        WriteLog("HATA: TypeInfo (ZTI) bulunamadi.");
        return;
    }
    // ZTI genellikle pointer'dan 8-16 byte öncesinde başlar
    ztiAddr -= sizeof(uintptr_t); 
    WriteLog("TypeInfo (ZTI) adresi: 0x%lx", ztiAddr);

    // 3. ADIM: VTable'ı (ZTV) bul (TypeInfo'yu işaret eden pointer)
    uintptr_t vtableEntry = FindAddressInRegions(regions, ztiAddr);
    if (!vtableEntry) {
        WriteLog("HATA: VTable (ZTV) bulunamadi.");
        return;
    }
    
    // C++ ABI'da VTable, ZTI pointer'ından 8 byte sonra başlar
    uintptr_t vtableStart = vtableEntry + sizeof(uintptr_t);
    WriteLog("VTable baslangici: 0x%lx", vtableStart);

    // 4. ADIM: Slot Yamalama (Slot 14 genelde getCooldownTicks'tir)
    int targetSlot = 14; 
    uintptr_t patchAddr = vtableStart + (targetSlot * sizeof(uintptr_t));
    uintptr_t originalFunc = *(uintptr_t*)patchAddr;

    WriteLog("Orijinal Fonksiyon (Slot %d): 0x%lx", targetSlot, originalFunc);

    if (Unprotect(patchAddr, sizeof(uintptr_t))) {
        *(uintptr_t*)patchAddr = (uintptr_t)hooked_getCooldown;
        WriteLog("BASARILI: VTable yamalandi!");
    } else {
        WriteLog("HATA: Yazma izni alinamadi.");
        return;
    }

    // 5. ADIM: Tum referansları tara (Redirect)
    int replacedCount = 0;
    for (const auto& reg : regions) {
        // Sadece veri bölgelerini tarayarak hızı artırıyoruz
        for (uintptr_t p = reg.start; p < reg.end - sizeof(uintptr_t); p += sizeof(uintptr_t)) {
            if (*(uintptr_t*)p == originalFunc) {
                if (Unprotect(p, sizeof(uintptr_t))) {
                    *(uintptr_t*)p = (uintptr_t)hooked_getCooldown;
                    replacedCount++;
                }
            }
        }
    }
    WriteLog("Referans Yonlendirme: %d adet ek yer yamalandi.", replacedCount);
}

__attribute__((constructor))
void init() {
    WriteLog("=== NoCooldown Pro v4.0 Baslatildi ===");
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(15)); // Daha güvenli bir bekleme
        StartProfessionalPatch();
    }).detach();
}
