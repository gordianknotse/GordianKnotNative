#include "State/RoleFactions.h"

#include "State/GameState.h"
#include "VmCall.h"

#include <unordered_set>

namespace GK::RoleFactions {
    namespace {
        constexpr std::uint32_t kSyncedBits[] = {Role::kWarden, Role::kPrisoner};

        RE::TESFaction* FactionFor(const LabyrinthFactions& a_factions, std::uint32_t a_roleBit) {
            switch (a_roleBit) {
            case Role::kWarden:
                return a_factions.warden;
            case Role::kPrisoner:
                return a_factions.prisoner;
            default:
                return nullptr;
            }
        }

        bool DispatchFactionCall(RE::Actor& a_actor, const char* a_fn, RE::TESFaction& a_faction) {
            return DispatchVmCall(static_cast<RE::VMTypeID>(a_actor.GetFormType()), &a_actor, "Actor", a_fn,
                                  RE::MakeFunctionArguments(&a_faction));
        }

        // Every faction the actor's CURRENT scoped roles (across all labyrinths)
        // entitle it to. Used to keep shared factions on partial role removal.
        std::unordered_set<RE::TESFaction*> EntitledFactions(GameState& a_state, RE::FormID a_actorID) {
            std::unordered_set<RE::TESFaction*> out;
            for (const auto lab : a_state.Actors().GetLabyrinths(a_actorID)) {
                const auto* factions = a_state.Labyrinths().Find(lab);
                if (!factions) {
                    continue;
                }
                const auto roles = a_state.Actors().GetRoles(a_actorID, lab);
                for (const auto bit : kSyncedBits) {
                    if ((roles & bit) != 0) {
                        if (auto* faction = FactionFor(*factions, bit)) {
                            out.insert(faction);
                        }
                    }
                }
            }
            return out;
        }
    }

    void Apply(GameState& a_state, RE::Actor& a_actor, RE::FormID a_labyrinth, std::uint32_t a_addedRoles,
               std::uint32_t a_removedRoles) {
        const auto* factions = a_state.Labyrinths().Find(a_labyrinth);
        if (!factions) {
            return;  // labyrinth not registered (or registered without factions yet)
        }

        for (const auto bit : kSyncedBits) {
            if ((a_addedRoles & bit) == 0) {
                continue;
            }
            if (auto* faction = FactionFor(*factions, bit)) {
                DispatchFactionCall(a_actor, "AddToFaction", *faction);
                logger::info("RoleFactions: actor {:08X} + faction {:08X} (role {:#x}, labyrinth {:08X}).",
                             a_actor.GetFormID(), faction->GetFormID(), bit, a_labyrinth);
            }
        }

        if (a_removedRoles == 0) {
            return;
        }
        const auto entitled = EntitledFactions(a_state, a_actor.GetFormID());
        for (const auto bit : kSyncedBits) {
            if ((a_removedRoles & bit) == 0) {
                continue;
            }
            auto* faction = FactionFor(*factions, bit);
            if (faction && !entitled.contains(faction)) {
                DispatchFactionCall(a_actor, "RemoveFromFaction", *faction);
                logger::info("RoleFactions: actor {:08X} - faction {:08X} (role {:#x}, labyrinth {:08X}).",
                             a_actor.GetFormID(), faction->GetFormID(), bit, a_labyrinth);
            }
        }
    }
}
