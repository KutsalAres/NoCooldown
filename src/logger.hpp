#pragma once
#define MOD_NAME "NoCooldown"
#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, MOD_NAME, __VA_ARGS__)
#endif
