# Gordian Knot Native — V1 Plan

Design doc for the V1 native layer: actor tracking + spatial resource management
(cells, patrol markers, furniture) grouped into labyrinths, with SKSE co-save
persistence and a `GKNative` Papyrus surface.

This is the contract the separate Papyrus/CK repo builds against. Keep it current.

---

## 1. Locked decisions

- **Resource identity:** opaque **`int` handles**, stable across saves (handles are
  persisted, see §7). Type-specific getters. `0` = "none / invalid".
- **Actor status:** opaque **`int`**, Papyrus owns the vocabulary. Native reserves
  only **`0` = Idle**.
- **Actor roles:** native-defined **bitflags** — `Wanderer = 0x1`, `Warden = 0x2`
  (both = `0x3`). Passed as `int`. Dynamic (can change at runtime).
- **Furniture:** a placed **ObjectReference**, single occupant.
- **Labyrinth identity (Encoding B):** a **Keyword** + a per-labyrinth **anchor
  XMarker** reference (gives the labyrinth a position — teleport target, debug TP,
  future spawn origin). A resource ties to its labyrinth via a linked ref whose
  **keyword = resource type** and whose **target = that labyrinth's anchor**.
- **Discovery trigger:** Papyrus calls `ScanLabyrinth(keyword)` when a labyrinth
  comes online (idempotent; safe to re-call).
- **Cell max-occupants:** native default (configurable) + `SetCellMaxOccupants`
  override from Papyrus.
- **Orphan handling:** when a resource disappears (deleted across a mod update, or
  not re-discovered on re-scan), its still-valid occupants become **orphan actors**,
  queued + persisted, and Papyrus is nudged to resolve them. ("Orphan", never
  "freed" — a future intentional *Free prisoner* action is a separate path.)

---

## 2. CK authoring contract

The ESP/quest (separate repo) must provide:

- **Type keywords** (shipped once, reused by every labyrinth):
  `GK_CellDoor`, `GK_PatrolMarker`, `GK_Furniture`, and `GK_InMarker`,
  `GK_OutMarker` for the cell triple.
- **Per labyrinth:** one **Keyword** (e.g. `GK_LabyrinthA`) + one **anchor XMarker**
  placed reference.
- **Each resource reference** (placed door / marker / furniture): a linked ref
  `type-keyword → labyrinth anchor`. One probe yields both *type* and *labyrinth*.
- **Each cell door** additionally: linked refs `GK_InMarker → in-marker`,
  `GK_OutMarker → out-marker`.
- **Quest properties → native:** the labyrinth keywords + their anchor refs (one
  `RegisterLabyrinth` call each), and the five type keywords (one `ConfigureKeywords`
  call). Native does not hardcode editor IDs.

Engine note: keywords live on **base forms**, not placed references, and there is no
reverse index for linked refs — so native enumerates a cell's references
(`TESObjectCELL::ForEachReference`) and probes each with `GetLinkedRef(kwd)`.

---

## 3. Module / file layout (`src/`)

```
src/State/ActorState.h             Role bitflags, status constants, ActorRecord
src/State/ActorRegistry.{h,cpp}    FormID -> ActorRecord; role/status queries
src/State/GameState.{h,cpp}        singleton: owns all registries + one mutex
src/State/Resource.h               OccupiableResource base + ResourcePool<T>     (Phase 3)
src/State/Labyrinth.{h,cpp}        keyword<->anchor registry + discovery scan     (Phase 3)
src/State/Cell.h / Marker.h / Furniture.h   resource structs                     (Phase 3)
src/Papyrus/GKNative.{h,cpp}       Papyrus native-function registration
src/Serialization/Records.h        record type codes + versions                   (Phase 2/5)
src/Serialization/Serialization.{h,cpp}  Save/Load/Revert callbacks               (Phase 2/5)
```

`Plugin.cpp` wires up the Papyrus callback (`GetPapyrusInterface()->Register`) and
the serialization callbacks (`GetSerializationInterface()` →
`SetUniqueID`/`SetSaveCallback`/`SetLoadCallback`/`SetRevertCallback`).

---

## 4. Core data model

- **`ActorRecord`** = `{ uint32 roles; int32 status; }`, keyed by actor `FormID`. The
  player (`0x14`) is just another entry.
- **`OccupiableResource`** (base) = `{ int handle; FormID labyrinthKwd; uint32
  maxOccupants; vector<FormID> occupants; }`.
  - **Cell** adds `door, inMarker, outMarker` (REFR FormIDs).
  - **Marker** adds `markerRef` (max default 1).
  - **Furniture** adds `furnitureRef` (max fixed 1).
- **`ResourcePool<T>`** = `map<int handle, T>` + an `actor FormID → handle` occupancy
  index (O(1) reverse lookup, `GetAvailable`). One global monotonic handle counter
  (persisted) → handles unique across all pools and stable across saves.
- **`Labyrinth`** = `keyword FormID → { anchor REFR FormID }`. Resources reference
  their labyrinth by keyword.

All registries are owned by the **`GameState`** singleton and guarded by one
`std::recursive_mutex` (Papyrus VM threads, the serialization thread, and event
sinks all touch this state).

---

## 5. `GKNative` Papyrus surface

All global functions (first param `StaticFunctionTag*`), class `"GKNative"`.
`RE::Actor*` / `RE::TESObjectREFR*` / `RE::BGSKeyword*` and `std::vector<…>` returns
are confirmed supported by the binding traits in this CommonLibSSE-NG fork.

### Config / lifecycle
```
ConfigureKeywords(Keyword cellDoor, Keyword patrolMarker, Keyword furniture,
                  Keyword inMarker, Keyword outMarker)
RegisterLabyrinth(Keyword labyrinth, ObjectReference anchor)
int ScanLabyrinth(Keyword labyrinth)            ; count discovered (idempotent)
ObjectReference GetLabyrinthAnchor(Keyword labyrinth)
```

### Actors (player and NPCs identical)
```
SetActorRoles(Actor, int roleBits) / AddActorRole(Actor, int) / RemoveActorRole(Actor, int)
int  GetActorRoles(Actor)
bool IsWanderer(Actor) / bool IsWarden(Actor)
SetActorStatus(Actor, int status) / int GetActorStatus(Actor)
Actor[] GetActorsByRole(int roleMask) / Actor[] GetActorsByStatus(int status)
ForgetActor(Actor)
```

### Cells (handle-keyed)
```
int GetAvailableCell(Keyword labyrinth)
int AssignCellToActor(Keyword labyrinth, Actor)         ; handle or 0
bool ReleaseActorFromCell(Actor) / int GetCellOfActor(Actor)
ObjectReference GetCellDoor(int) / GetCellInMarker(int) / GetCellOutMarker(int)
int GetCellMaxOccupants(int) / SetCellMaxOccupants(int, int)
int GetCellOccupantCount(int) / Actor[] GetCellOccupants(int)
Keyword GetCellLabyrinth(int) / int[] GetCells(Keyword labyrinth)
```

### Markers (same shape, default max 1)
```
int GetAvailableMarker(Keyword) / int AssignMarkerToActor(Keyword, Actor)
bool ReleaseActorFromMarker(Actor) / int GetMarkerOfActor(Actor)
ObjectReference GetMarkerRef(int) + max / occupants / labyrinth getters
```

### Furniture (same, single occupant)
```
int GetAvailableFurniture(Keyword) / int AssignFurnitureToActor(Keyword, Actor)
bool ReleaseActorFromFurniture(Actor) / int GetFurnitureOfActor(Actor)
ObjectReference GetFurnitureRef(int) + labyrinth getter
```

### Orphans (resource vanished out from under occupants)
```
Actor[] GetOrphanActors()
Keyword GetOrphanActorLabyrinth(Actor)          ; None if labyrinth also gone
int     GetOrphanActorType(Actor)               ; 0=cell 1=marker 2=furniture
int     GetOrphanActorLastStatus(Actor)
ClearOrphanActor(Actor) / ClearOrphanActors()
```

### Papyrus-side constants (mirror in the other repo's `GKNative.psc`)
```
GK_ROLE_WANDERER = 1   GK_ROLE_WARDEN = 2
GK_STATUS_IDLE   = 0
GK_TYPE_CELL = 0  GK_TYPE_MARKER = 1  GK_TYPE_FURNITURE = 2
```

---

## 6. Discovery (`ScanLabyrinth`)

1. Look up the labyrinth's anchor (from `RegisterLabyrinth`).
2. Sweep the labyrinth's loaded interior cells; for each reference, probe the three
   type keywords with `GetLinkedRef(typeKwd)`. A non-null target that equals a
   *registered* anchor identifies both the resource's **type** and **labyrinth**.
3. For a cell door, resolve the triple: `door.GetLinkedRef(GK_InMarker)` /
   `GetLinkedRef(GK_OutMarker)`.
4. **Idempotent:** dedupe on the resource's ref FormID — re-discovering an existing
   resource updates it and **preserves its handle/occupancy**; it does not duplicate.
5. The actual cell sweep runs on the **main thread via the SKSE task interface**
   (safe to touch `TESObjectCELL::references`); results are published under the lock.

---

## 7. Serialization (additive co-save)

`SetUniqueID('GKNT')`. Every FormID is round-tripped through `ResolveFormID` on load
(load order can shift). Records, each **version 1**, additive-only schema:

- **`'NEXT'`** — global handle counter (keeps saved handles valid).
- **`'ACTR'`** — count, then `{ actorFormID, roles u32, status i32 }`.
- **`'LABY'`** — count, then `{ labyrinthKwd FormID, anchor FormID }`.
- **`'CELL'`** — count, then `{ handle, labyrinthKwd, door, inMarker, outMarker,
  max, occupant FormIDs[] }`.
- **`'MARK'`** / **`'FURN'`** — analogous to `'CELL'`.
- **`'ORPH'`** — count, then `{ actor, labyrinthKwd, type, lastStatus }`.

**Load:** loop `GetNextRecordInfo` → switch on type. **Revert:** `GameState::Reset()`
clears all containers and resets the counter.

**Why persist topology + handles** (not just occupancy): keeps `int` handles stable so
Papyrus can store them safely. `ScanLabyrinth` is idempotent (keys on ref FormID), so
re-discovery after load reconciles rather than duplicating.

---

## 8. Orphan reconciliation

A resource is **lost** when (a) its ref fails `ResolveFormID` on load, or (b) a
`ScanLabyrinth` re-sweep does not re-discover a previously-registered resource. For a
lost resource:

- Each occupant that **still resolves** → an **orphan**: removed from the defunct
  resource, pushed onto the orphan queue with `{ actor, labyrinthKwd, type,
  lastStatus }`. Its status is **left untouched** so Papyrus retains context.
- Each occupant that **no longer resolves** (NPC deleted) → dropped silently.
- The resource + handle are discarded.

**Signalling:** the orphan queue is persisted (`'ORPH'`). Native fires the mod event
**`GK_OnActorsOrphaned`** (a nudge, no payload) at `kPostLoadGame` and after any
runtime re-scan that orphans someone. Papyrus drains via `GetOrphanActors()` (in the
event handler *and* its periodic maintenance), applies policy per actor
(relocate via `AssignCellToActor`, delete, or stash), then `ClearOrphanActor`.

Mod-event vocabulary so far: `GK_OnEquip`, `GK_OnUnequip`, `GK_OnActorsOrphaned`.

---

## 9. Implementation phases (each verifiable in-game)

1. **Actor core** — `GameState` + `ActorRegistry` + `GKNative` actor functions.
   Verify: a test script sets/reads roles + status on the player.
2. **Actor persistence** — `'NEXT'`/`'ACTR'` + Revert. Verify across save/load/new-game.
3. **Labyrinth + resource model + discovery** — pools, `ConfigureKeywords` /
   `RegisterLabyrinth` / `ScanLabyrinth` (Encoding B linked-ref sweep), getters.
   Verify: scan logs discovered doors/markers/furniture + grouped cell triples.
4. **Assignment** — `GetAvailable*` / `Assign*ToActor` / `Release*` / occupancy +
   runtime re-scan orphan reconciliation + `GK_OnActorsOrphaned`. Verify assign→full→release.
5. **Resource persistence** — `'LABY'/'CELL'/'MARK'/'FURN'/'ORPH'` + load-time
   reconciliation + idempotent re-scan. Verify full state survives reload + a removed
   resource surfaces orphans.
6. **Polish** — main-thread scan task, query ergonomics, docs (CLAUDE.md/README),
   MO2 end-to-end.
