#pragma once

#include "State/ActorRegistry.h"

#include <mutex>

namespace GK {
    // Central owner of all native runtime state. A single recursive mutex guards
    // every registry, since Papyrus VM threads, the SKSE serialization thread, and
    // event sinks all touch this state.
    //
    // Usage from a Papyrus binding or callback:
    //     auto* state = GK::GameState::GetSingleton();
    //     auto  lock  = state->Lock();
    //     state->Actors().SetStatus(id, status);
    //
    // Hold a single Lock() across compound operations that must be atomic (e.g. read
    // a registry, decide, then mutate another).
    class GameState {
    public:
        [[nodiscard]] static GameState* GetSingleton();

        [[nodiscard]] std::unique_lock<std::recursive_mutex> Lock() { return std::unique_lock(_mutex); }

        [[nodiscard]] ActorRegistry& Actors() { return _actors; }

        // Wipes all state. Called from the SKSE Revert callback (Phase 2) before a
        // save is loaded, and reusable for a clean slate.
        void Reset();

        GameState(const GameState&) = delete;
        GameState(GameState&&) = delete;
        GameState& operator=(const GameState&) = delete;
        GameState& operator=(GameState&&) = delete;

    private:
        GameState() = default;
        ~GameState() = default;

        std::recursive_mutex _mutex;
        ActorRegistry _actors;
    };
}
