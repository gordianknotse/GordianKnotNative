#include "State/AliasPool.h"

#include "State/GameState.h"

namespace GK::AliasPool {
    namespace {
        constexpr auto kReservationTTL = std::chrono::seconds(10);

        // Find a free pool alias and reserve it (caller holds the GameState lock).
        // Returns nullptr if the pool is exhausted.
        RE::BGSRefAlias* ReserveFree(GameState& a_state, RE::TESQuest& a_quest) {
            auto& reservations = a_state.AliasReservations();
            const auto now = std::chrono::steady_clock::now();

            const RE::BSReadLockGuard aliasGuard{a_quest.aliasAccessLock};
            for (auto* base : a_quest.aliases) {
                if (!base || !IsPoolAlias(*base)) {
                    continue;
                }
                auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(base);
                if (!refAlias) {
                    continue;  // pool-named but not a ReferenceAlias; ignore
                }
                const auto key = Key(a_quest, base->aliasID);
                if (refAlias->GetReference()) {
                    reservations.erase(key);  // filled: its reservation (if any) is consumed
                    continue;
                }
                if (auto it = reservations.find(key); it != reservations.end()) {
                    if (now < it->second) {
                        continue;  // actively reserved by another caller
                    }
                    reservations.erase(it);  // expired: the fill never landed
                }
                reservations.emplace(key, now + kReservationTTL);
                return refAlias;
            }
            return nullptr;  // pool exhausted
        }

        // Queue a Papyrus call on a pool alias (e.g. ForceRefTo/Clear) through the VM.
        // Takes ownership of a_args. Returns false if the VM couldn't dispatch.
        bool DispatchAliasCall(RE::BGSRefAlias& a_alias, const char* a_fn, RE::BSScript::IFunctionArguments* a_args) {
            const std::unique_ptr<RE::BSScript::IFunctionArguments> args{a_args};
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!policy) {
                return false;
            }
            const auto handle = policy->GetHandleForObject(a_alias.GetVMTypeID(), &a_alias);
            if (handle == policy->EmptyHandle()) {
                return false;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            return vm->DispatchMethodCall2(handle, "ReferenceAlias", a_fn, args.get(), callback);
        }
    }

    bool IsPoolAlias(const RE::BGSBaseAlias& a_alias) {
        const char* name = a_alias.aliasName.c_str();
        return name && _strnicmp(name, kPrefix.data(), kPrefix.size()) == 0;
    }

    std::uint64_t Key(const RE::TESQuest& a_quest, std::uint32_t a_aliasID) {
        return (static_cast<std::uint64_t>(a_quest.GetFormID()) << 32) | a_aliasID;
    }

    RE::BGSRefAlias* FindHolding(RE::TESQuest& a_quest, const RE::Actor& a_actor) {
        const RE::BSReadLockGuard aliasGuard{a_quest.aliasAccessLock};
        for (auto* base : a_quest.aliases) {
            if (!base || !IsPoolAlias(*base)) {
                continue;
            }
            auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(base);
            if (refAlias && refAlias->GetActorReference() == &a_actor) {
                return refAlias;
            }
        }
        return nullptr;
    }

    bool EnsureTrackable(GameState& a_state, RE::Actor& a_actor) {
        const auto id = a_actor.GetFormID();
        if (a_state.Actors().Records().contains(id)) {
            return true;  // already tracked -> already holds (or was scanned into) a slot
        }
        auto* quest = a_state.AliasQuest();
        if (!quest) {
            logger::warn("AliasPool: actor {:08X} not tracked (no alias quest; call ConfigureAliasQuest first).", id);
            return false;
        }
        auto* alias = ReserveFree(a_state, *quest);
        if (!alias) {
            logger::warn("AliasPool: actor {:08X} not tracked (alias pool exhausted).", id);
            return false;
        }
        if (!DispatchAliasCall(*alias, "ForceRefTo",
                               RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(&a_actor)))) {
            a_state.AliasReservations().erase(Key(*quest, alias->aliasID));  // free the slot again
            logger::error("AliasPool: actor {:08X} not tracked (ForceRefTo dispatch failed).", id);
            return false;
        }
        // Constructor hook, symmetric with OnGKRelease in Release(). The alias
        // script's OnInit is NO substitute: it fires when the script instance is
        // created (quest start, alias still empty), not when ForceRefTo fills it.
        // Queued behind the ForceRefTo above, so the alias is filled when it runs.
        DispatchAliasCall(*alias, "OnGKAssign", RE::MakeFunctionArguments(&a_actor));
        logger::info("AliasPool: actor {:08X} -> pool alias {} ('{}').", id, alias->aliasID, alias->aliasName.c_str());
        return true;
    }

    void Release(GameState& a_state, RE::Actor& a_actor) {
        auto* quest = a_state.AliasQuest();
        if (!quest) {
            return;
        }
        if (auto* alias = FindHolding(*quest, a_actor)) {
            // Destructor hook: Skyrim has no OnCleared event, so give the driver
            // script a last look at its actor. FindBoundObject resolves the DERIVED
            // script instance for class "ReferenceAlias", and the VM looks the
            // function up across the whole type hierarchy -- so a plain
            // `Function OnGKRelease(Actor akActor)` on the user's script is found.
            // No such function is fine: the dispatch just fails quietly.
            DispatchAliasCall(*alias, "OnGKRelease", RE::MakeFunctionArguments(&a_actor));
            DispatchAliasCall(*alias, "Clear", RE::MakeFunctionArguments());
        }
    }

    std::int32_t CountFree(GameState& a_state, RE::TESQuest& a_quest) {
        const auto& reservations = a_state.AliasReservations();
        const auto now = std::chrono::steady_clock::now();

        std::int32_t free = 0;
        const RE::BSReadLockGuard aliasGuard{a_quest.aliasAccessLock};
        for (const auto* base : a_quest.aliases) {
            if (!base || !IsPoolAlias(*base)) {
                continue;
            }
            const auto* refAlias = skyrim_cast<const RE::BGSRefAlias*>(base);
            if (!refAlias || refAlias->GetReference()) {
                continue;
            }
            const auto it = reservations.find(Key(a_quest, base->aliasID));
            if (it == reservations.end() || now >= it->second) {
                ++free;  // empty and not actively reserved
            }
        }
        return free;
    }
}
