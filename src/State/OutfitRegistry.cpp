#include "State/OutfitRegistry.h"

#include "State/CaseFold.h"

#include <algorithm>

namespace GK {
    const OutfitRegistry::Slots* OutfitRegistry::Find(RE::FormID a_actor, std::string_view a_name) const {
        const auto it = _outfits.find(a_actor);
        if (it == _outfits.end()) {
            return nullptr;
        }
        const auto outfit = it->second.find(FoldCase(a_name));
        return outfit != it->second.end() ? &outfit->second : nullptr;
    }

    OutfitRegistry::Slots& OutfitRegistry::GetOrCreate(RE::FormID a_actor, std::string_view a_name) {
        return _outfits[a_actor][FoldCase(a_name)];  // value-initialized: all slots 0
    }

    void OutfitRegistry::PruneIfEmpty(RE::FormID a_actor, std::string_view a_name) {
        const auto it = _outfits.find(a_actor);
        if (it == _outfits.end()) {
            return;
        }
        const auto key = FoldCase(a_name);
        const auto outfit = it->second.find(key);
        if (outfit == it->second.end() ||
            !std::ranges::all_of(outfit->second, [](RE::FormID a_id) { return a_id == 0; })) {
            return;
        }
        it->second.erase(outfit);
        if (it->second.empty()) {
            _outfits.erase(it);  // keep the invariant: no empty outfit maps stored
        }
    }

    void OutfitRegistry::Put(RE::FormID a_actor, OutfitMap a_outfits) {
        if (a_outfits.empty()) {
            return;
        }
        _outfits[a_actor] = std::move(a_outfits);
    }
}
