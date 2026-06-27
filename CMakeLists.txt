# Gordian Knot — native SKSE plugin (CommonLibSSE-NG)
#
# Produces GordianKnot.dll, a runtime-agnostic (SE / AE / GOG / VR) SKSE plugin
# via Address Library. The C++ layer handles equip/combat events and co-save
# serialization; Papyrus keeps quest/MCM/alias orchestration.

cmake_minimum_required(VERSION 3.21)

# Project name == output .dll name (GordianKnot.dll).
project(GordianKnot VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Auto-deploy. By DEFAULT every build stages the plugin into <repo>/dist,
# laid out exactly like a Skyrim mod (dist/SKSE/Plugins/GordianKnot.dll). This
# makes the repo itself a ready-to-use MO2 mod: link dist/ into MO2 as a mod via
# a directory junction (run once, as Administrator if needed):
#
#     mklink /J "C:\path\to\MO2\mods\GordianKnotNative" "C:\games\mods\Gordian Knot Native\dist"
#
# Then enable "GordianKnotNative" in MO2 and launch SKSE through MO2.
#
# Optional overrides (set the env var to deploy elsewhere instead of dist/):
#   SKYRIM_MODS_FOLDER -> <mods>/GordianKnotNative   (deploy straight into MO2/Vortex)
#   SKYRIM_FOLDER      -> <game>/Data                (deploy straight into the game)
# ---------------------------------------------------------------------------
set(OUTPUT_FOLDER "${CMAKE_SOURCE_DIR}/dist")

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/GordianKnotNative")
endif()
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

# ---------------------------------------------------------------------------
# CommonLibSSE-NG plugin target.
# add_commonlibsse_plugin() (from the commonlibsse-ng vcpkg port) creates the
# shared library, links CommonLibSSE/SKSE, and auto-generates the SKSE plugin
# version data (name/version taken from project() above).
# ---------------------------------------------------------------------------
find_package(CommonLibSSE CONFIG REQUIRED)

# Source list. Add new .cpp files here as the native layer grows. Headers are
# listed so IDEs (CLion / VS) surface them in the project tree.
set(SOURCES
    src/Plugin.cpp

    src/Log.h
    src/PCH.h
)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${SOURCES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE src)

# ---------------------------------------------------------------------------
# Auto-deploy copy step (only if OUTPUT_FOLDER resolved above).
# ---------------------------------------------------------------------------
if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()
