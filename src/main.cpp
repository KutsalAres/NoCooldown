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

// --- AYARLAR VE LOG ---
#define LOG_TAG "LeviMod_NoCooldown"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Termux'ta bulduğumuz altın bilgiler
static const uintptr_t COOLDOWN_OFFSET = 0x223b9f7; 
static const char* TARGET_LIB = "libminecraftpe.so";

// --- YARDIMCI FONKSİYONLAR (EnchantUnbound Stilinde) ---

// Kütüphanenin belirli bir bölümünü (örneğin .text veya .data.rel.ro) bulur
uintptr_t GetLibSection(const char* libname, const char* section_name, size_t* out_size) {
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
    return base_addr; // Basitleştirilmiş versiyon, base adresi döndürür
}

// Bellek yazma izinlerini açar (Crash koruması)
bool Unprotect(uintptr_t addr, size_t len) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t aligned_addr = addr & ~(pagesize - 1);
    return mprotect((void*)aligned_addr, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// --- HİLE MANTIĞI ---

// Bizim "Sıfır Bekleme" fonksiyonumuz
int hooked_getCooldownTicks(void* instance) {
    // Oyun bu fonksiyonu çağırdığında her zaman 0 döner, bekleme biter.
    return 0; 
}

// Tüm referansları yönlendiren ana fonksiyon
void ApplyProfessionalPatch() {
    LOG("No-Cooldown Yama islemi baslatildi...");

    // 1. Kütüphane taban adresini al
    uintptr_t libBase = GetLibSection(TARGET_LIB, nullptr, nullptr);
    if (!libBase) {
        LOG("HATA: libminecraftpe.so hafizada bulunamadi!");
        return;
    }

    // 2. Termux offset'ini kullanarak orijinal fonksiyonu bul
    uintptr_t originalFuncAddr = libBase + COOLDOWN_OFFSET;
    uintptr_t myHook = (uintptr_t)hooked_getCooldownTicks;

    LOG("Orijinal Fonksiyon: 0x%lx", originalFuncAddr);
    LOG("Yama Fonksiyonu:    0x%lx", myHook);

    // 3. .data.rel.ro Bölümünü Tara (EnchantUnbound'un en güçlü taktiği)
    // Bu kısım, vtable içindeki ve dışındaki tüm çağrıları yakalar.
    // Not: Gerçek projede section boundary'leri GetLibSection ile çekilir. 
    // Burada güvenlik için geniş bir tarama yapıyoruz.
    
    int replacedCount = 0;
    // Kütüphanenin veri kısmında orijinal fonksiyonun adresini ara
    // Genellikle base'den sonraki 20MB-50MB arası veri kısmıdır
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
        LOG("BASARILI: %d adet referans senin fonksiyonuna baglandi!", replacedCount);
    } else {
        // Eğer referans bulamazsak, direkt vtable slotunu zorla (B Planı)
        LOG("Uyari: Referans bulunamadi, manuel vtable denemesi...");
        // Burada vtable slotuna direkt yazma kodu eklenebilir.
    }
}

// --- GİRİŞ NOKTASI ---

__attribute__((constructor))
void init() {
    // Logcat'e ve varsa dosyaya yaz
    LOG("=== NoCooldown Mod Pro v2.0 Yuklendi ===");
    
    // Oyunun yüklenmesini beklemesi için bir thread açıyoruz
    std::thread([]() {
        LOG("Mod 10 saniye icinde aktif olacak...");
        std::this_thread::sleep_for(std::chrono::seconds(10));
        ApplyProfessionalPatch();
    }).detach();
}
