#include "main.hpp"
#include <dlfcn.h>
#include <sys/mman.h>
#include <string>
#include <ctime>

// Hook kütüphanesi (LeviLauncher genelde Dobby kullanır)
#include "dobby.h" 

#ifdef __ANDROID__

// AYARLAR
#define TARGET_LIB "libminecraftpe.so"
#define COOLDOWN_OFFSET 0x223b9f7 // Senin bulduğun altın ofset

static uintptr_t s_libBase = 0;
static FILE* g_log = nullptr;

// Detaylı Log Sistemi
void WriteLog(const char* level, const char* fmt, ...) {
    if (!g_log) return;

    time_t now = time(0);
    struct tm* tstruct = localtime(&now);
    char timeBuf[20];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tstruct);

    char messageBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(messageBuf, sizeof(messageBuf), fmt, args);
    va_end(args);

    fprintf(g_log, "[%s] [%s] %s\n", timeBuf, level, messageBuf);
    fflush(g_log);
    
    // Aynı zamanda Android Logcat'e de gönder
    LOG("[NoCooldown] %s: %s", level, messageBuf);
}

// Orijinal Fonksiyonun Yedeği (Oyunun çökmemesi için tip tanımı şart)
// Parametreler: (Bileşen örneği, oyuncu örneği)
int (*old_getCooldownTicksRemaining)(void* instance, void* player);

// Hileli Fonksiyon (Hook)
int hooked_getCooldownTicksRemaining(void* instance, void* player) {
    // Oyun "Kaç tick kaldı?" diye sorduğunda her zaman 0 diyoruz.
    // Bu sayede bekleme süresi anında dolar.
    return 0; 
}

void ApplyDirectHook() {
    WriteLog("INFO", "Yükleme başlatılıyor...");

    // 1. Kütüphane Adresini Bul
    Dl_info info;
    if (dladdr((void*)WriteLog, &info)) {
        s_libBase = (uintptr_t)info.dli_fbase;
        WriteLog("INFO", "Library Base: 0x%lX", s_libBase);
    }

    if (!s_libBase) {
        WriteLog("ERROR", "Kütüphane taban adresi bulunamadı!");
        return;
    }

    // 2. Hedef Adresi Hesapla
    uintptr_t targetAddr = s_libBase + COOLDOWN_OFFSET;
    WriteLog("INFO", "Hedef Ofset: 0x%lX", COOLDOWN_OFFSET);
    WriteLog("INFO", "Çalışma Zamanı Adresi: 0x%lX", targetAddr);

    // 3. Hook Atma İşlemi (Inline Hook)
    // Dobby, hedef adresteki ilk birkaç byte'ı JMP komutuyla değiştirir.
    dobby_enable_near_trampoline(); 
    
    DobbyHook(
        (void*)targetAddr,                          // Nereyi kancalıyoruz?
        (void*)hooked_getCooldownTicksRemaining,    // Kendi fonksiyonumuz
        (void**)&old_getCooldownTicksRemaining      // Orijinalin kopyası
    );

    WriteLog("SUCCESS", "Hook başarıyla yerleştirildi. No-Cooldown aktif.");
}

__attribute__((constructor))
void Init() {
    // Log dosyasını oluştur
    g_log = fopen("/storage/emulated/0/games/NoCooldown/pro_debug_log.txt", "w");
    if (!g_log) {
        // Eğer klasör yoksa fallback olarak ana dizine yaz
        g_log = fopen("/storage/emulated/0/nocooldown_debug.txt", "w");
    }

    WriteLog("START", "=== NoCooldown Profesyonel Ofset Modu Başlatıldı ===");
    WriteLog("INFO", "Hedef: %s", TARGET_LIB);
    
    ApplyDirectHook();
    
    WriteLog("END", "=== Başlatma İşlemi Tamamlandı ===");
}

#endif
