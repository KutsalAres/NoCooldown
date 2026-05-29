add_rules("mode.release", "mode.debug")

target("NoCooldown")
    set_kind("shared")
    set_languages("c++20")
    add_files("src/main.cpp")
    add_includedirs("src")
    add_syslinks("android", "log")
    if is_plat("android") then
        set_arch("arm64-v8a")
    end
