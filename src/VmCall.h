#pragma once

namespace GK {
    // Queue a Papyrus method call on a game object through the VM, fire-and-forget
    // (e.g. "ReferenceAlias"/"ForceRefTo", "Actor"/"AddToFaction"). Takes ownership
    // of a_args. Safe to call from any thread -- the call itself runs later on a VM
    // thread. Returns false if the VM couldn't dispatch (no handle / VM missing).
    //
    // a_callback (optional) fires when the queued stack has finished executing --
    // the only reliable "it really completed" signal. CAUTION: it runs from inside
    // the VM; do not take the GameState lock there (everywhere else the order is
    // GameState lock -> VM lock, so the reverse can deadlock).
    inline bool DispatchVmCall(RE::VMTypeID a_typeID, void* a_object, const char* a_class, const char* a_fn,
                               RE::BSScript::IFunctionArguments* a_args,
                               RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> a_callback = {}) {
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
        return vm->DispatchMethodCall2(handle, a_class, a_fn, args.get(), a_callback);
    }
}
