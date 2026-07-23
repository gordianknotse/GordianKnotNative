#pragma once

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace GK {
    // Skyrim biped slots are numbered 30..61; outfit slot arrays are indexed
    // DIRECTLY by that number (indices 0..29 are always empty), matching the
    // Armor[] layout the Papyrus bindings expose (armors[30] = head, ...).
    inline constexpr std::size_t kBipedSlotFirst = 30;
    inline constexpr std::size_t kBipedSlotLast = 61;
    inline constexpr std::size_t kOutfitSlotCount = 62;

    // Per-actor named "outfits": for each biped slot, the INVENTORY-device Armor
    // FormID meant to occupy it (0 = empty slot). Which slots a device claims is
    // decided by the Papyrus binding layer (rendered-device slot mask, see
    // GKNative.cpp); this registry is plain storage. Outfit names are
    // case-insensitive (stored case-folded). Outfits are save state (serialized
    // in the co-save; see Serialization.cpp, OUTF).
    //
    // NOT thread-safe on its own: GameState owns the instance and guards every
    // call with its mutex (see State/GameState.h).
    class OutfitRegistry {
    public:
        using Slots = std::array<RE::FormID, kOutfitSlotCount>;
        // Case-folded outfit name -> slot array.
        using OutfitMap = std::unordered_map<std::string, Slots>;

        // The named outfit's slots (nullptr if the actor has no such outfit).
        [[nodiscard]] const Slots* Find(RE::FormID a_actor, std::string_view a_name) const;

        // The named outfit's slots, default-created (all empty) if absent.
        [[nodiscard]] Slots& GetOrCreate(RE::FormID a_actor, std::string_view a_name);

        // Erase the outfit outright (no-op if absent). All-EMPTY outfits are
        // otherwise kept: an existing-but-empty outfit is meaningful (it
        // shadows the same-named template and reads as defined), so only an
        // explicit delete removes one.
        void Erase(RE::FormID a_actor, std::string_view a_name);

        // Inserts or overwrites an actor's outfits wholesale (used by
        // serialization load; names arrive already case-folded from the co-save).
        void Put(RE::FormID a_actor, OutfitMap a_outfits);

        void Clear() { _outfits.clear(); }

        // Read access to the backing store (used by serialization). Keys of the
        // inner maps are the case-folded outfit names.
        [[nodiscard]] const std::unordered_map<RE::FormID, OutfitMap>& Actors() const { return _outfits; }

    private:
        std::unordered_map<RE::FormID, OutfitMap> _outfits;
    };
}