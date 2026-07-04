#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace GK {
    // Named FIFO queues of actors (stored as FormIDs), keyed by a free-form queue
    // name so Papyrus / other plugins can mint their own queues without enums or
    // handle mappings. Names are case-insensitive (matching Papyrus string
    // semantics); keys are stored case-folded. An actor may sit in any number of
    // DIFFERENT queues, but at most once in any one queue. Queue contents are
    // save state (serialized in the co-save; see Serialization.cpp).
    //
    // NOT thread-safe on its own: GameState owns the instance and guards every call
    // with its mutex (see State/GameState.h). Operate on it only while holding that
    // lock.
    class QueueRegistry {
    public:
        // Append a_actor to the back of the named queue (created on first use).
        // False if the actor is already waiting in that queue.
        bool Enqueue(std::string_view a_queue, RE::FormID a_actor);

        // Pop and return the front FormID (0 if the queue is empty or unknown).
        // Callers resolve the FormID themselves; unresolvable entries are theirs
        // to skip (see the DequeueActor binding in GKNative.cpp).
        RE::FormID Dequeue(std::string_view a_queue);

        // Waiting entries in the named queue (0 if it doesn't exist).
        [[nodiscard]] std::size_t Size(std::string_view a_queue) const;

        // Empty the named queue, dropping every waiting entry.
        void ClearQueue(std::string_view a_queue);

        // Inserts or overwrites a queue wholesale (used by serialization load).
        void Put(std::string_view a_queue, std::deque<RE::FormID> a_entries);

        void Clear() { _queues.clear(); }

        // Read access to the backing store (used by serialization). Keys are the
        // case-folded queue names; no queue in here is ever empty (drained queues
        // are erased so one-shot names don't accumulate).
        [[nodiscard]] const std::unordered_map<std::string, std::deque<RE::FormID>>& Queues() const { return _queues; }

    private:
        // Lowercased copy of a_queue: the map key (Papyrus strings compare
        // case-insensitively, so "MyQueue" and "myqueue" must be one queue).
        [[nodiscard]] static std::string Fold(std::string_view a_queue);

        std::unordered_map<std::string, std::deque<RE::FormID>> _queues;
    };
}