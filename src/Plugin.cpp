#include "Log.h"

#include "Events/ActivateHook.h"
#include "Events/EquipEventSink.h"
#include "Papyrus/GKNative.h"
#include "Serialization/Serialization.h"
#include "UI/DebugOverlay.h"

// =============================================================================
// Gordian Knot — native SKSE entry point.
//
// This is intentionally a minimal, verifiable skeleton: it loads, sets up
// logging, and registers an SKSE messaging listener. The gameplay systems are
// NOT implemented yet — this file just establishes the load path and shows
// where each future subsystem hooks in (see the kDataLoaded case below).
// =============================================================================

namespace {
    // SKSE broadcasts lifecycle messages on the "SKSE" channel. kDataLoaded is
    // the safe point at which the game's data (forms, etc.) is fully loaded.
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message) {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            // All SKSE plugins' SKSEPluginLoad have run. Good place to wire up
            // inter-plugin APIs later, if needed.
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            // Game data is ready. Subsystems register here.
            GK::Events::EquipEventSink::Install();
            GK::Events::ActivateHook::Install();
            // Still to come:
            //   - TESCombatEvent sink  (combat dispatch via SKSE task interface)
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            // A save is about to load. Co-save deserialization is driven by the
            // SKSE serialization interface, not here, but state may be reset.
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            break;

        default:
            break;
        }
    }
}

// SKSEPluginLoad is a CommonLibSSE-NG macro: it emits the exported plugin entry
// point and the auto-generated SKSE plugin version data (name/version come from
// the CMake project() / add_commonlibsse_plugin() call). Returning false aborts
// the load.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    logger::info("{} native plugin loaded.", plugin->GetName());

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", OnSKSEMessage)) {
        logger::error("Failed to register SKSE messaging listener.");
        return false;
    }

    // Register the GKNative Papyrus function surface. The interface invokes the
    // callback once per VM init, so it's safe to register here at load time.
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(GK::Papyrus::Register)) {
        logger::error("Failed to register GKNative Papyrus functions.");
        return false;
    }

    // Register SKSE co-save serialization (Save/Load/Revert) for native state.
    GK::Serialization::Install();

    // Debug overlay hooks (D3D init / present / input) must be written before the
    // game initializes its renderer, i.e. here at plugin load.
    GK::UI::Install();

    return true;
}
