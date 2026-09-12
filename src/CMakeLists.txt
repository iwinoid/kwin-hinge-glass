set(hinge_glass_SOURCES
    hinge_glass.cpp
    hingesensor.cpp
    hingestate.cpp
    lidsensor.cpp
    main.cpp
)

kconfig_add_kcfg_files(hinge_glass_SOURCES hinge_glassconfig.kcfgc)

kcoreaddons_add_plugin(hinge_glass
    SOURCES ${hinge_glass_SOURCES}
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

# 产出 hinge_glass.so 而不是 libhinge_glass.so，消除 pluginId 的 lib 前缀歧义
set_target_properties(hinge_glass PROPERTIES PREFIX "")

target_link_libraries(hinge_glass PRIVATE
    KWin::kwin
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    epoxy
)

target_compile_options(hinge_glass PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(kcm)
