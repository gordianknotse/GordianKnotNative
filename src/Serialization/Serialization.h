#pragma once

// =============================================================================
// SKSE co-save serialization for native state. Registers the unique ID and the
// Save / Load / Revert callbacks with the SKSE serialization interface.
// See docs/PLAN-V1.md §7.
// =============================================================================

namespace GK::Serialization {
    // Registers the serialization callbacks. Call once from SKSEPluginLoad after
    // SKSE::Init. No-op (logged) if the interface is unavailable.
    void Install();
}
