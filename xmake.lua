add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("levilamina 0.13.4")

target("NoCooldown")
    set_kind("shared")
    set_languages("c++20")
    add_files("src/*.cpp")
    add_packages("levilamina")
    set_symbols("debug")
    set_optimize("fastest")
