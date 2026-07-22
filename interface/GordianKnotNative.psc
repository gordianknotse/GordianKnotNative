Scriptname GordianKnotNative Hidden
{Native (SKSE / GordianKnot.dll) function surface for the Gordian Knot mod. See the comment block below.}

; =============================================================================
; IMPORTANT: keep the { } docstring above to ONE SHORT LINE. Doc comments are
; compiled INTO the .pex, and the CK / game script loader reads them into a
; fixed-size buffer -- a long one crashes both ("Being told to read in more
; than our buffer will hold"). Plain ; comments are stripped by the compiler.
; =============================================================================
;
; All functions are Global Native and are registered by the C++ plugin at load
; (see src/Papyrus/GKNative.cpp). Call them as GordianKnotNative.FunctionName(...).
;
; Resources (cells / patrol markers / furniture) are referred to by an opaque
; Int "handle" that is stable across saves. handle 0 == invalid / not found.
;
; -- Role bit flags (used by the role functions; combine them together) --
;   0 = None
;   1 = Wanderer   (just wanders; GLOBAL -- not tied to any labyrinth)
;   2 = Warden     (dungeon-keeping duties; SCOPED to a labyrinth)
;   4 = Prisoner   (held in a labyrinth; SCOPED to a labyrinth)
; An actor may hold any combination (e.g. Wanderer + Warden of A).
;
; Roles come in two kinds:
;  * GLOBAL (Wanderer) is a plain attribute of the actor. Its functions take NO
;    labyrinth (SetWanderer/ClearWanderer/IsWanderer(akActor)).
;  * SCOPED (Warden, Prisoner) is an ASSOCIATION with a specific labyrinth (its
;    anchor reference), so an actor can be a Warden of A while a Prisoner of B.
;    Their functions take the labyrinth anchor. Clearing a scoped role in one
;    labyrinth leaves the actor's roles in every other labyrinth untouched.
; The generic per-labyrinth functions (SetActorRoles / AddActorRole /
; RemoveActorRole / GetActorRoles) operate on SCOPED bits only; a global bit in
; the mask is ignored -- use the Wanderer functions for that.
;
; -- Tracking & persistence --
; A tracked actor needs persistence (to survive unload / cell reset / discovery)
; AND its per-NPC driver script. EVERY new actor gets both the same way: it is
; placed into a free GkNpc pool alias on the quest given to ConfigureAliasQuest
; (the driver script is attached to the pool aliases in the CK -- never to actor
; refs or bases). This happens automatically in every ADDING mutator (AddActor,
; Set*/Add* role, SetActorStatus) AND for wardens discovered by
; ScanAllLabyrinths, even for an actor that is already persistent (it still
; needs the script). If no slot is free (or no alias quest is configured) the
; mutator returns FALSE / the scan skips the warden, and the actor is NOT
; tracked. CLEARING mutators (Remove*/Clear*) never track: clearing a role on an
; untracked actor is a no-op.
; The driver script (extends ReferenceAlias) gets explicit lifecycle hooks,
; dispatched natively (declare only what you need; plain Functions, not Events):
;   Function OnGKAssign(Actor akActor)   ; after the alias is force-filled
;   Function OnGKRelease(Actor akActor)  ; from ForgetActor, before the alias is
;                                        ; cleared -- keep it quick / non-latent
;   Function OnGKRoleApplied(Actor akActor, ObjectReference akLabyrinth, Int aiRole)
;                                        ; fires once the role's FACTION change
;                                        ; has COMPLETED (native completion
;                                        ; callback on the AddToFaction call; one
;                                        ; call per added role bit). The adders
;                                        ; return before the faction applies (it
;                                        ; goes through the VM), so anything that
;                                        ; depends on the new faction --
;                                        ; StopCombat after a Capture, say --
;                                        ; belongs here, not after the adder
;                                        ; call. Fires immediately if the role
;                                        ; has no faction. Role removals fire no
;                                        ; hook.
; Do NOT use OnInit as the constructor: it fires when the script INSTANCE is
; created -- at quest start, with the alias still empty -- not when the alias
; fills, and nothing fires on clear. Guard any OnInit logic against
; GetActorReference() == None.
;
; Papyrus has no bitwise operators. Because the flags are distinct bits you can
; combine them by ADDING (1 + 4 = 5), or use SKSE's Math.LogicalOr / LogicalAnd
; for general masking. The per-role Set/Clear/Is* helpers below mean you rarely
; need to build a mask by hand. The Role*() getters return the flag values so you
; never hardcode them (e.g. GordianKnotNative.RolePrisoner()).
;
; -- Status --
;   A free-form String; Papyrus owns that vocabulary, no enums or int codes
;   needed. Newly tracked actors start as "idle", and an empty String ("") is
;   treated as "idle" everywhere a status is accepted. Compared
;   case-insensitively (like Papyrus string compares). Status is GLOBAL to the
;   actor (one thing at a time), not per-labyrinth.

; =============================================================================
; Actors  (the player and tracked NPCs are handled identically here)
; =============================================================================

; Start tracking akActor with no roles (idle). No-op (True) if already tracked.
; Like every adding mutator below, returns False if the actor couldn't be made
; persistent (see the header) -- in that case it is not tracked at all.
Bool Function AddActor(Actor akActor) Global Native

; --- Scoped roles (Warden / Prisoner): take the labyrinth anchor ---

; Replace the actor's SCOPED role bitmask in akLabyrinth outright (Warden/Prisoner
; bits only; global bits are ignored). Passing 0 drops the association with it
; (a pure clear: never tracks, always True). False if the actor can't be tracked.
Bool Function SetActorRoles(Actor akActor, ObjectReference akLabyrinth, Int aiRoles) Global Native

; OR a scoped role flag into the actor's mask for akLabyrinth (global bits ignored).
; False if the actor can't be tracked.
Bool Function AddActorRole(Actor akActor, ObjectReference akLabyrinth, Int aiRole) Global Native

; Clear a scoped role flag from the actor's mask for akLabyrinth (other labyrinths
; untouched). Clearing never tracks an untracked actor.
Function RemoveActorRole(Actor akActor, ObjectReference akLabyrinth, Int aiRole) Global Native

; The actor's scoped role bitmask in akLabyrinth (0 if it holds no role there).
Int Function GetActorRoles(Actor akActor, ObjectReference akLabyrinth) Global Native

; Scoped convenience tests / set / clear, for one labyrinth. Other roles -- in this
; and every other labyrinth -- are left intact. Set* returns False if the actor
; can't be tracked (see the header). For the Is* tests, pass akLabyrinth = None
; to test the role in ANY labyrinth (like GetActorsByRole); Set*/Clear* still
; require a specific labyrinth.
Bool Function IsWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Bool Function IsPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native
Bool Function SetWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Function ClearWarden(Actor akActor, ObjectReference akLabyrinth) Global Native
Bool Function SetPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native
Function ClearPrisoner(Actor akActor, ObjectReference akLabyrinth) Global Native

; Imprison akActor in the labyrinth akWarden keeps: resolves the labyrinth from
; akWarden's Warden role, then acts exactly like SetPrisoner(akActor, it) --
; same tracking gate and faction sync. False WITHOUT effect if akActor is
; already a prisoner ANYWHERE (two wardens racing for the same actor: exactly
; one Capture wins), if akWarden wardens no labyrinth, or if akActor can't be
; tracked. If akWarden somehow wardens several labyrinths, the first is used (a
; warning is logged).
; NOTE: returns BEFORE the prisoner-faction change lands (it goes through the
; VM). Put combat-stopping / pose logic in the driver script's OnGKRoleApplied
; (see the header), which fires once the faction is really applied.
Bool Function Capture(Actor akWarden, Actor akActor) Global Native

; --- Global roles (Wanderer): no labyrinth ---

; The actor's global (non-labyrinth) role bitmask.
Int Function GetActorGlobalRoles(Actor akActor) Global Native

Bool Function IsWanderer(Actor akActor) Global Native
; False if the actor can't be tracked (see the header).
Bool Function SetWanderer(Actor akActor) Global Native
Function ClearWanderer(Actor akActor) Global Native

; Role-flag constant getters (single source of truth in the native plugin). Use
; these instead of hardcoding 1 / 2 / 4 when building masks for the role functions.
Int Function RoleWanderer() Global Native
Int Function RoleWarden() Global Native
Int Function RolePrisoner() Global Native

; Human-readable summary of every role akActor holds -- global (Wanderer) plus
; scoped roles held in ANY labyrinth -- as "Wanderer" / "Wanderer, Warden" / etc.
; Empty string if the actor holds no role (or is untracked). Pure-Papyrus helper
; composed from the natives above (debug/UI convenience, not perf-critical).
String Function GetActorRolesAsString(Actor akActor) Global
    String s = ""
    If IsWanderer(akActor)
        s = "Wanderer"
    EndIf
    If HasRoleAnywhere(akActor, RoleWarden())
        If s != ""
            s += ", "
        EndIf
        s += "Warden"
    EndIf
    If HasRoleAnywhere(akActor, RolePrisoner())
        If s != ""
            s += ", "
        EndIf
        s += "Prisoner"
    EndIf
    Return s
EndFunction

; Set / get an actor's status: a free-form String (see the header; "idle" is
; the default and "" is treated as "idle"). Status is global to the actor, not
; scoped to a labyrinth. Setting status on an untracked actor is an ADDER:
; False if the actor can't be tracked (see the header). GetActorStatus reads
; back with the casing it was set with.
Bool Function SetActorStatus(Actor akActor, String asStatus) Global Native
String Function GetActorStatus(Actor akActor) Global Native

; Put akActor back to the "idle" status. Clearing never tracks: on an
; untracked actor this is a no-op.
Function ClearActorStatus(Actor akActor) Global Native

; True if akActor's status equals asStatus (compared case-insensitively).
; An untracked actor's status is "idle"; a None actor always returns False.
Bool Function IsActorStatus(Actor akActor, String asStatus) Global Native

; IsActorStatus(akActor, "idle") shorthand.
Bool Function IsActorIdle(Actor akActor) Global Native

; Atomically claim an idle actor: finds a tracked actor whose status is "idle"
; and who is CALM -- alive, not in combat, and not searching for an enemy
; (suspicious) -- sets its status to asNewStatus, and returns it. The find, the
; combat check, and the transition all happen under one native lock, so two
; scripts can never claim the same actor. The player is never returned.
; None if no idle, calm actor is available.
Actor Function GetIdleActorAndTransitionTo(String asNewStatus) Global Native

; Claim from a queue for a SPECIFIC idle actor: if akActor's status is "idle"
; AND asQueue yields a live actor, transition akActor to asNewStatus and
; return the dequeued actor -- atomically, under one native lock. Returns None
; -- and changes NOTHING (the queue keeps its entries) -- when akActor is None
; or not idle, when the queue is empty (or holds only stale entries), or when
; akActor could not be tracked (transitioning is an ADDER, see the header).
Actor Function TransitionIdleActorToAndDequeue(Actor akActor, String asQueue, String asNewStatus) Global Native

; Like TransitionIdleActorToAndDequeue, but the idle actor is CHOSEN, not given:
; among the idle, calm actors holding aiRole (a Role* flag) for akLabyrinth --
; Wanderer, being global, matches no matter what akLabyrinth is; for the scoped
; roles pass None to mean ANY labyrinth -- picks the one CLOSEST (3D distance)
; to asQueue's next live actor, transitions it to asNewStatus, pops that queue
; entry, and returns a 2-element array: [0] = the claimed idle actor, [1] = the
; dequeued actor. Atomic under one native lock, so two scripts can never claim
; the same actor or queue entry. Returns an EMPTY array -- and changes NOTHING
; (the queue keeps its entries) -- when the queue is empty (or holds only stale
; entries) or no matching idle actor exists. The queued actor never claims
; itself; the player is never claimed.
Actor[] Function TransitionClosestIdleActorToAndDequeue(ObjectReference akLabyrinth, Int aiRole, String asQueue, String asNewStatus) Global Native

; Tracked actors whose SCOPED role mask in akLabyrinth matches ANY bit in aiRoleMask.
; Pass akLabyrinth = None to match the role in ANY labyrinth (each actor listed once).
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

; Tracked actors whose status equals asStatus (compared case-insensitively).
Actor[] Function GetActorsByStatus(String asStatus) Global Native

; Drop an actor from native tracking entirely (all labyrinth associations + status).
; Also releases the GkNpc pool alias holding it, if any (freeing the slot): the
; driver script's OnGKRelease(akActor) is called first (if declared -- see the
; header), then the alias is cleared.
Function ForgetActor(Actor akActor) Global Native

; =============================================================================
; Actor queues  (asQueue is a free-form queue name)
; =============================================================================
; Named FIFO queues of actors. Any String mints a queue on first use, so plugins
; can define their own queues without enums or int mappings. Names are
; case-insensitive (like Papyrus string compares). Queues are independent of
; tracking/roles -- enqueueing does NOT track the actor or take a pool alias --
; and their contents persist across save/load.

; Append akActor to the back of asQueue. False if akActor is None, asQueue is
; empty (""), or the actor is already waiting in that queue -- an actor sits in
; a given queue at most once, but may wait in any number of DIFFERENT queues.
;
; afDelaySeconds > 0 schedules the enqueue instead: the actor joins the back of
; the queue once that many seconds of UNPAUSED gameplay have elapsed (menus and
; the console pause the countdown; it is real-time otherwise, NOT scaled by the
; game timescale). Also False if the actor is already scheduled for that queue;
; if it is waiting in the queue by the time the delay expires, the delayed
; entry just evaporates. Delayed entries persist across save/load (remaining
; time preserved) and are dropped by ClearQueue.
Bool Function EnqueueActor(String asQueue, Actor akActor, Float afDelaySeconds = 0.0) Global Native

; With akActor = None: pop and return the actor at the front of asQueue, or
; None if it is empty. Entries that no longer resolve (actor deleted / its
; plugin removed) are silently skipped, so the next live actor in line comes
; out. With akActor given: remove THAT actor from asQueue -- wherever it sits,
; including a still-scheduled delayed entry (see EnqueueActor) -- and return
; it if it was there, None otherwise.
Actor Function DequeueActor(String asQueue, Actor akActor = None) Global Native

; The actor at the front of asQueue WITHOUT removing it, or None if the queue
; is empty. Stale entries in front of it are dropped exactly as a dequeue
; would drop them, so what PeekActor returns is what DequeueActor would pop.
Actor Function PeekActor(String asQueue) Global Native

; The actor at the BACK of asQueue (the one enqueued last) WITHOUT removing
; it, or None if the queue is empty. Stale entries behind it are dropped the
; same way DequeueActor drops stale front entries.
Actor Function PeekLastActor(String asQueue) Global Native

; The aiIndex-th actor of asQueue (0 = front) WITHOUT removing it; None once
; aiIndex runs past the end (or is negative). Stale entries are dropped as
; they are encountered, so indices range over LIVE actors only: a browse loop
; (0, 1, 2, ... until None) enumerates exactly the queue's contents.
Actor Function PeekActorAt(String asQueue, Int aiIndex) Global Native

; The neighbors of asQueue's aiIndex-th actor, as a 2-element array:
; [0] = the actor just before it (towards the FRONT; None when aiIndex is 0),
; [1] = the actor just after it (towards the BACK; None when aiIndex is the
; last index). Both None when aiIndex is negative or the queue is empty or
; unknown. Same index space as PeekActorAt (live actors; stale entries are
; dropped), and both neighbors are read atomically.
Actor[] Function PeekAdjacentActorsAt(String asQueue, Int aiIndex) Global Native

; PeekAdjacentActorsAt keyed by the actor instead of its index: the neighbors
; of akActor in asQueue ([0] = just before it, towards the FRONT; [1] = just
; after it, towards the BACK; None past either end). Both None when akActor is
; None or not in the queue. Stale entries are dropped, and both neighbors are
; read atomically.
Actor[] Function PeekAdjacentActors(String asQueue, Actor akActor) Global Native

; Snapshot of asQueue's live actors, front to back (empty array if the queue
; is empty or unknown). Stale entries are dropped like PeekActorAt, so the
; array is exactly what successive dequeues would hand out, in order. Build
; menus from this array and index back into it after the pick -- the queue
; can change while a menu is open.
Actor[] Function GetQueueActors(String asQueue) Global Native

; How many actors are waiting in asQueue (0 if it was never used or is drained).
Int Function GetQueueSize(String asQueue) Global Native

; Empty asQueue, dropping every waiting actor.
Function ClearQueue(String asQueue) Global Native

; =============================================================================
; Actor attributes  (asKey is a free-form attribute name)
; =============================================================================
; Per-actor key/value store: any String key holds one value per actor, so
; plugins can define their own attributes without enums or int mappings. Two
; independent stores (the same key names DIFFERENT attributes in each):
; ObjectReference values and Int values. Keys are case-insensitive (like
; Papyrus string compares). Attributes are independent of tracking/roles --
; setting one does NOT track the actor or take a pool alias -- and they
; persist across save/load.

; Set akActor's asKey attribute to akValue, overwriting any previous value.
; Pass akValue = None to clear the attribute. No-op if akActor is None or asKey
; is empty ("").
Function SetActorAttribute(Actor akActor, String asKey, ObjectReference akValue) Global Native

; akActor's asKey attribute, or None if it was never set, was cleared, or the
; stored reference no longer resolves (deleted / its plugin removed).
ObjectReference Function GetActorAttribute(Actor akActor, String asKey) Global Native

; Remove akActor's asKey attribute (no-op if it was never set). Equivalent to
; SetActorAttribute(akActor, asKey, None).
Function ClearActorAttribute(Actor akActor, String asKey) Global Native

; Set akActor's asKey Int attribute to aiValue, overwriting any previous
; value. EVERY value is stored -- 0 included -- so clearing goes through
; ClearActorIntAttribute, not a sentinel. No-op if akActor is None or asKey
; is empty ("").
Function SetActorIntAttribute(Actor akActor, String asKey, Int aiValue) Global Native

; akActor's asKey Int attribute, or aiDefault if it was never set or cleared.
Int Function GetActorIntAttribute(Actor akActor, String asKey, Int aiDefault = 0) Global Native

; Remove akActor's asKey Int attribute (no-op if it was never set).
Function ClearActorIntAttribute(Actor akActor, String asKey) Global Native

; Set akActor's asKey ARRAY attribute (a third store; the same key names
; different attributes in each). Values are stored by FormID, so any form
; types may mix in one array. POSITIONAL: order is preserved and None
; entries are stored as real slots (indices are stable -- see the *Index
; functions). An EMPTY array clears the attribute. NOTE Papyrus array types
; are invariant: declare your variable as Form[] -- an Actor[] cannot be
; passed here directly, copy it into a Form[] first.
Function SetActorArrayAttribute(Actor akActor, String asKey, Form[] akValues) Global Native

; akActor's asKey array attribute, or an empty array if it was never set or
; cleared; otherwise the SAME LENGTH as stored, with None at every empty
; slot and at entries that no longer resolve (deleted / plugin removed).
; Cast per element on the way out (e.g. arr[i] as Actor).
Form[] Function GetActorArrayAttribute(Actor akActor, String asKey) Global Native

; Remove akActor's asKey array attribute (no-op if it was never set).
; Equivalent to SetActorArrayAttribute with an empty array.
Function ClearActorArrayAttribute(Actor akActor, String asKey) Global Native

; Set position aiIndex (0-indexed) of akActor's asKey array attribute. An
; index past the current end EXTENDS the array, filling the gap with None:
; on a 2-element array, setting index 4 makes it 5 long with indices 2 and 3
; None. Setting on a nonexistent attribute creates it. akValue = None stores
; a None slot (does NOT shrink the array). Indices outside [0, 1023] are
; ignored (safety cap).
Function SetActorArrayAttributeIndex(Actor akActor, String asKey, Int aiIndex, Form akValue) Global Native

; The form at position aiIndex of akActor's asKey array attribute; None when
; the index is out of bounds, the slot is empty, or the entry no longer
; resolves.
Form Function GetActorArrayAttributeIndex(Actor akActor, String asKey, Int aiIndex) Global Native

; =============================================================================
; Armor scans
; =============================================================================

; Every loaded Armor form bearing akKeyword, optionally filtered to those
; whose display name CONTAINS asSearchText (case-insensitive, like Papyrus
; string compares; "" = no name filter, and armors with no display name never
; match a non-empty search). The result is built natively, so it is NOT
; subject to the 128-element cap of script-created arrays.
;
; The two keyword ARRAYS filter on the Devious Devices RENDERED armor of each
; candidate: it must bear ALL of akRenderedKeywords AND NONE of
; akRenderedExcludeKeywords. When either array is non-empty, armors with no
; rendered device (not a DD inventory device) are excluded. Omit (or pass
; None for) an array you don't need; both omitted = no rendered filtering
; at all. None
; entries INSIDE an array are ignored (with a log warning). The rendered
; lookup goes through DD NG's native database (DeviousDevices.dll, an
; OPTIONAL runtime dependency resolved on first use) -- the Papyrus VM cannot
; read script properties off un-instantiated base forms, and DD NG parses
; them from the plugins instead. Without DeviousDevices.dll the rendered
; filter matches nothing (a warning lands in the log). E.g.:
;   Keyword[] req = new Keyword[1]
;   req[0] = zad_DeviousBelt
;   Keyword[] excl = new Keyword[1]
;   excl[0] = zad_BlockGeneric
;   GetArmorsWithKeyword(zad_InventoryDevice, "", req, excl)
;     -> every inventory device whose rendered device is a belt and is not
;        flagged generic-blocked.
Armor[] Function GetArmorsWithKeyword(Keyword akKeyword, String asSearchText = "", Keyword[] akRenderedKeywords = None, Keyword[] akRenderedExcludeKeywords = None) Global Native

; =============================================================================
; Animation registries  (asRegistry is a free-form registry name)
; =============================================================================
; Named, weighted pools of animation names: any String mints a registry on
; first use, so plugins can define their own pools without enums or int
; mappings (names are case-insensitive, like Papyrus string compares).
; SESSION state, like the keyword config -- NOT saved: re-Add your animations
; after each game load (AddAnimation is idempotent, so just re-run the setup).

; Add asAnimation to asRegistry with the given draw weight, or UPDATE its
; weight if it is already in there (re-registering converges instead of
; accumulating). Weights are relative: an entry's chance is its weight divided
; by the registry's total. False if either name is empty ("") or afWeight <= 0
; (nothing is added).
Bool Function AddAnimation(String asRegistry, String asAnimation, Float afWeight = 1.0) Global Native

; A weighted-random animation name from asRegistry, or "" if the registry was
; never used.
String Function GetAnimation(String asRegistry) Global Native

; =============================================================================
; Configuration & labyrinth lifecycle
; =============================================================================

; Supply the linked-ref "type" keywords used by discovery. Call once per session
; (the pointers aren't persisted; re-supply them after each load).
;  - cellDoor/patrolMarker/furniture/in/out identify resource references.
;  - warden identifies an ACTOR reference placed as a labyrinth's warden: on
;    ScanAllLabyrinths it is tracked with the Warden role for the labyrinth its
;    linked-ref (via akWarden) points at.
;  - wanderer identifies an ACTOR reference with the global Wanderer role: it
;    belongs to no labyrinth, so its linked-ref (via akWanderer) may point at ANY
;    reference -- the target is ignored. The linked ref is still required: it
;    marks the role AND makes the ref persistent in the ESP, which is what lets
;    the scan find it while its cell is unloaded.
; An actor reference may carry both role linked-refs; it gets both roles.
Function ConfigureKeywords(Keyword akCellDoor, Keyword akPatrolMarker, Keyword akFurniture, Keyword akInMarker, Keyword akOutMarker, Keyword akWarden, Keyword akWanderer) Global Native

; Register a labyrinth, identified directly by its anchor XMarker reference. Only
; stores the anchor's FormID, so the anchor's cell need not be loaded. The same
; anchor reference is what you pass to GetCells / GetMarkers / GetFurnitures.
;
; akWardenFaction / akPrisonerFaction are the labyrinth's role factions: an actor
; JOINS the matching faction whenever it gains that role here (Set*/Add* mutators
; AND scan discovery), and LEAVES it when the role is cleared (Remove*/Clear*,
; SetActorRoles with the bit dropped, ForgetActor) -- unless another labyrinth
; where the actor still holds a role grants the same faction. Factions may be
; shared across labyrinths; pass None to disable faction sync for that role.
; Like the keywords, they're live session pointers: re-register after each load.
Function RegisterLabyrinth(ObjectReference akAnchor, Faction akWardenFaction, Faction akPrisonerFaction) Global Native

; Every registered labyrinth (its anchor reference). Anchors whose FormID no
; longer resolves are dropped from the result.
ObjectReference[] Function GetLabyrinths() Global Native

; One-shot global discovery across ALL registered labyrinths. Sweeps the whole
; form table, so it finds resources WITHOUT their cells being loaded -- but only
; PERSISTENT references (doors / patrol markers / furniture must be flagged
; persistent in the CK). Discovered warden AND wanderer actors are tracked
; through the GkNpc alias pool like any other actor (see the header), so call
; ConfigureAliasQuest BEFORE this or none is tracked. Call once after
; registration. Returns total matched resources.
Int Function ScanAllLabyrinths() Global Native

; True if akRef is a persistent reference (survives cell reset, is simulated off-screen,
; and is the only kind ScanAllLabyrinths can find while its cell is unloaded). Diagnostic
; helper -- reads the reference's persistent flag directly from the engine.
Bool Function IsPersistent(ObjectReference akRef) Global Native

; Debug.SendAnimationEvent with the engine's verdict surfaced: True when the
; actor's behavior graph knows asEvent (the animation plays), False when it
; does not -- i.e. the animation pack is missing OR installed but its
; FNIS/Nemesis/Pandora patch was never generated. Use it for soft dependencies:
; try the pack's event, fall back (or tell the player to run the generator, if
; the pack's .esp IS loaded) on False. The actor's 3D must be loaded, or
; installed events also report False.
Bool Function TrySendAnimationEvent(Actor akActor, String asEvent) Global Native

; =============================================================================
; Alias pool  (per-NPC driver aliases, authored on one quest as GkNpcAlias000...)
; =============================================================================
; Pool aliases are the ReferenceAliases on the configured quest whose name starts
; with "GkNpc". The native layer fills one automatically whenever an ADDING
; mutator or ScanAllLabyrinths needs to track a NEW actor (see the header), and
; releases it on ForgetActor -- there is no manual assign/free.

; Supply the quest carrying the pool aliases. Call once per session alongside
; ConfigureKeywords, BEFORE ScanAllLabyrinths (a live pointer, re-supplied after
; each game load). Without it, adders fail (return False) for any new actor and
; the scan drops discovered wardens.
Function ConfigureAliasQuest(Quest akQuest) Global Native

; Diagnostics: the alias ID of the pool alias currently holding akActor (-1 if
; none), and how many pool aliases are currently empty and unreserved.
Int Function FindAliasHolding(Quest akQuest, Actor akActor) Global Native
Int Function CountFreeAliases(Quest akQuest) Global Native

; =============================================================================
; Cells  (aiCell is a cell handle; akLabyrinth is the anchor reference)
; =============================================================================
; Cell FLAGS: a String where each CHARACTER is one flag (a cell may carry many).
; Configured in the CK on the door's GordianKnotCellDoor script (String property
; `flags`, refreshed on every scan) or at runtime via SetCellFlags.
;
; FILTER SYNTAX (every asFilterString parameter, cells and furniture alike):
;   a       the resource has flag a      !a      does NOT have a
;   (ab!c)  ALL terms must hold          [ab!c]  at least ONE term must hold
;   X & Y   both sides must hold         X | Y   either side must hold
; '&' binds tighter than '|'. Groups hold only flags (no operators/nesting
; inside); combine groups with & and |, e.g. (ab) & [cd!e]. A bare run of
; flags outside a group means ANY of them, so "abc" = [abc]. Whitespace is
; ignored; matching is case-insensitive. "" matches everything; a MALFORMED
; filter matches NOTHING (a warning lands in the log).

; All cell handles belonging to a labyrinth (its anchor reference), optionally
; filtered (see the filter syntax in the header).
Int[] Function GetCells(ObjectReference akLabyrinth, String asFilterString = "") Global Native

ObjectReference Function GetCellDoor(Int aiCell) Global Native

; The handle of the cell whose door is akDoor, or 0 if akDoor is None or not a
; discovered cell door. Inverse of GetCellDoor; use it to turn the door ref
; delivered by the GK_OnActivateCellDoor mod event back into a cell handle.
Int Function GetCellByDoor(ObjectReference akDoor) Global Native
ObjectReference Function GetCellInMarker(Int aiCell) Global Native
ObjectReference Function GetCellOutMarker(Int aiCell) Global Native

Int Function GetCellMaxOccupants(Int aiCell) Global Native
Function SetCellMaxOccupants(Int aiCell, Int aiMax) Global Native

; How many prisoners currently occupy aiCell (0 if the handle is unknown).
Int Function GetCellOccupantCount(Int aiCell) Global Native

; Free spots left in aiCell (maxOccupants - occupants). 0 means full -- or
; the handle is unknown.
Int Function GetCellVacancy(Int aiCell) Global Native

; The aiIndex-th prisoner of aiCell (0 = first assigned, in assignment order),
; or None if aiIndex is out of range / the handle is unknown / that occupant
; no longer resolves. Read-only: never changes the cell's occupancy.
Actor Function GetCellOccupantAt(Int aiCell, Int aiIndex) Global Native

; Snapshot of aiCell's prisoners in assignment order (empty array if the
; handle is unknown). Occupants that no longer resolve are skipped in the
; result but left in the cell (read-only, like GetCellOccupantAt).
Actor[] Function GetCellOccupants(Int aiCell) Global Native

String Function GetCellFlags(Int aiCell) Global Native
Function SetCellFlags(Int aiCell, String asFlags) Global Native

; The labyrinth (anchor reference) a cell belongs to (None if handle unknown).
ObjectReference Function GetCellLabyrinth(Int aiCell) Global Native

; Assign akActor to a free cell (one with occupants < maxOccupants) of
; akLabyrinth ONLY, passing the flag filter -- picked at RANDOM (uniform among
; the matching cells); returns the claimed cell's handle. akLabyrinth must not
; be None (else 0, no cell assigned). If the actor already occupies a cell --
; in any labyrinth -- it is MOVED to the picked one (its current cell is never
; re-picked); when no cell of akLabyrinth matches it stays put, and its current
; cell's handle is returned only if that cell is in akLabyrinth (otherwise 0).
; Returns 0 when no cell was assigned in akLabyrinth, or when a new actor
; cannot be tracked (adder -- see the alias-pool header); on 0 nothing has
; changed.
Int Function AssignFreeCellToPrisoner(Actor akActor, ObjectReference akLabyrinth, String asFilterString = "") Global Native

; Assign the SPECIFIC cell aiCell to akActor (handles are unique across all
; labyrinths, so the cell alone identifies the target). False if the cell
; doesn't exist, is already full, or a new actor cannot be tracked (adder --
; see the alias-pool header); on False nothing has changed. True if akActor
; already occupies aiCell (no change). If akActor occupies another cell -- in
; any labyrinth -- it is MOVED to aiCell.
Bool Function AssignPrisonerToCell(Actor akActor, Int aiCell) Global Native

; The handle of the cell akActor is assigned to, or 0 if unassigned.
Int Function GetActorCell(Actor akActor) Global Native

; Remove akActor from whatever cell it is assigned to (no-op if unassigned).
Function ClearActorCell(Actor akActor) Global Native

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
; Furniture FLAGS work exactly like cell flags: one flag per character, and
; every asFilterString parameter uses the FILTER SYNTAX documented in the
; Cells header. Configured in the CK on the furniture reference's
; GordianKnotFurnitureAttribs script (String property `flags`, refreshed on
; every scan) or at runtime via SetFurnitureFlags.

; All furniture handles belonging to a labyrinth, optionally filtered.
Int[] Function GetFurnitures(ObjectReference akLabyrinth, String asFilterString = "") Global Native

ObjectReference Function GetFurnitureRef(Int aiFurniture) Global Native

; The handle of the furniture whose reference is akRef, or 0 if akRef is None
; or not discovered furniture. Inverse of GetFurnitureRef; use it to turn the
; ref delivered by the GK_OnActivateFurniture mod event back into a handle.
Int Function GetFurnitureByRef(ObjectReference akRef) Global Native

ObjectReference Function GetFurnitureLabyrinth(Int aiFurniture) Global Native

String Function GetFurnitureFlags(Int aiFurniture) Global Native
Function SetFurnitureFlags(Int aiFurniture, String asFlags) Global Native

; 1 if aiFurniture is free, 0 if it is occupied -- or the handle is unknown.
Int Function GetFurnitureVacancy(Int aiFurniture) Global Native

; The actor occupying aiFurniture, or None if it is free, the handle is
; unknown, or the occupant no longer resolves. Read-only: never changes the
; furniture's occupancy.
Actor Function GetFurnitureOccupant(Int aiFurniture) Global Native

; Assign akActor to a free furniture of akLabyrinth ONLY, passing the flag
; filter -- picked at RANDOM (uniform among the matching furniture); returns
; the claimed furniture's handle. Same contract as AssignFreeCellToPrisoner (each
; furniture holds one actor). akLabyrinth must not be None (else 0, nothing
; assigned). If the actor already occupies a furniture -- in any labyrinth --
; it is MOVED to the picked one (its current furniture is never re-picked);
; when no furniture of akLabyrinth matches it stays put, and its current
; furniture's handle is returned only if that furniture is in akLabyrinth
; (otherwise 0). Returns 0 when nothing was assigned in akLabyrinth, or when a
; new actor cannot be tracked (adder -- see the alias-pool header); on 0
; nothing has changed.
Int Function AssignFreeFurnitureToPrisoner(Actor akActor, ObjectReference akLabyrinth, String asFilterString = "") Global Native

; Assign the SPECIFIC furniture aiFurniture to akActor (handles are unique
; across all labyrinths, so the furniture alone identifies the target). False
; if the furniture doesn't exist, is already occupied, or a new actor cannot
; be tracked (adder -- see the alias-pool header); on False nothing has
; changed. True if akActor already occupies aiFurniture (no change). If
; akActor occupies another furniture -- in any labyrinth -- it is MOVED to
; aiFurniture.
Bool Function AssignPrisonerToFurniture(Actor akActor, Int aiFurniture) Global Native

; The handle of the furniture akActor is assigned to, or 0 if unassigned.
Int Function GetActorFurniture(Actor akActor) Global Native

; Remove akActor from whatever furniture it is assigned to (no-op if unassigned).
Function ClearActorFurniture(Actor akActor) Global Native
