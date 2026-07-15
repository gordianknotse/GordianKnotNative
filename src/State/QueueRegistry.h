#pragma once

#include "State/GameClock.h"

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GK {
    // Seconds on the session clock the delayed enqueues run on: UNPAUSED
    // gameplay time (menus and the console stop the countdown; see GameClock).
    // Absolute values are meaningless across sessions, so serialization stores
    // REMAINING seconds and rebases on load (see Serialization.cpp, QUEU v2).
    [[nodiscard]] inline double NowSeconds() { return GameClock::Now(); }

    // An actor scheduled to join a queue once its time comes (see EnqueueAfter).
    struct DelayedEnqueue {
        std::string queue;    // case-folded queue name
        RE::FormID actor = 0;
        double due = 0.0;     // absolute NowSeconds() deadline
    };

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

        // Schedule a_actor to join the named queue a_delay seconds after a_now
        // (it actually joins on the first PromoteDue once the time has come).
        // False if the actor is already waiting in that queue or already
        // scheduled for it.
        bool EnqueueAfter(std::string_view a_queue, RE::FormID a_actor, double a_delay, double a_now);

        // Move every delayed entry whose time has come into its queue, in due
        // order. The Papyrus bindings call this before any queue operation, so a
        // due actor is always in its queue by the time anything observes it.
        void PromoteDue(double a_now);

        // Pop and return the front FormID (0 if the queue is empty or unknown).
        // Callers resolve the FormID themselves; unresolvable entries are theirs
        // to skip (see the DequeueActor binding in GKNative.cpp).
        RE::FormID Dequeue(std::string_view a_queue);

        // Pop and return the BACK FormID (0 if the queue is empty or unknown):
        // the mirror of Dequeue, used by the PeekLastActor binding.
        RE::FormID PopBack(std::string_view a_queue);

        // The FormID at position a_index (0 = front) WITHOUT removing it (0 if
        // the queue is unknown or a_index is past the end). Used by the
        // PeekActorAt binding, which drops stale entries and retries the slot.
        [[nodiscard]] RE::FormID At(std::string_view a_queue, std::size_t a_index) const;

        // Put a_actor back at the FRONT of the named queue: undoes a Dequeue when
        // a compound operation fails after popping (no dedupe -- the caller just
        // popped this entry).
        void PushFront(std::string_view a_queue, RE::FormID a_actor);

        // Remove a_actor from the named queue wherever it sits -- waiting entry or
        // still-scheduled delayed entry (like ClearQueue, so a claimed actor can't
        // resurface when its delay ripens). True if anything was removed.
        bool Remove(std::string_view a_queue, RE::FormID a_actor);

        // Waiting entries in the named queue (0 if it doesn't exist).
        [[nodiscard]] std::size_t Size(std::string_view a_queue) const;

        // Empty the named queue, dropping every waiting entry -- including the
        // delayed ones still scheduled for it.
        void ClearQueue(std::string_view a_queue);

        // Inserts or overwrites a queue wholesale (used by serialization load).
        void Put(std::string_view a_queue, std::deque<RE::FormID> a_entries);

        // Replaces the delayed-enqueue list wholesale (used by serialization load).
        void PutDelayed(std::vector<DelayedEnqueue> a_entries) { _delayed = std::move(a_entries); }

        void Clear() {
            _queues.clear();
            _delayed.clear();
        }

        // Read access to the backing store (used by serialization). Keys are the
        // case-folded queue names; no queue in here is ever empty (drained queues
        // are erased so one-shot names don't accumulate).
        [[nodiscard]] const std::unordered_map<std::string, std::deque<RE::FormID>>& Queues() const { return _queues; }

        // Read access to the delayed enqueues (serialization + debug overlay).
        [[nodiscard]] const std::vector<DelayedEnqueue>& Delayed() const { return _delayed; }

    private:
        std::unordered_map<std::string, std::deque<RE::FormID>> _queues;
        std::vector<DelayedEnqueue> _delayed;
    };
}