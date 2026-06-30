#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// =============================================================================
// Occupiable resource model. A resource is something an actor can occupy (a cell,
// a patrol marker, a furniture). Each is identified to Papyrus by an opaque int
// handle (stable across saves once serialization lands in Phase 5). See
// docs/PLAN-V1.md §4.
// =============================================================================

namespace GK {
    // Opaque handle exposed to Papyrus as an int. 0 = none / invalid.
    using Handle = std::int32_t;
    inline constexpr Handle kInvalidHandle = 0;

    // Mirrored as Papyrus GK_TYPE_* constants.
    enum class ResourceType : std::int32_t {
        kCell = 0,
        kMarker = 1,
        kFurniture = 2,
    };

    // Shared state for any occupiable resource. Concrete types (Cell/Marker/
    // Furniture) inherit and add their own reference FormIDs + a Key() returning
    // the primary REFR FormID used as the discovery/dedup key.
    struct OccupiableResource {
        Handle handle = kInvalidHandle;
        RE::FormID labyrinth = 0;           // labyrinth anchor REFR FormID
        std::uint32_t maxOccupants = 1;
        std::vector<RE::FormID> occupants;  // actor FormIDs (filled in Phase 4)

        [[nodiscard]] bool HasSpace() const { return occupants.size() < maxOccupants; }

        [[nodiscard]] bool Contains(RE::FormID a_actor) const {
            return std::find(occupants.begin(), occupants.end(), a_actor) != occupants.end();
        }
    };

    // Typed container for one resource kind. Indexes by handle and by the
    // resource's primary key (its REFR FormID) so discovery can dedupe.
    //
    // NOT thread-safe: lives inside GameState and is used only under its lock.
    template <class T>
    class ResourcePool {
    public:
        [[nodiscard]] T* FindByHandle(Handle a_handle) {
            const auto it = _byHandle.find(a_handle);
            return it != _byHandle.end() ? &it->second : nullptr;
        }
        [[nodiscard]] const T* FindByHandle(Handle a_handle) const {
            const auto it = _byHandle.find(a_handle);
            return it != _byHandle.end() ? &it->second : nullptr;
        }

        [[nodiscard]] T* FindByKey(RE::FormID a_key) {
            const auto it = _byKey.find(a_key);
            return it != _byKey.end() ? FindByHandle(it->second) : nullptr;
        }

        // Inserts (or replaces) a fully-formed resource whose handle + key are set.
        // Used by discovery for new resources and by serialization load.
        T& Insert(T a_resource) {
            const Handle handle = a_resource.handle;
            const RE::FormID key = a_resource.Key();
            _byKey[key] = handle;
            const auto [it, _] = _byHandle.insert_or_assign(handle, std::move(a_resource));
            return it->second;
        }

        void Remove(Handle a_handle) {
            const auto it = _byHandle.find(a_handle);
            if (it == _byHandle.end()) {
                return;
            }
            _byKey.erase(it->second.Key());
            _byHandle.erase(it);
        }

        [[nodiscard]] std::vector<Handle> HandlesInLabyrinth(RE::FormID a_labyrinth) const {
            std::vector<Handle> out;
            for (const auto& [handle, res] : _byHandle) {
                if (res.labyrinth == a_labyrinth) {
                    out.push_back(handle);
                }
            }
            return out;
        }

        void Clear() {
            _byHandle.clear();
            _byKey.clear();
        }

        [[nodiscard]] std::unordered_map<Handle, T>& All() { return _byHandle; }
        [[nodiscard]] const std::unordered_map<Handle, T>& All() const { return _byHandle; }

    private:
        std::unordered_map<Handle, T> _byHandle;
        std::unordered_map<RE::FormID, Handle> _byKey;  // primary REFR FormID -> handle
    };
}
