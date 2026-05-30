#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <string>
#include <thread>
#include <chrono>

#define LOG_TAG "LeviMod_NoCooldown"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Levi ekosisteminde hazır bulunan DobbyHook'u dışarıdan alıyoruz
extern "C" int DobbyHook(void* address, void* replace, void** origin);

static uintptr_t s_libBase = 0;
// Ofsetinden eminsin, o yüzden burayı sabit tutuyoruz.
static const uintptr_t COOLDOWN_OFFSET = 0x223b9f7; 

// Orijinal fonksiyonun yedeği
int (*old_getCooldown)(void* instance, void* player) = nullptr;

// Hileli fonksiyon
int hooked_getCooldown(void* instance, void* player) {
    return 0; // Tick sayısını 0 döndür
}

void MainThread() {
    LOGI("Mod baslatiliyor, libminecraftpe.so bekleniyor...");
    
    void* handle = nullptr;
    // Kütüphane yüklenene kadar kısa bir döngü (Levi projelerinde sık yapılır)
    for(int i = 0; i < 100; i++) {
        handle = dlopen("libminecraftpe.so", RTLD_NOLOAD);
        if (handle) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!handle) handle = dlopen("libminecraftpe.so", RTLD_LAZY);

    if (handle) {
        Dl_info info;
        if (dladdr((void*)dlsym(handle, "JNI_OnLoad"), &info)) {
            s_libBase = (uintptr_t)info.dli_fbase;
            LOGI("Kütüphane taban adresi bulundu: 0x%lx", s_libBase);
            
            uintptr_t target = s_libBase + COOLDOWN_OFFSET;
            LOGI("Hook hedef adresi: 0x%lx", target);

            // Hook atma işlemi
            int ret = DobbyHook((void*)target, (void*)hooked_getCooldown, (void**)&old_getCooldown);
            if (ret == 0) {
                LOGI("TEBRİKLER: Hook basariyla uygulandi!");
            } else {
                LOGI("HATA: DobbyHook kodu %d döndürdü.", ret);
            }
        }
        dlclose(handle);
    } else {
        LOGI("KRİTİK HATA: libminecraftpe.so yüklenemedi!");
    }
}

__attribute__((constructor))
void init() {
    // Modun oyunun ana işleyişini kilitlememesi için ayrı bir thread'de başlatalım
    // EnchantUnbound tarzı projeler genellikle yükleme sırasını beklemek için bunu yapar
    std::thread(MainThread).detach();
}
