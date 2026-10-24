cmake_minimum_required(VERSION 3.16)
project(b6-themes-wayfire VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRAW REQUIRED IMPORTED_TARGET cairo pangocairo)

# the frame drawing: cairo and pango only
add_library(b6core STATIC src/XfwmTheme.cpp src/FrameRenderer.cpp)
target_include_directories(b6core PUBLIC src)
target_link_libraries(b6core PUBLIC PkgConfig::DRAW)

# draws frames to a PNG, for looking at a theme without a compositor
add_executable(b6-preview tools/preview.cpp)
target_link_libraries(b6-preview PRIVATE b6core)
