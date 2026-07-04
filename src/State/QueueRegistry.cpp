#include "State/QueueRegistry.h"

#include <algorithm>
#include <cctype>

namespace GK {
    std::string QueueRegistry::Fold(std::string_view a_queue) {
        std::string key(a_queue);
        std::ranges::transform(key, key.begin(),
                               [](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
        return key;
    }

    bool QueueRegistry::Enqueue(std::string_view a_queue, RE::FormID a_actor) {
        auto& queue = _queues[Fold(a_queue)];
        if (std::ranges::find(queue, a_actor) != queue.end()) {
            return false;
        }
        queue.push_back(a_actor);
        return true;
    }

    RE::FormID QueueRegistry::Dequeue(std::string_view a_queue) {
        const auto it = _queues.find(Fold(a_queue));
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

    std::size_t QueueRegistry::Size(std::string_view a_queue) const {
        const auto it = _queues.find(Fold(a_queue));
        return it != _queues.end() ? it->second.size() : 0;
    }

    void QueueRegistry::ClearQueue(std::string_view a_queue) { _queues.erase(Fold(a_queue)); }

    void QueueRegistry::Put(std::string_view a_queue, std::deque<RE::FormID> a_entries) {
        if (a_entries.empty()) {
            return;
        }
        _queues[Fold(a_queue)] = std::move(a_entries);
    }
}