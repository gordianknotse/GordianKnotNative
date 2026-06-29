Scriptname GordianKnotNative Hidden
{ Native (SKSE / GordianKnot.dll) function surface for the Gordian Knot mod.

  All functions are Global Native and are registered by the C++ plugin at load
  (see src/Papyrus/GKNative.cpp). Call them as GordianKnotNative.FunctionName(...).

  Resources (cells / patrol markers / furniture) are referred to by an opaque
  Int "handle" that is stable across saves. handle 0 == invalid / not found.

  -- Role bit flags (used by the role functions; OR them together) --
    0 = None
    1 = Wanderer   (wanders the labyrinth)
    2 = Warden     (dungeon-keeping duties)
  An actor may hold both (3 = Wanderer + Warden).

  -- Status --
    0 = Idle. Any other Int is a "busy" code; Papyrus owns that vocabulary.
}

; =============================================================================
; Actors  (the player and tracked NPCs are handled identically here)
; =============================================================================

; Replace an actor's role bitmask outright (see role flags above).
Function SetActorRoles(Actor akActor, Int aiRoles) Global Native

; OR a role flag into an actor's mask.
Function AddActorRole(Actor akActor, Int aiRole) Global Native

; Clear a role flag from an actor's mask.
Function RemoveActorRole(Actor akActor, Int aiRole) Global Native

; Current role bitmask (0 if the actor isn't tracked).
Int Function GetActorRoles(Actor akActor) Global Native

; Convenience role tests.
Bool Function IsWanderer(Actor akActor) Global Native
Bool Function IsWarden(Actor akActor) Global Native

; Set / get an actor's status code (0 = Idle; other values are Papyrus-defined).
Function SetActorStatus(Actor akActor, Int aiStatus) Global Native
Int Function GetActorStatus(Actor akActor) Global Native

; Tracked actors matching ANY bit in aiRoleMask.
Actor[] Function GetActorsByRole(Int aiRoleMask) Global Native

; Tracked actors whose status equals aiStatus.
Actor[] Function GetActorsByStatus(Int aiStatus) Global Native

; Drop an actor from native tracking entirely.
Function ForgetActor(Actor akActor) Global Native

; =============================================================================
; Configuration & labyrinth lifecycle
; =============================================================================

; Supply the five linked-ref "type" keywords used by discovery. Call once per
; session (the pointers aren't persisted; re-supply them after each load).
Function ConfigureKeywords(Keyword akCellDoor, Keyword akPatrolMarker, Keyword akFurniture, Keyword akInMarker, Keyword akOutMarker) Global Native

; Register a labyrinth: its identifying Keyword plus the anchor XMarker that
; gives it a position. Only stores the anchor's FormID, so the anchor's cell
; need not be loaded.
Function RegisterLabyrinth(Keyword akLabyrinth, ObjectReference akAnchor) Global Native

; One-shot global discovery across ALL registered labyrinths. Sweeps the whole
; form table, so it finds resources WITHOUT their cells being loaded -- but only
; PERSISTENT references (doors / patrol markers / furniture must be flagged
; persistent in the CK). Call once after registration. Returns total matched.
Int Function ScanAllLabyrinths() Global Native

; The anchor reference a labyrinth was registered with (None if unregistered).
ObjectReference Function GetLabyrinthAnchor(Keyword akLabyrinth) Global Native

; =============================================================================
; Cells  (aiCell is a cell handle)
; =============================================================================

; All cell handles belonging to a labyrinth.
Int[] Function GetCells(Keyword akLabyrinth) Global Native

ObjectReference Function GetCellDoor(Int aiCell) Global Native
ObjectReference Function GetCellInMarker(Int aiCell) Global Native
ObjectReference Function GetCellOutMarker(Int aiCell) Global Native

Int Function GetCellMaxOccupants(Int aiCell) Global Native
Function SetCellMaxOccupants(Int aiCell, Int aiMax) Global Native

; The labyrinth keyword a cell belongs to (None if the handle is unknown).
Keyword Function GetCellLabyrinth(Int aiCell) Global Native

; =============================================================================
; Patrol markers  (aiMarker is a marker handle)
; =============================================================================

Int[] Function GetMarkers(Keyword akLabyrinth) Global Native

ObjectReference Function GetMarkerRef(Int aiMarker) Global Native

Int Function GetMarkerMaxOccupants(Int aiMarker) Global Native
Function SetMarkerMaxOccupants(Int aiMarker, Int aiMax) Global Native

Keyword Function GetMarkerLabyrinth(Int aiMarker) Global Native

; =============================================================================
; Furniture  (aiFurniture is a furniture handle; always single-occupant)
; =============================================================================

Int[] Function GetFurnitures(Keyword akLabyrinth) Global Native

ObjectReference Function GetFurnitureRef(Int aiFurniture) Global Native

Keyword Function GetFurnitureLabyrinth(Int aiFurniture) Global Native
