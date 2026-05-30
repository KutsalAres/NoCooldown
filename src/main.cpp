#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <thread> // EKSİKTİ, EKLENDİ
#include <chrono> // EKSİKTİ, EKLENDİ
#include <stdarg.h>

// --- AYARLAR VE LOG ---
#define LOG_TAG "NoCooldownPro"
#define LOG_PATH "/storage/emulated/0/games/NoCooldown/"
#define LOG_FILE "/storage/emulated/0/games/NoCooldown/mod_log.txt"

// Hem Logcat'e hem de dosyaya yazan zırhlı log fonksiyonu
void WriteLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // 1. Logcat (MatLog için)
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);

    // 2. Dosya Kaydı (İstediğin klasöre)
    mkdir("/storage/emulated/0/games", 0777);
    mkdir(LOG_PATH, 0777);
    
    FILE* f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(0);
        char* dt = ctime(&now);
        if(dt) dt[strlen(dt) - 1] = '\0';
        fprintf(f, "[%s] %s\n", dt ? dt : "??", buf);
        fclose(f);
    }
}

// Termux'ta bulduğumuz altın bilgiler
static const uintptr_t COOLDOWN_OFFSET = 0x223b9f7; 
static const char* TARGET_LIB = "libminecraftpe.so";

// --- YARDIMCI FONKSİYONLAR ---

uintptr_t GetLibBase(const char* libname) {
    uintptr_t base_addr = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libname)) {
            if (sscanf(line, "%lx-%*x", &base_addr) == 1) break;
        }
    }
    fclose(maps);
    return base_addr;
}

bool Unprotect(uintptr_t addr, size_t len) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned_addr = addr & ~(pagesize - 1);
    return mprotect((void*)aligned_addr, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// --- HİLE MANTIĞI ---

int hooked_getCooldownTicks(void* instance) {
    return 0; 
}

void ApplyProfessionalPatch() {
    WriteLog("Yama islemi baslatildi...");

    uintptr_t libBase = GetLibBase(TARGET_LIB);
    if (!libBase) {
        WriteLog("HATA: %s hafizada bulunamadi!", TARGET_LIB);
        return;
    }

    uintptr_t originalFuncAddr = libBase + COOLDOWN_OFFSET;
    uintptr_t myHook = (uintptr_t)hooked_getCooldownTicks;

    WriteLog("Base: 0x%lx | Hedef: 0x%lx", libBase, originalFuncAddr);

    int replacedCount = 0;
    // .data.rel.ro taraması (Önemli: Base'den sonraki 64MB'lık alanı tara)
    for (uintptr_t p = libBase; p < libBase + 0x4000000; p += sizeof(uintptr_t)) {
        uintptr_t* entry = (uintptr_t*)p;
        
        if (*entry == originalFuncAddr) {
            if (Unprotect((uintptr_t)entry, sizeof(uintptr_t))) {
                *entry = myHook;
                replacedCount++;
            }
        }
    }

    if (replacedCount > 0) {
        WriteLog("BASARILI: %d adet vtable referansi guncellendi!", replacedCount);
    } else {
        WriteLog("HATA: Referans bulunamadi. Offset veya surum hatasi olabilir.");
    }
}

// --- GİRİŞ NOKTASI ---

__attribute__((constructor))
void init() {
    WriteLog("=== NoCooldown Mod Yuklendi ===");
    
    // Ayrı bir thread açarak oyunu kilitlemeden bekle
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        ApplyProfessionalPatch();
    }).detach();
}
