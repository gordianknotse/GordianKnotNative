#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GK {
    // Named, weighted pools of animation names, keyed by a free-form registry
    // name so Papyrus can mint its own pools without enums or handle mappings
    // (names are case-insensitive, matching Papyrus string semantics; keys are
    // stored case-folded). SESSION config like ResourceKeywords -- never
    // serialized and not wiped by Reset; scripts re-Add on every game load
    // (Add is idempotent).
    //
    // NOT thread-safe on its own: GameState owns the instance and guards every
    // call with its mutex (see State/GameState.h). Operate on it only while
    // holding that lock.
    class AnimationRegistry {
    public:
        struct Entry {
            std::string name;     // animation name, stored with the caller's casing
            double weight = 1.0;  // relative draw weight (> 0)
        };

        // Insert a_anim into the named registry (created on first use) with the
        // given weight, or UPDATE its weight (and casing) when already present --
        // so re-registering on every load converges instead of accumulating.
        // False if either name is empty or a_weight <= 0.
        bool Add(std::string_view a_registry, std::string_view a_anim, double a_weight);

        // The entries of the named registry (nullptr if it was never used).
        [[nodiscard]] const std::vector<Entry>* Find(std::string_view a_registry) const;

        void Clear() { _registries.clear(); }

        // Read access (debug overlay). Keys are the case-folded registry names.
        [[nodiscard]] const std::unordered_map<std::string, std::vector<Entry>>& All() const { return _registries; }

    private:
        std::unordered_map<std::string, std::vector<Entry>> _registries;
    };
}
