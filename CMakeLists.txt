cmake_minimum_required(VERSION 3.21)
project(iHateVisa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

set(SOURCES
    src/App/main.cpp
    src/App/MainWindow.cpp
    src/App/EditorWidget.cpp
    src/Core/EditorRenderer.cpp
    src/Core/PrintComposer.cpp
    src/Imaging/Adjustments.cpp
    src/Imaging/BackgroundTools.cpp
)

add_executable(iHateVisa MACOSX_BUNDLE ${SOURCES})

target_include_directories(iHateVisa PRIVATE src)

target_link_libraries(iHateVisa PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
)
