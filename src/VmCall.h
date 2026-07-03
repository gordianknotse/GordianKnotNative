#pragma once

namespace GK {
    // Queue a Papyrus method call on a game object through the VM, fire-and-forget
    // (e.g. "ReferenceAlias"/"ForceRefTo", "Actor"/"AddToFaction"). Takes ownership
    // of a_args. Safe to call from any thread -- the call itself runs later on a VM
    // thread. Returns false if the VM couldn't dispatch (no handle / VM missing).
    inline bool DispatchVmCall(RE::VMTypeID a_typeID, void* a_object, const char* a_class, const char* a_fn,
                               RE::BSScript::IFunctionArguments* a_args) {
        const std::unique_ptr<RE::BSScript::IFunctionArguments> args{a_args};
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!policy) {
            return false;
        }
        const auto handle = policy->GetHandleForObject(a_typeID, a_object);
        if (handle == policy->EmptyHandle()) {
            return false;
        }
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        return vm->DispatchMethodCall2(handle, a_class, a_fn, args.get(), callback);
    }
}
