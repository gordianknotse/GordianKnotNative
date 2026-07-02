Scriptname GordianKnotNative Hidden
{ Native (SKSE / GordianKnot.dll) function surface for the Gordian Knot mod.

  All functions are Global Native and are registered by the C++ plugin at load
  (see src/Papyrus/GKNative.cpp). Call them as GordianKnotNative.FunctionName(...).

  Resources (cells / patrol markers / furniture) are referred to by an opaque
  Int "handle" that is stable across saves. handle 0 == invalid / not found.

  -- Role bit flags (used by the role functions; combine them together) --
    0 = None
    1 = Wanderer   (just wanders; GLOBAL -- not tied to any labyrinth)
    2 = Warden     (dungeon-keeping duties; SCOPED to a labyrinth)
    4 = Prisoner   (held in a labyrinth; SCOPED to a labyrinth)
  An actor may hold any combination (e.g. Wanderer + Warden of A).

  Roles come in two kinds:
   * GLOBAL (Wanderer) is a plain attribute of the actor. Its functions take NO
     labyrinth (SetWanderer/ClearWanderer/IsWanderer(akActor)).
   * SCOPED (Warden, Prisoner) is an ASSOCIATION with a specific labyrinth (its
     anchor reference), so an actor can be a Warden of A while a Prisoner of B.
     Their functions take the labyrinth anchor. Clearing a scoped role in one
     labyrinth leaves the actor's roles in every other labyrinth untouched.
  The generic per-labyrinth functions (SetActorRoles / AddActorRole /
  RemoveActorRole / GetActorRoles) operate on SCOPED bits only; a global bit in
  the mask is ignored -- use the Wanderer functions for that.

  Any role mutator (including the Set/Clear helpers) also TRACKS the actor if it
  wasn't already; AddActor(akActor) tracks one with no roles up front.

  Papyrus has no bitwise operators. Because the flags are distinct bits you can
  combine them by ADDING (1 + 4 = 5), or use SKSE's Math.LogicalOr / LogicalAnd
  for general masking. The per-role Set/Clear/Is* helpers below mean you rarely
  need to build a mask by hand. The Role*() getters return the flag values so you
  never hardcode them (e.g. GordianKnotNative.RolePrisoner()).

  -- Status --
    0 = Idle. Any other Int is a "busy" code; Papyrus owns that vocabulary.
    Status is GLOBAL to the actor (one thing at a time), not per-labyrinth.
}

; =============================================================================
; Actors  (the player and tracked NPCs are handled identically here)
; =============================================================================

; Start tracking akActor with no roles (idle). No-op if already tracked. Every role
; mutator below also tracks the actor, so this is only needed to track one up front.
Function AddActor(Actor akActor) Global Native

; --- Scoped roles (Warden / Prisoner): take the labyrinth anchor ---

; Replace the actor's SCOPED role bitmask in akLabyrinth outright (Warden/Prisoner
; bits only; global bits are ignored). Passing 0 drops the association with it.
Function SetActorRoles(Actor akActor, ObjectReference akLabyrinth, Int aiRoles) Global Native

; OR a scoped role flag into the actor's mask for akLabyrinth (global bits ignored).
Function AddActorRole(Actor akActor, ObjectReference akLabyrinth, Int aiRole) Global Native

; Clear a scoped role flag from the actor's mask for akLabyrinth (other labyrinths untouched).
Function RemoveActorRole(Actor akActor, ObjectReference akLabyrinth, Int aiRole) Global Native

; The actor's scoped role bitmask in akLabyrinth (0 if it holds no role there).
Int Function GetActorRoles(Actor akActor, ObjectReference akLabyrinth) Global Native

; Scoped convenience tests / set / clear, for one labyrinth. Other roles -- in this
; and every other labyrinth -- are left intact.
Bool Function IsWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Bool Function IsPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native
Function SetWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Function ClearWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Function SetPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native
Function ClearPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native

; --- Global roles (Wanderer): no labyrinth ---

; The actor's global (non-labyrinth) role bitmask.
Int Function GetActorGlobalRoles(Actor akActor) Global Native

Bool Function IsWanderer(Actor akActor) Global Native
Function SetWanderer(Actor akActor) Global Native
Function ClearWanderer(Actor akActor) Global Native

; Role-flag constant getters (single source of truth in the native plugin). Use
; these instead of hardcoding 1 / 2 / 4 when building masks for the role functions.
Int Function RoleWanderer() Global Native
Int Function RoleWarden() Global Native
Int Function RolePrisoner() Global Native

; Set / get an actor's status code (0 = Idle; other values are Papyrus-defined).
; Status is global to the actor, not scoped to a labyrinth.
Function SetActorStatus(Actor akActor, Int aiStatus) Global Native
Int Function GetActorStatus(Actor akActor) Global Native

; Tracked actors whose SCOPED role mask in akLabyrinth matches ANY bit in aiRoleMask.
Actor[] Function GetActorsByRole(ObjectReference akLabyrinth, Int aiRoleMask) Global Native

; Tracked actors whose GLOBAL role mask matches ANY bit in aiRoleMask (e.g. Wanderers).
Actor[] Function GetActorsByGlobalRole(Int aiRoleMask) Global Native

; Every labyrinth (anchor reference) in which akActor holds any scoped role.
ObjectReference[] Function GetActorLabyrinths(Actor akActor) Global Native

; Labyrinths (anchor references) where akActor's scoped role mask matches ANY bit in aiRoleMask.
ObjectReference[] Function GetLabyrinthsByActorRole(Actor akActor, Int aiRoleMask) Global Native

; True if akActor holds any role matching aiRoleMask -- globally OR in any labyrinth
; (so it works for both Wanderer and the scoped roles).
Bool Function HasRoleAnywhere(Actor akActor, Int aiRoleMask) Global Native

; Tracked actors whose status equals aiStatus.
Actor[] Function GetActorsByStatus(Int aiStatus) Global Native

; Drop an actor from native tracking entirely (all labyrinth associations + status).
Function ForgetActor(Actor akActor) Global Native

; =============================================================================
; Configuration & labyrinth lifecycle
; =============================================================================

; Supply the linked-ref "type" keywords used by discovery. Call once per session
; (the pointers aren't persisted; re-supply them after each load).
;  - cellDoor/patrolMarker/furniture/in/out identify resource references.
;  - warden identifies an ACTOR reference placed as a labyrinth's warden: on
;    ScanAllLabyrinths it is tracked with the Warden role for the labyrinth its
;    linked-ref (via akWarden) points at.
Function ConfigureKeywords(Keyword akCellDoor, Keyword akPatrolMarker, Keyword akFurniture, Keyword akInMarker, Keyword akOutMarker, Keyword akWarden) Global Native

; Register a labyrinth, identified directly by its anchor XMarker reference. Only
; stores the anchor's FormID, so the anchor's cell need not be loaded. The same
; anchor reference is what you pass to GetCells / GetMarkers / GetFurnitures.
Function RegisterLabyrinth(ObjectReference akAnchor) Global Native

; One-shot global discovery across ALL registered labyrinths. Sweeps the whole
; form table, so it finds resources WITHOUT their cells being loaded -- but only
; PERSISTENT references (doors / patrol markers / furniture must be flagged
; persistent in the CK). Call once after registration. Returns total matched.
Int Function ScanAllLabyrinths() Global Native

; True if akRef is a persistent reference (survives cell reset, is simulated off-screen,
; and is the only kind ScanAllLabyrinths can find while its cell is unloaded). Diagnostic
; helper -- reads the reference's persistent flag directly from the engine.
Bool Function IsPersistent(ObjectReference akRef) Global Native

; =============================================================================
; Cells  (aiCell is a cell handle; akLabyrinth is the anchor reference)
; =============================================================================

; All cell handles belonging to a labyrinth (its anchor reference).
Int[] Function GetCells(ObjectReference akLabyrinth) Global Native

ObjectReference Function GetCellDoor(Int aiCell) Global Native
ObjectReference Function GetCellInMarker(Int aiCell) Global Native
ObjectReference Function GetCellOutMarker(Int aiCell) Global Native

Int Function GetCellMaxOccupants(Int aiCell) Global Native
Function SetCellMaxOccupants(Int aiCell, Int aiMax) Global Native

; The labyrinth (anchor reference) a cell belongs to (None if handle unknown).
ObjectReference Function GetCellLabyrinth(Int aiCell) Global Native

; =============================================================================
; Patrol markers  (aiMarker is a marker handle)
; =============================================================================

Int[] Function GetMarkers(ObjectReference akLabyrinth) Global Native

ObjectReference Function GetMarkerRef(Int aiMarker) Global Native

Int Function GetMarkerMaxOccupants(Int aiMarker) Global Native
Function SetMarkerMaxOccupants(Int aiMarker, Int aiMax) Global Native

ObjectReference Function GetMarkerLabyrinth(Int aiMarker) Global Native

; =============================================================================
; Furniture  (aiFurniture is a furniture handle; always single-occupant)
; =============================================================================

Int[] Function GetFurnitures(ObjectReference akLabyrinth) Global Native

ObjectReference Function GetFurnitureRef(Int aiFurniture) Global Native

ObjectReference Function GetFurnitureLabyrinth(Int aiFurniture) Global Native
