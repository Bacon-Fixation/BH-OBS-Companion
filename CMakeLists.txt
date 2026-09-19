cmake_minimum_required(VERSION 3.28)

project(bacons-helper-obs VERSION 0.3.0 LANGUAGES CXX)

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

find_package(libobs CONFIG REQUIRED)
find_package(obs-frontend-api CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_library(bacons-helper MODULE
    src/plugin-main.cpp
    src/bacons-helper-dock.cpp
    src/bacons-helper-dock.hpp
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

# OBS treats plugin MODULE libraries as LIBRARY artifacts on all platforms,
# including Windows. Keep platform-specific install layouts so a staged
# prefix can be copied directly into an OBS installation.
if(WIN32)
    install(TARGETS bacons-helper LIBRARY DESTINATION obs-plugins/64bit)
    install(DIRECTORY data/ DESTINATION data/obs-plugins/bacons-helper)
elseif(APPLE)
    install(TARGETS bacons-helper LIBRARY DESTINATION obs-plugins)
    install(DIRECTORY data/ DESTINATION share/obs/obs-plugins/bacons-helper)
else()
    include(GNUInstallDirs)
    install(TARGETS bacons-helper LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins)
    install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/bacons-helper)
endif()
