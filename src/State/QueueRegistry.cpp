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

    void QueueRegistry::PushFront(std::string_view a_queue, RE::FormID a_actor) {
        _queues[FoldCase(a_queue)].push_front(a_actor);
    }

    std::size_t QueueRegistry::Size(std::string_view a_queue) const {
        const auto it = _queues.find(FoldCase(a_queue));
        return it != _queues.end() ? it->second.size() : 0;
    }

    void QueueRegistry::ClearQueue(std::string_view a_queue) { _queues.erase(FoldCase(a_queue)); }

    void QueueRegistry::Put(std::string_view a_queue, std::deque<RE::FormID> a_entries) {
        if (a_entries.empty()) {
            return;
        }
        _queues[FoldCase(a_queue)] = std::move(a_entries);
    }
}