#pragma once

namespace GK::UI {
    // Installs the in-game debug overlay (Dear ImGui over a D3D11 present hook),
    // toggled with Ctrl+Shift+G. Read-only inspector over GameState: actors (roles /
    // status / alias / live load state), labyrinth resources, the GkNpc alias pool,
    // and session config -- plus safe actions (e.g. re-running the scan).
    //
    // Call once from SKSEPluginLoad (the hooks must be written before the game
    // initializes D3D). No-op on VR runtimes.
    void Install();
}
