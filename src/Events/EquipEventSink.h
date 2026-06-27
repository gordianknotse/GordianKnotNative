#pragma once

namespace GK::Events {
    // Listens for TESEquipEvent and forwards the player's equip/unequip actions to
    // Papyrus as mod events (GK_OnEquip / GK_OnUnequip). This replaces the
    // unreliable Papyrus OnEquipped callback.
    //
    // Scope is currently player-only (see ProcessEvent); widen once form-resolution
    // and serialization exist to also cover authored NPCs (GkLabWarden / GkWanderer).
    class EquipEventSink : public RE::BSTEventSink<RE::TESEquipEvent> {
    public:
        [[nodiscard]] static EquipEventSink* GetSingleton();

        // Registers the singleton with the game's equip event source. Call once,
        // after kDataLoaded.
        static void Install();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event,
                                              RE::BSTEventSource<RE::TESEquipEvent>* a_source) override;

    private:
        EquipEventSink() = default;
        EquipEventSink(const EquipEventSink&) = delete;
        EquipEventSink(EquipEventSink&&) = delete;
        EquipEventSink& operator=(const EquipEventSink&) = delete;
        EquipEventSink& operator=(EquipEventSink&&) = delete;
    };
}
