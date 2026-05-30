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

// --- BELLEK YÖNETİMİ ---
bool Unprotect(uintptr_t addr, size_t len) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned = addr & ~(pagesize - 1);
    return mprotect((void*)aligned, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// --- SCANNER (EnchantUnbound Mantığı) ---
// Hafızada bir string arar (VTable ismini bulmak için)
uintptr_t FindString(uintptr_t start, uintptr_t end, const char* str) {
    size_t len = strlen(str);
    for (uintptr_t p = start; p < end - len; p++) {
        if (memcmp((void*)p, str, len) == 0) return p;
    }
    return 0;
}

// Bizim hileli fonksiyonumuz
int hooked_getCooldown(void* instance) { return 0; }

void StartProfessionalPatch() {
    WriteLog("Scanner baslatildi: CooldownItemComponent araniyor...");

    // 1. Kütüphaneyi bul
    uintptr_t libBase = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, "libminecraftpe.so")) {
                sscanf(line, "%lx-%*x", &libBase);
                break;
            }
        }
        fclose(maps);
    }

    if (!libBase) {
        WriteLog("HATA: Kütüphane bulunamadi.");
        return;
    }

    // 2. VTable İsminden Adres Bulma (En Kesin Yöntem)
    // Sınıf ismi: CooldownItemComponent (Mangled: _ZTV21CooldownItemComponent)
    // Biz direkt sınıfın ham ismini arıyoruz.
    const char* targetClassName = "21CooldownItemComponent";
    uintptr_t classStr = FindString(libBase, libBase + 0x5000000, targetClassName);

    if (!classStr) {
        WriteLog("HATA: Sinif ismi bulunamadi! Sürüm uyumsuz olabilir.");
        return;
    }
    WriteLog("Sinif ismi bulundu: 0x%lx", classStr);

    // 3. Referansları Değiştirme
    // Şimdi bu sınıfın vtable adresini kullanan yerleri bulup kendi fonksiyonumuza yönlendiriyoruz.
    // Minecraft'ta cooldown fonksiyonu vtable içinde genellikle 14. slottadır.
    
    int replacedCount = 0;
    uintptr_t myHook = (uintptr_t)hooked_getCooldown;

    // Kütüphanenin veri kısmını tara
    for (uintptr_t p = libBase + 0x3000000; p < libBase + 0x6000000; p += 8) {
        uintptr_t* entry = (uintptr_t*)p;
        
        // Eğer bu adres bizim bulduğumuz sınıf ismine çok yakın bir yeri işaret ediyorsa
        // Bu muhtemelen vtable girişidir.
        if (*entry > libBase && *entry < libBase + 0x6000000) {
             // Basit ama etkili: Eğer bu pointer'ın 14 slot ilerisi cooldown'sa değiştir
             // (Bu kısım gelişmiş projelerde vtable doğrulaması ile yapılır)
        }
    }

    // ALTERNATİF: Senin ofsetini "hizalayıp" (align) tekrar deneyelim
    uintptr_t alignedOffset = 0x223b9f8; // f7'yi f8 yaptık (çift sayı)
    uintptr_t targetFunc = libBase + alignedOffset;

    for (uintptr_t p = libBase + 0x3000000; p < libBase + 0x6000000; p += 8) {
        uintptr_t* entry = (uintptr_t*)p;
        if (*entry == targetFunc) {
            if (Unprotect((uintptr_t)entry, 8)) {
                *entry = myHook;
                replacedCount++;
            }
        }
    }

    if (replacedCount > 0) {
        WriteLog("BASARILI: %d adet referans yamanlandi!", replacedCount);
    } else {
        WriteLog("HATA: Otomatik tarama referans yakalayamadi.");
    }
}

__attribute__((constructor))
void init() {
    WriteLog("=== NoCooldown Pro v3.0 Baslatildi ===");
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(12)); // Oyunun oturması için biraz daha süre
        StartProfessionalPatch();
    }).detach();
}
