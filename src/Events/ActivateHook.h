#pragma once

namespace GK::Events::ActivateHook {
    // Claims the PLAYER's activation of discovered resources: a cell door,
    // furniture, or plain activator (activators count as furniture when
    // discovered) belonging to a labyrinth the player is warden of — or an NPC
    // that is a prisoner of such a labyrinth. A claimed activation is fully
    // suppressed (no vanilla action, no Papyrus OnActivate on the target, no
    // TESActivateEvent) and a GK_OnActivate* mod event is sent to Papyrus
    // instead (vocabulary in the .cpp). Every other activation passes through
    // untouched: NPC activators (pathing through doors, using furniture),
    // unregistered references, and labyrinths the player is not warden of.
    //
    // Two layers: an ActivateHandler::ProcessButton vtable hook consumes the
    // E-press before the activation machinery runs (full suppression), and
    // TESObjectDOOR / TESFurniture / TESObjectACTI / TESNPC ::Activate vfunc
    // hooks back it up for paths that bypass the input handler — scripted
    // ObjectReference.Activate(Game.GetPlayer()) is claimed there under the
    // same rules, though on that path the target's OnActivate still fires.
    // Papyrus cannot yet re-trigger the vanilla action on a claimed ref
    // (needs a pass-through binding).

    // Writes the three vfunc hooks. Call once, after kDataLoaded.
    void Install();
}