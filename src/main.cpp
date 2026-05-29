#include <cstdint>
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/memory/Hook.h"

// Item Cooldown'u kaldıran hook
LL_AUTO_TYPED_INSTANCE_HOOK(
    ItemCooldownHook,
    ll::memory::HookPriority::Normal,
    Actor,
    "?isOnCooldown@ItemStack@@QEBAXAEAVPlayer@@@Z",
    bool,
    class ItemStack const& item
) {
    return false; // her zaman cooldown yok
}

LL_REGISTER_MOD(NoCooldownMod, instance);
