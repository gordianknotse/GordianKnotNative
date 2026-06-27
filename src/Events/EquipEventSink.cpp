#include "Events/EquipEventSink.h"

namespace GK::Events {
    namespace {
        // Mod-event vocabulary (native -> Papyrus), v1. ModCallbackEvent carries a
        // single Form (used for the actor), one float, and one string, so the
        // equipped item's FormID rides in both numArg (convenient for small base-game
        // FormIDs) and strArg as "0x........" (the lossless carrier — FormIDs at or
        // above 0x01000000 exceed a float's exact-integer range). Papyrus side:
        //   RegisterForModEvent("GK_OnEquip", "...") ->
        //     Event(string eventName, string strArg, float numArg, Form sender)
        constexpr auto kEquipEvent = "GK_OnEquip"sv;
        constexpr auto kUnequipEvent = "GK_OnUnequip"sv;
    }

    EquipEventSink* EquipEventSink::GetSingleton() {
        static EquipEventSink singleton;
        return &singleton;
    }

    void EquipEventSink::Install() {
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::error("ScriptEventSourceHolder unavailable; equip sink not installed.");
            return;
        }
        holder->AddEventSink<RE::TESEquipEvent>(GetSingleton());
        logger::info("EquipEventSink installed.");
    }

    RE::BSEventNotifyControl EquipEventSink::ProcessEvent(const RE::TESEquipEvent* a_event,
                                                         RE::BSTEventSource<RE::TESEquipEvent>*) {
        if (!a_event || !a_event->actor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Player-only scope for now.
        auto* actor = a_event->actor.get();
        if (actor != RE::PlayerCharacter::GetSingleton()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto* baseObject = RE::TESForm::LookupByID(a_event->baseObject);
        const char* itemName = (baseObject && baseObject->GetName()) ? baseObject->GetName() : "<unknown>";

        logger::info("Player {} {} (0x{:08X})", a_event->equipped ? "equipped"sv : "unequipped"sv, itemName,
                     a_event->baseObject);

        if (auto* source = SKSE::GetModCallbackEventSource()) {
            SKSE::ModCallbackEvent modEvent{
                .eventName = a_event->equipped ? kEquipEvent : kUnequipEvent,
                .strArg = std::format("0x{:08X}", a_event->baseObject),
                .numArg = static_cast<float>(a_event->baseObject),
                .sender = actor,
            };
            source->SendEvent(&modEvent);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
