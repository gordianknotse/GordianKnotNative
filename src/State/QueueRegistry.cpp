#include "State/QueueRegistry.h"

#include "State/CaseFold.h"

#include <algorithm>

namespace GK {
    bool QueueRegistry::Enqueue(std::string_view a_queue, RE::FormID a_actor) {
        auto& queue = _queues[FoldCase(a_queue)];
        if (std::ranges::find(queue, a_actor) != queue.end()) {
            return false;
        }
        queue.push_back(a_actor);
        return true;
    }

    bool QueueRegistry::EnqueueAfter(std::string_view a_queue, RE::FormID a_actor, double a_delay, double a_now) {
        auto key = FoldCase(a_queue);
        if (const auto it = _queues.find(key);
            it != _queues.end() && std::ranges::find(it->second, a_actor) != it->second.end()) {
            return false;
        }
        if (std::ranges::any_of(_delayed,
                                [&](const DelayedEnqueue& d) { return d.actor == a_actor && d.queue == key; })) {
            return false;
        }
        _delayed.push_back({std::move(key), a_actor, a_now + a_delay});
        return true;
    }

    void QueueRegistry::PromoteDue(double a_now) {
        if (_delayed.empty()) {
            return;
        }
        // Due entries join in due order, so two delays into the same queue keep
        // their scheduled order no matter when the promotion actually runs.
        std::stable_sort(_delayed.begin(), _delayed.end(),
                         [](const DelayedEnqueue& a_lhs, const DelayedEnqueue& a_rhs) { return a_lhs.due < a_rhs.due; });
        std::size_t due = 0;
        while (due < _delayed.size() && _delayed[due].due <= a_now) {
            Enqueue(_delayed[due].queue, _delayed[due].actor);  // dedupe inside; a duplicate just evaporates
            ++due;
        }
        _delayed.erase(_delayed.begin(), _delayed.begin() + static_cast<std::ptrdiff_t>(due));
    }

    bool QueueRegistry::PromoteDelayedNow(std::string_view a_queue, RE::FormID a_actor) {
        const auto key = FoldCase(a_queue);
        const auto it = std::ranges::find_if(
            _delayed, [&](const DelayedEnqueue& d) { return d.actor == a_actor && d.queue == key; });
        if (it == _delayed.end()) {
            return false;
        }
        _delayed.erase(it);
        Enqueue(key, a_actor);  // dedupe inside; a duplicate just evaporates
        return true;
    }

    RE::FormID QueueRegistry::Dequeue(std::string_view a_queue) {
        const auto it = _queues.find(FoldCase(a_queue));
        if (it == _queues.end()) {
            return 0;
        }
        const auto id = it->second.front();
        it->second.pop_front();
        if (it->second.empty()) {
            _queues.erase(it);  // keep the invariant: no empty queues stored
        }
        return id;
    }

    RE::FormID QueueRegistry::PopBack(std::string_view a_queue) {
        const auto it = _queues.find(FoldCase(a_queue));
        if (it == _queues.end()) {
            return 0;
        }
        const auto id = it->second.back();
        it->second.pop_back();
        if (it->second.empty()) {
            _queues.erase(it);  // keep the invariant: no empty queues stored
        }
        return id;
    }

    RE::FormID QueueRegistry::At(std::string_view a_queue, std::size_t a_index) const {
        const auto it = _queues.find(FoldCase(a_queue));
        if (it == _queues.end() || a_index >= it->second.size()) {
            return 0;
        }
        return it->second[a_index];
    }

    void QueueRegistry::PushFront(std::string_view a_queue, RE::FormID a_actor) {
        _queues[FoldCase(a_queue)].push_front(a_actor);
    }

    bool QueueRegistry::Remove(std::string_view a_queue, RE::FormID a_actor) {
        const auto key = FoldCase(a_queue);
        bool removed = false;
        if (const auto it = _queues.find(key); it != _queues.end()) {
            if (const auto pos = std::ranges::find(it->second, a_actor); pos != it->second.end()) {
                it->second.erase(pos);
                removed = true;
                if (it->second.empty()) {
                    _queues.erase(it);  // keep the invariant: no empty queues stored
                }
            }
        }
        removed |= std::erase_if(_delayed, [&](const DelayedEnqueue& d) {
                       return d.queue == key && d.actor == a_actor;
                   }) > 0;
        return removed;
    }

    std::size_t QueueRegistry::Size(std::string_view a_queue) const {
        const auto it = _queues.find(FoldCase(a_queue));
        return it != _queues.end() ? it->second.size() : 0;
    }

    void QueueRegistry::ClearQueue(std::string_view a_queue) {
        const auto key = FoldCase(a_queue);
        _queues.erase(key);
        std::erase_if(_delayed, [&](const DelayedEnqueue& d) { return d.queue == key; });
    }

    void QueueRegistry::Put(std::string_view a_queue, std::deque<RE::FormID> a_entries) {
        if (a_entries.empty()) {
            return;
        }
        _queues[FoldCase(a_queue)] = std::move(a_entries);
    }
}