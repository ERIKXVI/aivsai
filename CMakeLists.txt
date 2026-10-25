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
option(PM_BUILD_TOOLS "Build developer tools (level and audio preview)" ON)
option(PM_SANITIZE "Build with address/undefined sanitizers (GCC/Clang)" OFF)
option(PM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project targets (CI)" OFF)

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
file(GLOB PM_SHADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/shaders/*.vs" "${PROJECT_SOURCE_DIR}/shaders/*.fs")
set(PM_GEN_DIR "${PROJECT_BINARY_DIR}/generated")
set(PM_EMBED_CPP "${PM_GEN_DIR}/EmbeddedShaders.cpp")
set(PM_EMBED_HPP "${PM_GEN_DIR}/EmbeddedShaders.hpp")
add_custom_command(
    OUTPUT "${PM_EMBED_CPP}" "${PM_EMBED_HPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PM_GEN_DIR}"
    COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT=${PM_EMBED_CPP}" "-DHEADER=${PM_EMBED_HPP}" "-DINPUTS=${PM_SHADERS}"
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS ${PM_SHADERS} "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    COMMENT "Embedding shaders"
    VERBATIM)

# ---------------------------------------------------------------------------
# Warning policy, linked PRIVATE into every first-party target (never raylib or GLFW)
# ---------------------------------------------------------------------------
add_library(pm_warnings INTERFACE)
if(MSVC)
    target_compile_options(pm_warnings INTERFACE /W4 /permissive- /utf-8 /wd4100 /wd4244 /wd4267 /wd4305
        $<$<BOOL:${PM_WARNINGS_AS_ERRORS}>:/WX>)
    target_compile_definitions(pm_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(pm_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
        $<$<BOOL:${PM_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# ---------------------------------------------------------------------------
# Game library (everything except the entry point, so tests can link it)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PM_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM PM_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")

add_library(pm_game STATIC ${PM_SOURCES} "${PM_EMBED_CPP}")
target_include_directories(pm_game PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PM_GEN_DIR}")
target_link_libraries(pm_game PUBLIC raylib PRIVATE pm_warnings)
# GLFW's header (raylib bundles GLFW) for the click latch hook.
target_include_directories(pm_game PRIVATE "${raylib_SOURCE_DIR}/src/external/glfw/include")
target_compile_definitions(pm_game PUBLIC
    PM_VERSION_STRING="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

find_package(Threads REQUIRED)
target_link_libraries(pm_game PUBLIC Threads::Threads)

# Release optimisation comes from CMake's own per-config flags (/O2, -O3); these only add to it.
if(MSVC)
    # /O2 already implies /Oi; add whole-program optimisation and fast FP.
    target_compile_options(pm_game PUBLIC $<$<CONFIG:Release>:/GL /fp:fast>)
    target_link_options(pm_game PUBLIC $<$<CONFIG:Release>:/LTCG>)
elseif(MINGW)
    # Every executable (game, tests, tools) links the runtime statically: no DLLs to ship.
    target_link_options(pm_game INTERFACE -static -static-libgcc -static-libstdc++)
endif()

if(PM_SANITIZE AND NOT MSVC)
    # Make UBSan findings fatal: by default it only logs and exits 0, so ctest would pass.
    target_compile_options(pm_game PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(pm_game PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
set(PM_EXE_SOURCES src/main.cpp)
if(WIN32)
    # Icon and VERSIONINFO, stamped from project(VERSION) (keep it MAJOR.MINOR.PATCH).
    configure_file("${PROJECT_SOURCE_DIR}/resources/app.rc.in" "${PM_GEN_DIR}/app.rc" @ONLY)
    list(APPEND PM_EXE_SOURCES "${PM_GEN_DIR}/app.rc")
endif()
add_executable(PrimaMateria WIN32 ${PM_EXE_SOURCES})
target_link_libraries(PrimaMateria PRIVATE pm_game pm_warnings)
if(MSVC)
    # Keep a standard main() while using the GUI subsystem (no console window).
    target_link_options(PrimaMateria PRIVATE /ENTRY:mainCRTStartup)
endif()
set_target_properties(PrimaMateria PROPERTIES
    OUTPUT_NAME "PrimaMateria"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PM_BUILD_TESTS)
    enable_testing()
    file(GLOB PM_TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp")
    add_executable(pm_tests ${PM_TEST_SOURCES})
    target_link_libraries(pm_tests PRIVATE pm_game pm_warnings)
    target_include_directories(pm_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME pm_tests COMMAND pm_tests)
    # A hang (a deadlocked pool, a generator that never settles) fails the run instead of stalling CI.
    set_tests_properties(pm_tests PROPERTIES TIMEOUT 600)
endif()

# ---------------------------------------------------------------------------
# Developer tools
# ---------------------------------------------------------------------------
if(PM_BUILD_TOOLS)
    add_executable(pm_genpreview tools/genpreview.cpp)
    target_link_libraries(pm_genpreview PRIVATE pm_game pm_warnings)
    add_executable(pm_audiopreview tools/audiopreview.cpp)
    target_link_libraries(pm_audiopreview PRIVATE pm_game pm_warnings)
endif()
