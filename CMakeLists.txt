cmake_minimum_required(VERSION 3.28)

project(bacons-helper-obs VERSION 0.7.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# On Windows, libobs and obs-frontend-api are development packages, not part of
# the normal OBS installation. The packaging script bootstraps them from the
# official OBS plugin template and passes their package directories here.
if(WIN32 AND (NOT libobs_DIR OR NOT EXISTS "${libobs_DIR}/libobsConfig.cmake"))
    message(FATAL_ERROR
        "OBS development packages are not configured. "
        "Run .\\scripts\\package-windows.ps1 from the repository root, "
        "or provide libobs_DIR, obs-frontend-api_DIR, and Qt6_DIR manually."
    )
endif()

set(BH_OBS_REQUIRED_SDK_RANGE "" CACHE STRING "OBS SDK version range required when packaging")
find_package(libobs ${BH_OBS_REQUIRED_SDK_RANGE} CONFIG REQUIRED)
find_package(obs-frontend-api ${BH_OBS_REQUIRED_SDK_RANGE} CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_library(bacons-helper MODULE
    src/plugin-main.cpp
    src/bacons-helper-dock.cpp
    src/bacons-helper-dock.hpp
    src/language-panel.cpp
    src/language-panel.hpp
    src/native-language-detector.cpp
    src/native-language-detector.hpp
    src/native-language-worker.cpp
    src/language-feedback.cpp
    src/language-feedback.hpp
    src/native-language-worker.hpp
)

target_link_libraries(bacons-helper PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

option(BH_LINUX_SECRET_SERVICE "Persist Linux credentials with Secret Service/libsecret" ON)

if(WIN32)
    target_link_libraries(bacons-helper PRIVATE Crypt32)
elseif(UNIX AND NOT APPLE AND BH_LINUX_SECRET_SERVICE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    target_link_libraries(bacons-helper PRIVATE PkgConfig::LIBSECRET)
    target_compile_definitions(bacons-helper PRIVATE BH_HAVE_LIBSECRET=1)
endif()

target_compile_definitions(bacons-helper PRIVATE
    BH_OBS_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS=1
)

set_target_properties(bacons-helper PROPERTIES
    PREFIX ""
    OUTPUT_NAME "bacons-helper"
)

# Generate OBS manager metadata and stage the selected third-party layout.
include(cmake/PluginInstall.cmake)
bh_install_plugin(bacons-helper "${CMAKE_CURRENT_SOURCE_DIR}/data")
