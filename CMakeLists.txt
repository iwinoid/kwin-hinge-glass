cmake_minimum_required(VERSION 3.16)
project(kwin-hinge-glass VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# ECM 必须带最小版本号，否则 ECMConfig.cmake 里
#     set(ECM_GLOBAL_FIND_VERSION "${ECM_FIND_VERSION}")
# 会把 ECM_GLOBAL_FIND_VERSION 设成空字符串（ECM_FIND_VERSION 只在带版本查找时才存在），
# 而 ECM 内部多处依赖它做比较，会同时坏掉两件事：
#
#   1) QtVersionOption.cmake  ->  `if (ECM_GLOBAL_FIND_VERSION VERSION_GREATER_EQUAL 5.240)`
#      比不过 -> BUILD_WITH_QT6=OFF -> QT_MAJOR_VERSION=5
#      -> include(KDEInstallDirs) 实际加载 KDEInstallDirs5.cmake
#      -> KDE_INSTALL_PLUGINDIR 变成 lib/qt/plugins（KWin 不扫描这个目录）
#      -> 且 QT_DEFAULT_MAJOR_VERSION 被设成 5，之后 find_package(Qt6) 因
#         `if(NOT DEFINED ...)` 守卫改不回来，Qt6QmlMacros.cmake 直接 FATAL_ERROR
#
#   2) KDECMakeSettings.cmake ->  `if (WIN32 OR ECM_GLOBAL_FIND_VERSION VERSION_GREATER_EQUAL 5.38.0)`
#      比不过 -> CMAKE_LIBRARY_OUTPUT_DIRECTORY 不被设置
#      -> kcoreaddons_add_plugin() 报 FATAL_ERROR
#
# 参考项目 kwin-effects-better-blur-dx 的 CMakeLists.txt:11 用 set(QT_MAJOR_VERSION 6)
# 绕过了第 1 条，但它没有 ECM 版本号，第 2 条仍然潜伏（它不用 kcoreaddons_add_plugin 才没暴露）。
set(ECM_MIN_VERSION 6.0.0)
find_package(ECM ${ECM_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

# 双保险：即使 ECM 版本判断失效，也钉死 Qt6
set(QT_MAJOR_VERSION 6)

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus OpenGL)
find_package(KF6 REQUIRED COMPONENTS Config CoreAddons I18n WidgetsAddons KCMUtils)
find_package(KWin REQUIRED COMPONENTS kwineffects)
find_package(KWinDBusInterface CONFIG REQUIRED)
find_package(epoxy REQUIRED)

if(KWin_VERSION VERSION_LESS 6.7)
    message(FATAL_ERROR "铰链玻璃只支持 KWin 6.7 及以上，当前是 ${KWin_VERSION}")
endif()
message(STATUS "目标 KWin 版本: ${KWin_VERSION}")

# 上游 KWin 的 KWinConfig.cmake 里有变量名拼写不一致
#   CMakeLists.txt:485   CONFIG_CODE_VARIABLE  PACKAGE_SETUP_KWINEFFECTS_AUTOMOC_VARIABLES
#   KWinConfig.cmake.in  @PACKAGE_SETUP_KWIN_AUTOMOC_VARIABLES@      ← 少了 EFFECTS
# 导致 @...@ 替换为空，AUTOMOC 拿不到 KWIN_EFFECT_FACTORY* 的宏名。
# 这里自行补上。
foreach(_macro KWIN_EFFECT_FACTORY
               KWIN_EFFECT_FACTORY_ENABLED
               KWIN_EFFECT_FACTORY_SUPPORTED
               KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED)
    list(APPEND CMAKE_AUTOMOC_MACRO_NAMES ${_macro})
endforeach()
list(APPEND CMAKE_AUTOMOC_DEPEND_FILTERS
     "KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED"
     "[\n^][ \t]*KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED[ \t\n]*\\([^,]*,[ \t\n]*\"([^\"]+)\"")

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(tools/preview)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
