# Gordian Knot — Native (SKSE / CommonLibSSE-NG)

Native C++ layer for the **Gordian Knot** Skyrim SE/AE mod. Builds to
`GordianKnot.dll`, a runtime-agnostic SKSE plugin (SE 1.5.x / AE 1.6.x / GOG / VR
via Address Library). The Papyrus / Creation Kit side (`GordianKnot.esp`,
`Scripts/Source/*.psc`, `GordianKnot.ppj`) lives in a separate repository; this
repo is native-only.

Native owns: equip detection, combat events, SKSE co-save serialization.
Papyrus keeps: quest orchestration, alias pool, MCM, stage flow. The two sides
talk over a `GKNative` Papyrus function surface plus a mod-event vocabulary.

## Layout

| Path                       | Purpose                                                            |
| -------------------------- | ----------------------------------------------------------------- |
| `CMakeLists.txt`           | Build definition; `add_commonlibsse_plugin` target + auto-deploy. |
| `CMakePresets.json`        | `debug` / `release` presets (Ninja, MSVC, `x64-windows-static`).  |
| `vcpkg.json`               | Manifest: depends on `commonlibsse-ng`.                           |
| `vcpkg-configuration.json` | Registries: microsoft default + colorglass (CommonLibSSE-NG).     |
| `src/Plugin.cpp`           | SKSE entry point (`SKSEPluginLoad`) + messaging listener.         |
| `src/Log.h`                | `SetupLog()` — file logger to `SKSE/GordianKnot.log`.             |
| `src/PCH.h`                | Precompiled header (`RE/Skyrim.h`, `SKSE/SKSE.h`, spdlog).        |
| `src/Events/`              | *(reserved)* TESEquipEvent / TESCombatEvent sinks.               |
| `src/Serialization/`       | *(reserved)* SKSE co-save serialization.                          |
| `src/Papyrus/`             | *(reserved)* GKNative function surface + mod events.              |
| `.github/workflows/`       | CI build with vcpkg GHA binary caching.                           |

## Building

Requires: VS Build Tools (MSVC), CMake ≥ 3.21, Ninja, a bootstrapped vcpkg with
`VCPKG_ROOT` set. Build from a shell where `cl.exe` is available (VS Dev prompt
or `vcvars64`), or open the folder in CLion / VS and pick a preset.

```sh
cmake --preset release
cmake --build --preset release
```

Output: `build/release/GordianKnot.dll`. Set `SKYRIM_MODS_FOLDER` (MO2/Vortex
mods dir) or `SKYRIM_FOLDER` (game root) to auto-copy the DLL to
`<folder>/SKSE/Plugins` after each build.

> First configure compiles CommonLibSSE-NG from source via vcpkg — expect several
> minutes. Subsequent builds use the vcpkg binary cache.
