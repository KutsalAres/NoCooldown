#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define LOG_TAG "NoCooldown"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Hook fonksiyonunu dışarıdan alıyoruz (Dobby.h gerektirmez)
extern "C" void DobbyHook(void* address, void* replace, void** origin);

static uintptr_t s_libBase = 0;
static const uintptr_t COOLDOWN_OFFSET = 0x223b9f7;

// Log yazma
void WriteToLog(const char* txt) {
    LOGI("%s", txt);
    FILE* f = fopen("/storage/emulated/0/nocooldown_log.txt", "a");
    if (f) {
        time_t now = time(0);
        char* dt = ctime(&now);
        dt[strlen(dt) - 1] = '\0';
        fprintf(f, "[%s] %s\n", dt, txt);
        fclose(f);
    }
}

// Orijinal fonksiyon yedeği
int (*old_getCooldown)(void* instance, void* player);

// Bizim hileli fonksiyonumuz
int hooked_getCooldown(void* instance, void* player) {
    return 0; // Bekleme süresini sıfırla
}

void ApplyHook() {
    // Kütüphane adresini bul
    void* handle = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!handle) {
        WriteToLog("Hata: libminecraftpe.so bulunamadi!");
        return;
    }

    Dl_info info;
    if (dladdr(handle, &info)) {
        s_libBase = (uintptr_t)info.dli_fbase;
    }
    dlclose(handle);

    if (s_libBase != 0) {
        uintptr_t target = s_libBase + COOLDOWN_OFFSET;
        char buf[128];
        sprintf(buf, "Hedef Adres: 0x%lx", target);
        WriteToLog(buf);

        DobbyHook((void*)target, (void*)hooked_getCooldown, (void**)&old_getCooldown);
        WriteToLog("Hook basariyla uygulandi!");
    }
}

__attribute__((constructor))
void init() {
    WriteToLog("Mod Yukleniyor...");
    ApplyHook();
}
