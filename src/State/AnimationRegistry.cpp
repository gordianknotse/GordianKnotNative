#include "State/AnimationRegistry.h"

#include "State/CaseFold.h"

namespace GK {
    bool AnimationRegistry::Add(std::string_view a_registry, std::string_view a_anim, double a_weight) {
        if (a_registry.empty() || a_anim.empty() || a_weight <= 0.0) {
            return false;
        }
        auto& entries = _registries[FoldCase(a_registry)];
        const auto folded = FoldCase(a_anim);
        for (auto& entry : entries) {
            if (FoldCase(entry.name) == folded) {
                entry.name = a_anim;  // refresh the stored casing along with the weight
                entry.weight = a_weight;
                return true;
            }
        }
        entries.push_back({std::string(a_anim), a_weight});
        return true;
    }

    const std::vector<AnimationRegistry::Entry>* AnimationRegistry::Find(std::string_view a_registry) const {
        const auto it = _registries.find(FoldCase(a_registry));
        return it != _registries.end() ? &it->second : nullptr;
    }
}
