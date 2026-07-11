#pragma once

#include "State/ActorRegistry.h"
#include "State/AnimationRegistry.h"
#include "State/AttributeRegistry.h"
#include "State/Labyrinth.h"
#include "State/QueueRegistry.h"
#include "State/ResourceRegistry.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

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
        // Session config like _keywords: never serialized, not wiped by Reset
        // (Papyrus re-adds its animations after each load; Add is idempotent).
        [[nodiscard]] AnimationRegistry& Animations() { return _animations; }
        [[nodiscard]] AttributeRegistry& Attributes() { return _attributes; }
        [[nodiscard]] LabyrinthRegistry& Labyrinths() { return _labyrinths; }
        [[nodiscard]] QueueRegistry& Queues() { return _queues; }
        [[nodiscard]] ResourceRegistry& Resources() { return _resources; }
        [[nodiscard]] ResourceKeywords& Keywords() { return _keywords; }

        // The quest carrying the GkNpc alias pool. Session config like _keywords (a live
        // form pointer re-supplied by Papyrus each load); never serialized, not Reset.
        [[nodiscard]] RE::TESQuest*& AliasQuest() { return _aliasQuest; }

        // One pending alias-pool slot: who reserved it and until when.
        struct AliasReservation {
            std::chrono::steady_clock::time_point expiry;
            RE::FormID actor = 0;  // lets hooks find the slot before the fill lands
        };

        // Transient alias-pool reservations: (quest FormID << 32 | alias ID) -> holder.
        // Guards the reserve->ForceRefTo window so two Papyrus threads aren't handed
        // the same free alias. Never serialized; entries expire or are consumed on fill.
        [[nodiscard]] std::unordered_map<std::uint64_t, AliasReservation>& AliasReservations() {
            return _aliasReservations;
        }

        // Wipes per-save state (actors, labyrinths, resources). Called from the SKSE
        // Revert callback before a save is loaded. The keyword config is NOT cleared:
        // those are live session pointers re-supplied by Papyrus, independent of any
        // save.
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
        AnimationRegistry _animations;
        AttributeRegistry _attributes;
        LabyrinthRegistry _labyrinths;
        QueueRegistry _queues;
        ResourceRegistry _resources;
        ResourceKeywords _keywords;
        RE::TESQuest* _aliasQuest = nullptr;
        std::unordered_map<std::uint64_t, AliasReservation> _aliasReservations;
    };
}
