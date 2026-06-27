# Gordian Knot — Native (SKSE / CommonLibSSE-NG)

Native C++ layer for the **Gordian Knot** Skyrim SE/AE mod. Builds to
`GordianKnot.dll`, a runtime-agnostic SKSE plugin (SE 1.5.x / AE 1.6.x / GOG / VR via
Address Library). The Papyrus / Creation Kit side (`GordianKnot.esp`,
`Scripts/Source/*.psc`, `GordianKnot.ppj`) lives in a **separate repository**; this repo
is native-only.

Native owns: equip detection, combat events, SKSE co-save serialization.
Papyrus keeps: quest orchestration, alias pool, MCM, stage flow. The two sides talk over a
`GKNative` Papyrus function surface plus a mod-event vocabulary.

---

## Requirements

You need four things: a C++ toolchain (MSVC), CMake + Ninja, a bootstrapped **vcpkg**, and
Git. Everything else (CommonLibSSE-NG, fmt, spdlog, …) is pulled automatically by vcpkg on
first configure.

### 1. Visual Studio Build Tools (MSVC)

Install **Visual Studio Build Tools 2022 or newer** (this project is developed against
Build Tools 2026 / MSVC 14.51) with the **Desktop development with C++** workload, which
includes the MSVC compiler and the Windows SDK. The full Visual Studio Community edition
works too.

> This project builds with a very new MSVC. The vcpkg baseline is pinned accordingly — see
> *Troubleshooting* if you hit an `stdext` / `fmt` compile error.

### 2. CMake ≥ 3.21 and Ninja

You don't need standalone installs — both **CLion** and the **VS Build Tools** bundle a
recent CMake and Ninja:

- VS Build Tools: `…\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  and `…\Ninja\ninja.exe`.
- CLion: `…\CLion <ver>\bin\cmake\…` and `…\bin\ninja\…`.

Standalone installs are fine too if you prefer them on PATH.

### 3. vcpkg (bootstrapped) + `VCPKG_ROOT`

The build uses vcpkg in **manifest mode**. You only need to clone and bootstrap it once;
CMake invokes it for you (do **not** run `vcpkg install` by hand).

Clone it somewhere stable (this project assumes `C:\dev\vcpkg`) and bootstrap:

```cmd
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
```

Then set the **`VCPKG_ROOT`** environment variable to that folder (the CMake presets read
`$env{VCPKG_ROOT}`). `setx` writes it permanently to your **user** environment:

```cmd
setx VCPKG_ROOT "C:\dev\vcpkg"
```

> `setx` only affects **new** processes — open a fresh terminal (and restart CLion) so it
> picks up the variable. Verify with `echo %VCPKG_ROOT%`.
>
> Caveat: the **VS Developer Command Prompt** overrides `VCPKG_ROOT` with its own bundled
> vcpkg. Build from a plain shell, or rely on the CMake presets / CLion's Visual Studio
> toolchain, so this standalone clone is the one used.

The exact dependency versions are pinned by the registries/baselines in
`vcpkg-configuration.json`, so any recently bootstrapped vcpkg works — its own port
checkout doesn't matter.

### 4. Git

Any recent Git for Windows.

---

## Building in CLion (step by step)

CLion opens a CMake project directly — there is no separate "import" step.

1. **Open the folder.** *File → Open* → select this repo's root → *Open as Project*. CLion
   detects `CMakeLists.txt` and `CMakePresets.json`.
2. **Enable the presets.** When prompted (or under *Settings → Build, Execution, Deployment
   → CMake*), enable the **release** and **debug** presets.
3. **Add a Visual Studio toolchain.** *Settings → Build, Execution, Deployment →
   Toolchains* → **+ → Visual Studio**. Set the toolset path to your Build Tools install
   (e.g. `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`) and architecture
   **amd64**. (Leave any other toolchains, e.g. an ESP-IDF "System" one, in place — the
   toolchain list is global and shared across all your projects.)
4. **Point the presets at it.** Back in *Settings → CMake*, for the **release** (and
   **debug**) profile set **Toolchain → Visual Studio**. This is stored per-project, so it
   won't affect your other projects.
5. **Leave the rest to the preset.** Generator, build type, compiler, toolchain file and
   triplet all come from `--preset`. Leave CLion's **Generator** on *Let CMake decide* and
   don't re-specify those — the only thing CLion must supply is the Visual Studio
   *toolchain* (the MSVC environment that puts `cl.exe` on PATH).
6. **Ensure `VCPKG_ROOT` is set** (see Requirements). If CLion was open before you ran
   `setx`, restart it so it inherits the variable.
7. **Reload & build.** *Tools → CMake → Reset Cache and Reload Project*. The first
   configure compiles CommonLibSSE-NG via vcpkg (~2 min; cached afterward). Then pick the
   **release** profile + **GordianKnot** target and **Build** (Ctrl+F9).

A successful configure shows `The CXX compiler identification is MSVC 19.51.x` →
`Configuring done`. If it instead says the compiler is *unknown* / `cl.exe ... not found`,
the active toolchain isn't Visual Studio (step 3–4).

## Building from the command line

From a shell where `cl.exe` is available (VS Developer prompt or after calling
`vcvars64.bat`), with `VCPKG_ROOT` set:

```sh
cmake --preset release
cmake --build --preset release
```

---

## Output & deploying to the game (MO2)

Each build produces `build/<preset>/GordianKnot.dll` and stages it into the repo root
laid out as a Skyrim mod:

```
SKSE/Plugins/GordianKnot.dll
```

The **repo root itself is the MO2 mod** — register it in MO2's mods folder (e.g. a
directory junction so this repo stays the source of truth):

```cmd
mklink /J "C:\path\to\MO2\mods\GordianKnotNative" "C:\games\mods\Gordian Knot Native"
```

Reopen MO2, press **F5**, enable **GordianKnotNative**, and launch **SKSE through MO2**.
Confirm load in `Documents\My Games\Skyrim Special Edition\SKSE\GordianKnot.log` —
look for `GordianKnot native plugin loaded.`

> Trade-off: with the repo root as the mod, MO2's VFS overlays the whole repo (including
> `build/` and `.git/`) onto the game's Data. It works but is heavy; if MO2 gets sluggish,
> point the MO2 mod at a dedicated subfolder instead and set `OUTPUT_FOLDER` (in
> `CMakeLists.txt`) to match.

Prerequisites on the MO2 side (independent of this plugin): **SKSE** and **Address Library
for SKSE Plugins** installed and enabled in that profile (this is an Address-Library
plugin).

Alternatively, set `SKYRIM_MODS_FOLDER` (MO2/Vortex mods dir) or `SKYRIM_FOLDER` (game
root) before configuring and the build copies straight there instead.

---

## Viewing logs

The plugin logs to `Documents\My Games\Skyrim Special Edition\SKSE\GordianKnot.log`
(under OneDrive if your Documents are redirected). This is **separate** from the Papyrus
log — it's an spdlog file sink, and `SkyrimSE.exe` has no console, so nothing streams by
default. Two convenient ways to watch it:

**Tail it into a CLion console (one click).** `tools/tail-log.ps1` waits for the log to
appear and then follows it. Wire it up once:

- *Run → Edit Configurations → + → Shell Script*
- Name: `Tail GordianKnot.log`
- Execute: *Script file* → `tools/tail-log.ps1`
- Interpreter: `powershell.exe`, options `-NoProfile -ExecutionPolicy Bypass -File`

Click ▶ and log lines stream into the Run window. (The log is truncated each game launch,
so launch the game first, then start/restart the tail.)

**Debugger / DebugView (Debug builds only).** Debug builds also emit every line via
`OutputDebugString`. Attach CLion to `SkyrimSE.exe` (*Run → Attach to Process*) and the log
appears live in the debugger console, or run Sysinternals **DebugView** to watch it without
attaching. (Release builds omit this sink.)

---

## Project layout

| Path                       | Purpose                                                            |
| -------------------------- | ----------------------------------------------------------------- |
| `CMakeLists.txt`           | `add_commonlibsse_plugin` target + `dist/` auto-deploy.           |
| `CMakePresets.json`        | `debug` / `release` presets (Ninja, MSVC, `x64-windows-static`).  |
| `vcpkg.json`               | Manifest: depends on `commonlibsse-ng`.                           |
| `vcpkg-configuration.json` | Registries: microsoft default + colorglass (CommonLibSSE-NG).     |
| `src/Plugin.cpp`           | SKSE entry point (`SKSEPluginLoad`) + messaging listener.         |
| `src/Log.h`                | `SetupLog()` — file logger to `SKSE/GordianKnot.log`.             |
| `src/PCH.h`                | Precompiled header (`RE/Skyrim.h`, `SKSE/SKSE.h`, spdlog).        |
| `src/Events/`              | *(reserved)* TESEquipEvent / TESCombatEvent sinks.               |
| `src/Serialization/`       | *(reserved)* SKSE co-save serialization.                          |
| `src/Papyrus/`             | *(reserved)* GKNative function surface + mod events.              |
| `.github/workflows/`       | CI build with vcpkg GitHub Actions binary caching.                |

---

## Troubleshooting

- **`error C2653: 'stdext' is not a class or namespace name`** (building `fmt`): the vcpkg
  `default-registry` baseline is too old for your MSVC. This project already pins a recent
  baseline in `vcpkg-configuration.json`; if you regenerated config from a template, bump
  the microsoft baseline to a current commit (don't downgrade the compiler).
- **`CMAKE_CXX_COMPILER: cl.exe is not a full path and was not found in PATH`**: the active
  toolchain isn't Visual Studio, so the MSVC environment wasn't applied. Fix the CLion
  toolchain (setup steps 3–4) or call `vcvars64.bat` first on the command line.
- **`VCPKG_ROOT` empty / vcpkg toolchain file not found**: set it via `setx` and restart
  the shell/IDE.
