cmake_minimum_required(VERSION 3.21)

project(PrimaMateria
    VERSION 1.0.0
    DESCRIPTION "A falling-sand alchemy roguelite"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Ship a self-contained executable on Windows: no VC++ redistributable required.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(PM_BUILD_TESTS "Build the unit test executable" ON)
option(PM_SANITIZE "Build with address/undefined sanitizers (GCC/Clang)" OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RMODELS OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_GIF OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(raylib)

# ---------------------------------------------------------------------------
# Embedded shaders
# ---------------------------------------------------------------------------
file(GLOB PM_SHADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/*.vs" "${CMAKE_SOURCE_DIR}/shaders/*.fs")
set(PM_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
set(PM_EMBED_CPP "${PM_GEN_DIR}/EmbeddedShaders.cpp")
set(PM_EMBED_HPP "${PM_GEN_DIR}/EmbeddedShaders.hpp")
add_custom_command(
    OUTPUT "${PM_EMBED_CPP}" "${PM_EMBED_HPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PM_GEN_DIR}"
    COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT=${PM_EMBED_CPP}" "-DHEADER=${PM_EMBED_HPP}" "-DINPUTS=${PM_SHADERS}"
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS ${PM_SHADERS} "${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    COMMENT "Embedding shaders"
    VERBATIM)

# ---------------------------------------------------------------------------
# Game library (everything except the entry point, so tests can link it)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM PM_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_library(pm_game STATIC ${PM_SOURCES} "${PM_EMBED_CPP}")
target_include_directories(pm_game PUBLIC "${CMAKE_SOURCE_DIR}/src" "${PM_GEN_DIR}")
target_link_libraries(pm_game PUBLIC raylib)
# GLFW's header (raylib bundles GLFW) for the click latch hook.
target_include_directories(pm_game PRIVATE "${raylib_SOURCE_DIR}/src/external/glfw/include")
target_compile_definitions(pm_game PUBLIC
    PM_VERSION_STRING="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

find_package(Threads REQUIRED)
target_link_libraries(pm_game PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(pm_game PRIVATE /W4 /permissive- /utf-8 /wd4100 /wd4244 /wd4267 /wd4305)
    target_compile_definitions(pm_game PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pm_game PUBLIC $<$<CONFIG:Release>:/O2 /Oi /GL /fp:fast>)
    target_link_options(pm_game PUBLIC $<$<CONFIG:Release>:/LTCG>)
else()
    target_compile_options(pm_game PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    target_compile_options(pm_game PUBLIC $<$<CONFIG:Release>:-O2>)
endif()

if(PM_SANITIZE AND NOT MSVC)
    target_compile_options(pm_game PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pm_game PUBLIC -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
set(PM_EXE_SOURCES src/main.cpp)
if(WIN32)
    list(APPEND PM_EXE_SOURCES resources/app.rc)
endif()
add_executable(PrimaMateria WIN32 ${PM_EXE_SOURCES})
target_link_libraries(PrimaMateria PRIVATE pm_game)
if(MSVC)
    # Keep a standard main() while using the GUI subsystem (no console window).
    target_link_options(PrimaMateria PRIVATE /ENTRY:mainCRTStartup)
elseif(MINGW)
    # Every executable (game, tests, tools) links the runtime statically: no DLLs to ship.
    target_link_options(pm_game INTERFACE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(PrimaMateria PROPERTIES
    OUTPUT_NAME "PrimaMateria"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PM_BUILD_TESTS)
    enable_testing()
    file(GLOB PM_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
    add_executable(pm_tests ${PM_TEST_SOURCES})
    target_link_libraries(pm_tests PRIVATE pm_game)
    target_include_directories(pm_tests PRIVATE "${CMAKE_SOURCE_DIR}")
    add_test(NAME pm_tests COMMAND pm_tests)
endif()

# ---------------------------------------------------------------------------
# Developer tools
# ---------------------------------------------------------------------------
option(PM_BUILD_TOOLS "Build developer tools (level and audio preview)" ON)
if(PM_BUILD_TOOLS)
    add_executable(pm_genpreview tools/genpreview.cpp)
    target_link_libraries(pm_genpreview PRIVATE pm_game)
    add_executable(pm_audiopreview tools/audiopreview.cpp)
    target_link_libraries(pm_audiopreview PRIVATE pm_game)
endif()
