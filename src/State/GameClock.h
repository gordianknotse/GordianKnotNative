#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

namespace GK::GameClock {
    // Session clock counting seconds of UNPAUSED gameplay: menus, the console,
    // and anything else that pauses the game stop it. Ticked once per frame from
    // the overlay's present hook (the plugin's only per-frame callback) -- if
    // that hook failed to install, this clock does not advance.
    //
    // Monotonic within a session, meaningless across sessions: persistence code
    // must store durations relative to Now() and rebase on load.
    inline std::atomic<double> g_seconds{0.0};

    [[nodiscard]] inline double Now() { return g_seconds.load(std::memory_order_relaxed); }

    // Advance the clock by the real time since the previous frame, unless the
    // game is paused. A single frame's delta is capped so loading hitches and
    // alt-tab stalls (where the pause flag may not cover the whole gap) can't
    // jump the clock.
    inline void Tick() {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - last).count();
        last = now;
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->GameIsPaused()) {
            return;
        }
        g_seconds.fetch_add(std::min(delta, 1.0), std::memory_order_relaxed);
    }
}
