# CLAUDE.md — Gordian Knot (Native)

Context for AI assistants working in this repo. Keep it current when architecture or build setup changes.

## What this is

Native C++ (SKSE / CommonLibSSE-NG) layer for the **Gordian Knot** Skyrim SE/AE mod.
Builds to **`GordianKnot.dll`**, a runtime-agnostic SKSE plugin (SE 1.5.x / AE 1.6.x /
GOG / VR via Address Library).

This repo is **native-only**. The Papyrus / Creation Kit side (`GordianKnot.esp`,
`Scripts/Source/*.psc`, `GordianKnot.ppj`) lives in a **separate repository**. Do not
expect Papyrus assets here.

## Hybrid architecture (division of responsibility)

- **Native (this repo) owns:** equip detection, combat events, SKSE co-save
  serialization. Performance- and reliability-critical, event-driven work.
- **Papyrus (other repo) keeps:** quest orchestration, the alias pool, MCM, stage flow.
- **Contract between them:** a `GKNative` Papyrus function surface (native functions
  exposed to Papyrus) plus a mod-event vocabulary (`SendModEvent`) emitted native→Papyrus.

Script/asset prefix is **`GK`** (e.g. `GKNative`, authored NPCs `GkLabWarden01-04`,
`GkWanderer01-04`).

## Tech stack

- **CommonLibSSE-NG** via the **colorglass** vcpkg registry (`commonlibsse-ng`, *not*
  Monitor's `commonlibsse-ng-fork`). CMake helper `add_commonlibsse_plugin`.
- **vcpkg** manifest mode (`vcpkg.json` + `vcpkg-configuration.json`). CMake runs the
  install automatically; do **not** `vcpkg install` by hand.
- **CMake presets** (`debug`/`release`), **Ninja** generator, **MSVC** (`cl.exe`).
- Triplet **`x64-windows-static`** (static CRT `/MT` → self-contained DLL, no UCRT dep).
- **C++23**, PCH at `src/PCH.h`.

## Build

Presets do everything; the generator/compiler/triplet/toolchain-file all come from
`CMakePresets.json`.

```sh
cmake --preset release        # configures (first run compiles deps via vcpkg, ~2 min)
cmake --build --preset release
```

- Output: `build/<preset>/GordianKnot.dll`.
- A `POST_BUILD` step stages it into **`<repo>/SKSE/Plugins/`** — the repo root is itself
  the MO2 mod. Override with env vars `SKYRIM_MODS_FOLDER` (→ `<mods>/GordianKnotNative`)
  or `SKYRIM_FOLDER` (→ `<game>/Data`).
- `cl.exe` must be on PATH — i.e. run inside an MSVC env (`vcvars64.bat`), or let CLion's
  **Visual Studio toolchain** provide it. See README for CLion setup.
- `VCPKG_ROOT` must point at a bootstrapped vcpkg clone (presets read `$env{VCPKG_ROOT}`).

## Critical gotcha: vcpkg baseline vs MSVC version

`vcpkg-configuration.json` `default-registry` baseline is intentionally bumped to a
**recent** microsoft/vcpkg commit (`c8f45a06…`). The CommonLibSSE-NG template's stock
baseline (`cc288af7…`, ~2023) serves **fmt 9.1.0**, which uses
`stdext::checked_array_iterator` — **removed from modern MSVC STL** (this project builds
with MSVC 14.51 / VS Build Tools 2026). Symptom: `error C2653: 'stdext' is not a class or
namespace name`. **Fix is to bump the microsoft baseline, never to downgrade the
compiler.** Leave the colorglass `commonlibsse-ng` baseline alone.

## File layout

```
CMakeLists.txt              add_commonlibsse_plugin target + dist/ auto-deploy
CMakePresets.json           debug/release · Ninja · cl.exe · x64-windows-static
vcpkg.json                  manifest: depends on commonlibsse-ng
vcpkg-configuration.json    registries: microsoft (bumped baseline) + colorglass
src/Plugin.cpp              SKSEPluginLoad entry + SKSE messaging listener
src/Log.h                   SetupLog() -> SKSE/GordianKnot.log (spdlog)
src/PCH.h                   RE/Skyrim.h, SKSE/SKSE.h, spdlog; `logger` alias
src/Events/      (reserved) TESEquipEvent / TESCombatEvent sinks
src/Serialization/(reserved) SKSE co-save (records, version codes, containers)
src/Papyrus/     (reserved) GKNative function surface + mod-event vocabulary
.github/workflows/build.yml CI: MSVC + CMake/Ninja, vcpkg GHA binary cache
```

Add new `.cpp` files to the `SOURCES` list in `CMakeLists.txt` (explicit list — the
CommonLibSSE-NG-recommended form, not a glob).

## Roadmap (not yet implemented — don't build ahead of asks)

1. **`src/Events/`** — `TESEquipEvent` sink → emits a mod event (replaces unreliable
   Papyrus `OnEquipped`). `TESCombatEvent` sink → serial main-thread dispatch via the SKSE
   task interface, frame-paced (replaces a Papyrus polling-mutex). Pattern:
   `RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESEquipEvent>(sink)`
   with a `BSTEventSink<T>::ProcessEvent` singleton.
2. **`src/Serialization/`** — SKSE serialization interface (Save/Load/Revert), replacing
   StorageUtil. Per-NPC and per-door state in STL containers. Field-by-field, per-record
   type codes + version numbers, **additive-only** schema. Logical enum keys for authored
   NPCs (wardens/wanderers); FormID resolution only for dynamic/cross-mod forms. Revert
   callback clears containers.
3. **`src/Papyrus/`** — `GKNative` native function registration (SKSE PapyrusInterface) +
   the mod-event vocabulary.

## Conventions

- Match surrounding style; `.clang-format` is authoritative (LLVM-based, 120 col, 4-space).
- Verify RE:: / SKSE:: symbol spellings against the installed CommonLibSSE-NG headers —
  they drift between forks. Don't guess.
- `build/`, `dist/`, `out/`, `install/`, `vcpkg_installed/`, `.claude/` are gitignored —
  never commit build trees or the packaged mod.
