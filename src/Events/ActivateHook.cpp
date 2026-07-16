#include "Events/ActivateHook.h"

#include "State/GameState.h"

#include <optional>

namespace GK::Events::ActivateHook {
    namespace {
        // Mod-event vocabulary (native -> Papyrus), v1, following the carrier
        // conventions established in EquipEventSink.cpp: sender = the activated
        // reference (cast back to ObjectReference/Actor in the handler), strArg =
        // the owning labyrinth's anchor FormID as "0x........" (the lossless
        // carrier), numArg = the resource handle (0 for a prisoner NPC, which has
        // no resource handle). Papyrus side:
        //   RegisterForModEvent("GK_OnActivateCellDoor", "...") ->
        //     Event(string eventName, string strArg, float numArg, Form sender)
        constexpr auto kCellDoorEvent = "GK_OnActivateCellDoor"sv;
        constexpr auto kFurnitureEvent = "GK_OnActivateFurniture"sv;
        constexpr auto kPrisonerEvent = "GK_OnActivatePrisoner"sv;

        constexpr RE::FormID kPlayerFormID = 0x14;

        // What GK sends instead of the vanilla action when it claims an activation.
        struct Claim {
            std::string_view event;
            RE::FormID labyrinth = 0;
            Handle handle = kInvalidHandle;
        };

        [[nodiscard]] bool PlayerIsWardenOf(GameState& a_state, RE::FormID a_lab) {
            return (a_state.Actors().GetRoles(kPlayerFormID, a_lab) & Role::kWarden) != 0;
        }

        // The filter chain: player activator -> registered target -> owning
        // labyrinth -> player is its warden. nullopt = vanilla proceeds.
        [[nodiscard]] std::optional<Claim> ShouldClaim(RE::TESObjectREFR* a_targetRef,
                                                       RE::TESObjectREFR* a_activatorRef) {
            if (!a_targetRef || !a_activatorRef || a_activatorRef != RE::PlayerCharacter::GetSingleton()) {
                return std::nullopt;
            }

            const RE::FormID targetID = a_targetRef->GetFormID();
            auto* state = GameState::GetSingleton();
            const auto lock = state->Lock();

            if (const auto* cell = state->Resources().CellPool().FindByKey(targetID)) {
                if (PlayerIsWardenOf(*state, cell->labyrinth)) {
                    return Claim{kCellDoorEvent, cell->labyrinth, cell->handle};
                }
                return std::nullopt;
            }
            if (const auto* furniture = state->Resources().FurniturePool().FindByKey(targetID)) {
                if (PlayerIsWardenOf(*state, furniture->labyrinth)) {
                    return Claim{kFurnitureEvent, furniture->labyrinth, furniture->handle};
                }
                return std::nullopt;
            }
            if (a_targetRef->As<RE::Actor>()) {
                for (const RE::FormID lab : state->Actors().GetLabyrinthsByRole(targetID, Role::kPrisoner)) {
                    if (PlayerIsWardenOf(*state, lab)) {
                        return Claim{kPrisonerEvent, lab, kInvalidHandle};
                    }
                }
            }
            return std::nullopt;
        }

        void SendClaimEvent(const Claim& a_claim, RE::TESObjectREFR* a_targetRef) {
            logger::info("ActivateHook: claimed {:08X} -> {} (labyrinth {:08X}, handle {}).",
                         a_targetRef->GetFormID(), a_claim.event, a_claim.labyrinth, a_claim.handle);
            auto* source = SKSE::GetModCallbackEventSource();
            if (!source) {
                return;
            }
            SKSE::ModCallbackEvent modEvent{
                .eventName = a_claim.event,
                .strArg = std::format("0x{:08X}", a_claim.labyrinth),
                .numArg = static_cast<float>(a_claim.handle),
                .sender = a_targetRef,
            };
            source->SendEvent(&modEvent);
        }

        // Player E-press interception: ActivateHandler::ProcessButton (vfunc 0x4
        // on PlayerInputHandler) runs BEFORE the activation machinery, so
        // consuming here suppresses everything vanilla on a claimed target --
        // including the Papyrus OnActivate event and TESActivateEvent, which
        // TESObjectREFR::ActivateRef dispatches BEFORE the bound-object Activate
        // hooks below ever run (they can only stop the default action). While
        // the crosshair rests on a claimed ref every phase of the button is
        // swallowed; the claim event fires on the initial press only.
        struct ActivateButtonHook {
            static void thunk(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data) {
                if (a_event) {
                    const auto* pick = RE::CrosshairPickData::GetSingleton();
                    const auto target = pick ? pick->target.get() : RE::NiPointer<RE::TESObjectREFR>{};
                    if (target) {
                        if (const auto claim = ShouldClaim(target.get(), RE::PlayerCharacter::GetSingleton())) {
                            if (a_event->IsDown()) {
                                SendClaimEvent(*claim, target.get());
                            }
                            return;  // consumed: ActivateRef never runs, nothing vanilla fires
                        }
                    }
                }
                func(a_this, a_event, a_data);
            }
            static inline REL::Relocation<decltype(thunk)> func;

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ActivateHandler[0]};
                func = vtbl.write_vfunc(0x4, thunk);
            }
        };

        // TESBoundObject::Activate (vfunc 0x37, see RE/T/TESBoundObject.h), hooked
        // on each concrete class vtable below. Chains to the vanilla vfunc for
        // everything it doesn't claim, so other mods hooking the same slot and all
        // non-player activations keep working. BACKSTOP behind ActivateButtonHook:
        // it catches activation paths that bypass the input handler (scripted
        // ObjectReference.Activate(player), engine-internal calls). On those paths
        // the target's Papyrus OnActivate still fires -- only the input-level hook
        // can stop that.
        template <class TForm>
        struct Hook {
            static bool thunk(TForm* a_this, RE::TESObjectREFR* a_targetRef, RE::TESObjectREFR* a_activatorRef,
                              std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_targetCount) {
                if (const auto claim = ShouldClaim(a_targetRef, a_activatorRef)) {
                    SendClaimEvent(*claim, a_targetRef);
                    return true;  // claimed: vanilla open/sit/talk suppressed
                }
                return func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);
            }
            static inline REL::Relocation<decltype(thunk)> func;

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{TForm::VTABLE[0]};
                func = vtbl.write_vfunc(0x37, thunk);
            }
        };
    }

    void Install() {
        ActivateButtonHook::Install();
        Hook<RE::TESObjectDOOR>::Install();
        Hook<RE::TESFurniture>::Install();
        // Plain activators can be discovered as GK furniture too (the registry
        // keys on the linked-ref keyword, not the form class). Subclasses with
        // their own vtables (BGSTalkingActivator, TESFlora) are NOT covered.
        Hook<RE::TESObjectACTI>::Install();
        Hook<RE::TESNPC>::Install();
        logger::info("ActivateHook: door/furniture/activator/NPC Activate hooks installed.");
    }
}