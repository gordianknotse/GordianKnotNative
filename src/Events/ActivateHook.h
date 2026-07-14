#pragma once

namespace GK::Events::ActivateHook {
    // Vtable hooks over TESObjectDOOR / TESFurniture / TESNPC ::Activate. When the
    // PLAYER activates a discovered cell door or furniture belonging to a labyrinth
    // the player is warden of — or an NPC that is a prisoner of such a labyrinth —
    // the vanilla action (open / sit / talk) is suppressed and a GK_OnActivate*
    // mod event is sent to Papyrus instead (vocabulary in the .cpp). Every other
    // activation passes through to vanilla untouched: NPC activators (pathing
    // through doors, using furniture), unregistered references, and labyrinths the
    // player is not warden of.
    //
    // Scripted ObjectReference.Activate(Game.GetPlayer()) calls land in the same
    // vfunc and are claimed under the same rules; Papyrus cannot yet re-trigger
    // the vanilla action on a claimed ref (needs a pass-through binding).

    // Writes the three vfunc hooks. Call once, after kDataLoaded.
    void Install();
}