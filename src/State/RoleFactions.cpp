#include "State/RoleFactions.h"

#include "State/AliasPool.h"
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

        // Queue the hook `OnGKRoleApplied(Actor, ObjectReference, Int)` on a driver
        // alias. Takes no lock -- callers either hold the GameState lock or run
        // inside a VM callback where taking it could deadlock (lock order
        // everywhere else is GameState -> VM).
        void DispatchRoleAppliedHook(RE::BGSRefAlias& a_alias, RE::Actor& a_actor, RE::FormID a_labyrinth,
                                     std::uint32_t a_role) {
            auto* labForm = RE::TESForm::LookupByID(a_labyrinth);
            auto* lab = labForm ? labForm->As<RE::TESObjectREFR>() : nullptr;
            DispatchVmCall(a_alias.GetVMTypeID(), &a_alias, "ReferenceAlias", "OnGKRoleApplied",
                           RE::MakeFunctionArguments(&a_actor, static_cast<RE::TESObjectREFR*>(lab),
                                                     static_cast<std::int32_t>(a_role)));
        }

        // Stack-completion callback for a queued AddToFaction: fires OnGKRoleApplied
        // only once the faction change has REALLY finished executing -- the strong
        // guarantee callers rely on to stop combat in the hook. The target alias is
        // resolved by Apply() up front (under the GameState lock, so a just-reserved
        // slot is visible); the callback itself touches no GameState.
        //
        // NOTE: a CTD was once misattributed to this functor and it was briefly
        // replaced with plain FIFO dispatch; the real cause was an oversized .pex
        // docstring (loader buffer overflow). The functor is fine: CanSave() is
        // false, so the VM simply drops it if the stack is saved mid-flight.
        class RoleAppliedCallback : public RE::BSScript::IStackCallbackFunctor {
        public:
            RoleAppliedCallback(RE::ActorHandle a_actor, RE::BGSRefAlias* a_alias, RE::FormID a_labyrinth,
                                std::uint32_t a_role)
                : _actor(a_actor), _alias(a_alias), _labyrinth(a_labyrinth), _role(a_role) {}

            void operator()(RE::BSScript::Variable) override {
                const auto actor = _actor.get();
                if (actor && _alias) {
                    DispatchRoleAppliedHook(*_alias, *actor, _labyrinth, _role);
                }
            }
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

        private:
            RE::ActorHandle _actor;
            RE::BGSRefAlias* _alias;
            RE::FormID _labyrinth;
            std::uint32_t _role;
        };

        bool DispatchFactionCall(RE::Actor& a_actor, const char* a_fn, RE::TESFaction& a_faction,
                                 RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> a_callback = {}) {
            return DispatchVmCall(static_cast<RE::VMTypeID>(a_actor.GetFormType()), &a_actor, "Actor", a_fn,
                                  RE::MakeFunctionArguments(&a_faction), std::move(a_callback));
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

        auto* quest = a_state.AliasQuest();
        for (const auto bit : kSyncedBits) {
            if ((a_addedRoles & bit) == 0) {
                continue;
            }
            // Resolve the hook target now, under the lock: FindHoldingOrReserved
            // also sees the slot a brand-new actor was just reserved.
            auto* alias = quest ? AliasPool::FindHoldingOrReserved(a_state, *quest, a_actor) : nullptr;
            if (auto* faction = FactionFor(*factions, bit)) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                if (alias) {
                    callback.reset(new RoleAppliedCallback(a_actor.GetHandle(), alias, a_labyrinth, bit));
                }
                DispatchFactionCall(a_actor, "AddToFaction", *faction, std::move(callback));
                logger::info("RoleFactions: actor {:08X} + faction {:08X} (role {:#x}, labyrinth {:08X}).",
                             a_actor.GetFormID(), faction->GetFormID(), bit, a_labyrinth);
            } else if (alias) {
                // No faction to wait for -- the role itself was applied synchronously.
                DispatchRoleAppliedHook(*alias, a_actor, a_labyrinth, bit);
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
