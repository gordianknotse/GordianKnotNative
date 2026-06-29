#pragma once

#include "State/Cell.h"
#include "State/Furniture.h"
#include "State/Marker.h"
#include "State/Resource.h"

namespace GK {
    // Owns the three resource pools and the single monotonic handle counter that
    // keeps handles unique across all pools (and stable across saves once the
    // counter is persisted in Phase 5).
    //
    // NOT thread-safe: lives inside GameState and is used only under its lock.
    class ResourceRegistry {
    public:
        [[nodiscard]] ResourcePool<Cell>& CellPool() { return _cells; }
        [[nodiscard]] const ResourcePool<Cell>& CellPool() const { return _cells; }

        [[nodiscard]] ResourcePool<Marker>& MarkerPool() { return _markers; }
        [[nodiscard]] const ResourcePool<Marker>& MarkerPool() const { return _markers; }

        [[nodiscard]] ResourcePool<Furniture>& FurniturePool() { return _furniture; }
        [[nodiscard]] const ResourcePool<Furniture>& FurniturePool() const { return _furniture; }

        // Allocates the next unique handle. Never returns kInvalidHandle (0).
        [[nodiscard]] Handle NextHandle() { return ++_nextHandle; }

        [[nodiscard]] Handle PeekNextHandle() const { return _nextHandle; }
        void SetNextHandle(Handle a_value) { _nextHandle = a_value; }  // serialization load

        void Clear() {
            _cells.Clear();
            _markers.Clear();
            _furniture.Clear();
            _nextHandle = kInvalidHandle;
        }

    private:
        ResourcePool<Cell> _cells;
        ResourcePool<Marker> _markers;
        ResourcePool<Furniture> _furniture;
        Handle _nextHandle = kInvalidHandle;  // ++ before use -> first handle is 1
    };
}
