#pragma once

namespace RE::BSScript {
    class IVirtualMachine;
}

// =============================================================================
// GKNative — the native-function surface exposed to Papyrus (class "GKNative").
// Registered via SKSE::GetPapyrusInterface()->Register(GK::Papyrus::Register) in
// Plugin.cpp. See docs/PLAN-V1.md §5 for the full surface.
// =============================================================================

namespace GK::Papyrus {
    // Binds the GKNative native functions to the Papyrus VM. Returns false if the
    // VM is null (registration is then skipped).
    bool Register(RE::BSScript::IVirtualMachine* a_vm);
}
