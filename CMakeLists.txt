cmake_minimum_required(VERSION 3.28)

project(bacons-helper-obs VERSION 0.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(libobs CONFIG REQUIRED)
find_package(obs-frontend-api CONFIG REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets Network)

# Package the TLS backend from the Qt SDK that CMake actually selected.
get_filename_component(bh_qt_package_dir "${Qt6_CONFIG}" DIRECTORY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bh-qt-package-dir.txt" "${bh_qt_package_dir}\n")

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

option(BH_BUILD_TESTS "Build OBS Companion dock tests" OFF)
if(BH_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(bacons-helper-dock-tests
        tests/dock-tests.cpp
        src/bacons-helper-dock.cpp
        src/bacons-helper-dock.hpp
        src/plugin-main.cpp
    )
    target_include_directories(bacons-helper-dock-tests PRIVATE src)
    target_link_libraries(bacons-helper-dock-tests PRIVATE
        OBS::libobs OBS::obs-frontend-api Qt6::Widgets Qt6::Network Qt6::Test
    )
    target_compile_definitions(bacons-helper-dock-tests PRIVATE
        BH_OBS_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS=1
    )
    if(WIN32)
        target_link_libraries(bacons-helper-dock-tests PRIVATE Crypt32)
    endif()
    add_test(NAME dock-tests COMMAND bacons-helper-dock-tests)
    set_tests_properties(dock-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
