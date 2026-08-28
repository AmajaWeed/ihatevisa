cmake_minimum_required(VERSION 3.21)
project(iHateVisa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/little-cms2")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/libzip")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network)
find_package(ZLIB REQUIRED)

# macOS/Linux: Homebrew ships lcms2/libzip with pkg-config files, no
# CMake config. Windows CI has no pkg-config at all (and no lcms2/libzip
# preinstalled either) — it goes through vcpkg instead (preinstalled on
# GitHub's windows-latest runners, driven by vcpkg.json in manifest mode).
if(WIN32)
    find_package(lcms2 CONFIG REQUIRED)
    find_package(libzip CONFIG REQUIRED)
    set(LCMS2_TARGET lcms2::lcms2)
    set(LIBZIP_TARGET libzip::zip)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
    pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
    set(LCMS2_TARGET PkgConfig::LCMS2)
    set(LIBZIP_TARGET PkgConfig::LIBZIP)
endif()

set(SOURCES
    src/App/main.cpp
    src/App/MainWindow.cpp
    src/App/EditorWidget.cpp
    src/Core/EditorRenderer.cpp
    src/Core/PrintComposer.cpp
    src/Imaging/Adjustments.cpp
    src/Imaging/BackgroundTools.cpp
    src/Imaging/CmykPipeline.cpp
    src/Pdf/PdfObjectWriter.cpp
    src/Pdf/CmykPdfWriter.cpp
    src/Project/HateFile.cpp
    src/Update/UpdateChecker.cpp
    src/Update/UpdateInstaller.cpp
    src/Update/UpdateToast.cpp
    assets/resources.qrc
)

add_executable(iHateVisa MACOSX_BUNDLE ${SOURCES})

target_include_directories(iHateVisa PRIVATE src)
target_compile_definitions(iHateVisa PRIVATE IHATEVISA_VERSION="1.3.1")

target_link_libraries(iHateVisa PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network
    ${LCMS2_TARGET}
    ${LIBZIP_TARGET}
    ZLIB::ZLIB
)

if(WIN32)
    # Phase: Windows-only self-update file-replace shim (uses winspool-style
    # APIs elsewhere already handled via QPrinter; the update apply script
    # itself is a generated .ps1, not compiled code).
endif()

if(APPLE)
    # macOS-only: ad-hoc re-codesign after a self-update file swap is done
    # via /usr/bin/codesign at runtime (Update/UpdateInstaller.cpp), no
    # extra compiled sources needed.
endif()
