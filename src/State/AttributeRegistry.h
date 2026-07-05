#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace GK {
    // Per-actor attribute store: a free-form String key -> ObjectReference value
    // (held as a FormID), so Papyrus / other plugins can mint their own attributes
    // without enums or handle mappings. Keys are case-insensitive (matching Papyrus
    // string semantics); they are stored case-folded. Attributes are save state
    // (serialized in the co-save; see Serialization.cpp).
    //
    // NOT thread-safe on its own: GameState owns the instance and guards every call
    // with its mutex (see State/GameState.h). Operate on it only while holding that
    // lock.
    class AttributeRegistry {
    public:
        // Case-folded attribute key -> value FormID.
        using AttributeMap = std::unordered_map<std::string, RE::FormID>;

        // Set a_actor's a_key attribute to a_value. A value of 0 clears the
        // attribute (Papyrus passes None to clear).
        void Set(RE::FormID a_actor, std::string_view a_key, RE::FormID a_value);

        // a_actor's a_key attribute (0 if never set or cleared). Callers resolve
        // the FormID themselves (see the GetActorAttribute binding in GKNative.cpp).
        [[nodiscard]] RE::FormID Get(RE::FormID a_actor, std::string_view a_key) const;

        // Inserts or overwrites an actor's attributes wholesale (used by
        // serialization load; keys arrive already case-folded from the co-save).
        void Put(RE::FormID a_actor, AttributeMap a_attributes);

        void Clear() { _attributes.clear(); }

        // Read access to the backing store (used by serialization). Keys of the
        // inner maps are the case-folded attribute names; no actor in here has an
        // empty map (clearing the last attribute erases the actor's entry).
        [[nodiscard]] const std::unordered_map<RE::FormID, AttributeMap>& Actors() const { return _attributes; }

    private:
        std::unordered_map<RE::FormID, AttributeMap> _attributes;
    };
}
