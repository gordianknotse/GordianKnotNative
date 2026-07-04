# Gordian Knot — native SKSE plugin (CommonLibSSE-NG)
#
# Produces GordianKnot.dll, a runtime-agnostic (SE / AE / GOG / VR) SKSE plugin
# via Address Library. The C++ layer handles equip/combat events and co-save
# serialization; Papyrus keeps quest/MCM/alias orchestration.

cmake_minimum_required(VERSION 3.21)

# Project name == output .dll name (GordianKnot.dll).
project(GordianKnot VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Auto-deploy. By DEFAULT every build stages the plugin into the repo ROOT laid
# out as a Skyrim mod (<repo>/SKSE/Plugins/GordianKnot.dll), because the repo
# root itself is registered as the mod in MO2.
#
# NOTE: with the repo root as the MO2 mod, MO2's VFS overlays the ENTIRE repo
# (including build/ and .git/) onto the game's Data. It works, but it's heavy; if
# MO2 gets sluggish, point the MO2 mod at a clean subfolder instead and set
# OUTPUT_FOLDER to match.
#
# Optional overrides (set the env var to deploy elsewhere instead of the root):
#   SKYRIM_MODS_FOLDER -> <mods>/GordianKnotNative   (deploy straight into MO2/Vortex)
#   SKYRIM_FOLDER      -> <game>/Data                (deploy straight into the game)
# ---------------------------------------------------------------------------
set(OUTPUT_FOLDER "${CMAKE_SOURCE_DIR}")

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
find_package(imgui CONFIG REQUIRED)  # debug overlay (src/UI), Dear ImGui + DX11 backend

# Source list. Add new .cpp files here as the native layer grows. Headers are
# listed so IDEs (CLion / VS) surface them in the project tree.
set(SOURCES
    src/Plugin.cpp
    src/Events/EquipEventSink.cpp
    src/State/ActorRegistry.cpp
    src/State/AliasPool.cpp
    src/State/GameState.cpp
    src/State/Labyrinth.cpp
    src/State/QueueRegistry.cpp
    src/State/RoleFactions.cpp
    src/Papyrus/GKNative.cpp
    src/Serialization/Serialization.cpp
    src/UI/DebugOverlay.cpp

    src/Log.h
    src/PCH.h
    src/VmCall.h
    src/Events/EquipEventSink.h
    src/State/ActorState.h
    src/State/ActorRegistry.h
    src/State/AliasPool.h
    src/State/GameState.h
    src/State/Resource.h
    src/State/Cell.h
    src/State/Marker.h
    src/State/Furniture.h
    src/State/ResourceRegistry.h
    src/State/Labyrinth.h
    src/State/QueueRegistry.h
    src/State/RoleFactions.h
    src/Papyrus/GKNative.h
    src/Serialization/Records.h
    src/Serialization/Serialization.h
    src/UI/DebugOverlay.h
)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${SOURCES})

# Debug overlay: Dear ImGui rendered off a D3D11 present hook. d3d11/d3dcompiler
# come from the Windows SDK (the ImGui DX11 backend compiles its shaders at init).
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui d3d11 d3dcompiler)

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
