set(kwin_hinge_glass_config_SRCS
    hinge_glass_config.cpp
)

# 从 XML 生成 DBus 接口代码（生成的类名是 OrgKdeKwinEffectsInterface，
# 头文件是 kwineffects_interface.h）。KWinDBusInterface 不导出库，
# 只提供 XML 路径变量，没有可以 target_link_libraries 的库。
qt_add_dbus_interface(kwin_hinge_glass_config_SRCS ${KWIN_EFFECTS_INTERFACE} kwineffects_interface)

# 配置项 schema 与特效共用同一份 kcfg / kcfgc
kconfig_add_kcfg_files(kwin_hinge_glass_config_SRCS ../hinge_glassconfig.kcfgc)

add_library(kwin_hinge_glass_config MODULE ${kwin_hinge_glass_config_SRCS})
set_target_properties(kwin_hinge_glass_config PROPERTIES PREFIX "")

target_include_directories(kwin_hinge_glass_config PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(kwin_hinge_glass_config PRIVATE
    KF6::KCMUtils
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::I18n
    KF6::WidgetsAddons
    Qt6::DBus
    Qt6::Widgets
)

target_compile_options(kwin_hinge_glass_config PRIVATE -Wall -Wextra)

install(TARGETS kwin_hinge_glass_config
        DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/configs)
