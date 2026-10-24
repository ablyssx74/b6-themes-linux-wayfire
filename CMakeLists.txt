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

# the Wayfire plugin: built when Wayfire's development files are installed
pkg_check_modules(WAYFIRE IMPORTED_TARGET wayfire wf-config)
if(WAYFIRE_FOUND)
    add_library(xfwm-decoration MODULE plugin/xfwm-decoration.cpp)
    target_compile_definitions(xfwm-decoration PRIVATE WLR_USE_UNSTABLE WAYFIRE_PLUGIN)
    target_link_libraries(xfwm-decoration PRIVATE b6core PkgConfig::WAYFIRE)
    set_target_properties(xfwm-decoration PROPERTIES PREFIX "lib")
    execute_process(COMMAND pkg-config --variable=plugindir wayfire OUTPUT_VARIABLE WAYFIRE_PLUGINDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND pkg-config --variable=metadatadir wayfire OUTPUT_VARIABLE WAYFIRE_METADATADIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    install(TARGETS xfwm-decoration DESTINATION "${WAYFIRE_PLUGINDIR}")
    install(FILES plugin/xfwm-decoration.xml DESTINATION "${WAYFIRE_METADATADIR}")
else()
    message(STATUS "Wayfire development files not found: the plugin is not built (the preview tool is)")
endif()
