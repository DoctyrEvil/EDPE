# Builds edpe_debug.dll against the EDPE tree under test.
#   cmake -S tools/edpe_debug/module -B <build> -A x64 -DEDPE_TREE=<worktree>
cmake_minimum_required(VERSION 3.25)
project(edpe_debug LANGUAGES CXX)

set(EDPE_TREE "" CACHE PATH "EDPE source tree whose external/imgui the module must match")
if(NOT EXISTS "${EDPE_TREE}/external/imgui/imgui.cpp")
    message(FATAL_ERROR "EDPE_TREE must point at an EDPE tree with external/imgui")
endif()

set(IMGUI "${EDPE_TREE}/external/imgui")
add_library(edpe_debug SHARED
    edpe_debug.cpp
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp)
target_compile_features(edpe_debug PRIVATE cxx_std_20)
target_compile_definitions(edpe_debug PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(edpe_debug PRIVATE ${IMGUI} ${CMAKE_CURRENT_SOURCE_DIR}/../include)
