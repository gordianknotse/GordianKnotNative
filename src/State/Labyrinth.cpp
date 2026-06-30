#include "State/Labyrinth.h"

#include "State/GameState.h"

// =============================================================================
// Discovery (Encoding B). A resource reference is identified by a type linked-ref
// (GK_CellDoor / GK_PatrolMarker / GK_Furniture) whose target is a registered
// labyrinth's anchor XMarker; that target both classifies the resource and (being
// the labyrinth's identity) tells us which labyrinth it belongs to. A cell door
// additionally resolves its in/out markers via GK_InMarker / GK_OutMarker.
//
// Discovery is a single global sweep of the instantiated-form table (ScanAllForms):
// it finds every PERSISTENT resource reference without requiring its cell to be
// loaded. Non-persistent references aren't in that table while their cell is
// detached, so resources must be flagged persistent in the CK to be discovered.
// =============================================================================

namespace GK {
    namespace {
        // Contract with the Papyrus mod: a cell door may carry a script of this class
        // exposing an Int property of this name to set the cell's capacity from the CK
        // (no runtime call needed). Absent script / wrong class / missing or non-int
        // property all fall back to the default capacity.
        constexpr const char* kDoorScriptClass = "GordianKnotCellDoor";
        constexpr const char* kMaxOccupantsProperty = "maxOccupants";

        std::uint32_t ReadDoorMaxOccupants(RE::TESObjectREFR& a_door, std::uint32_t a_default) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                return a_default;
            }
            auto* policy = vm->GetObjectHandlePolicy();
            if (!policy) {
                return a_default;
            }

            const auto handle = policy->GetHandleForObject(a_door.GetFormType(), &a_door);
            if (handle == policy->EmptyHandle()) {
                return a_default;  // no VM handle for this ref
            }

            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!vm->FindBoundObject(handle, kDoorScriptClass, object) || !object) {
                return a_default;  // door has no GordianKnotCellDoor script attached
            }

            const auto* var = object->GetProperty(kMaxOccupantsProperty);
            if (!var || !var->IsInt()) {
                return a_default;  // property missing or not an Int
            }

            const auto value = var->GetSInt();
            return value > 0 ? static_cast<std::uint32_t>(value) : a_default;
        }

        void UpsertCell(ResourceRegistry& a_reg, RE::TESObjectREFR& a_door, RE::FormID a_labyrinth,
                        const ResourceKeywords& a_kw) {
            const RE::FormID doorID = a_door.GetFormID();

            RE::FormID inID = 0;
            RE::FormID outID = 0;
            if (auto* m = a_door.GetLinkedRef(a_kw.inMarker)) {
                inID = m->GetFormID();
            }
            if (auto* m = a_door.GetLinkedRef(a_kw.outMarker)) {
                outID = m->GetFormID();
            }

            // Capacity is CK config (read off the door's script), so refresh it on
            // every discovery; occupants/handle are runtime state and are preserved.
            const std::uint32_t maxOcc = ReadDoorMaxOccupants(a_door, kDefaultCellMaxOccupants);

            if (auto* existing = a_reg.CellPool().FindByKey(doorID)) {
                // Refresh topology + capacity; preserve handle and occupants.
                existing->labyrinth = a_labyrinth;
                existing->maxOccupants = maxOcc;
                existing->inMarker = inID;
                existing->outMarker = outID;
            } else {
                Cell cell;
                cell.handle = a_reg.NextHandle();
                cell.labyrinth = a_labyrinth;
                cell.maxOccupants = maxOcc;
                cell.door = doorID;
                cell.inMarker = inID;
                cell.outMarker = outID;
                a_reg.CellPool().Insert(std::move(cell));
            }
        }

        void UpsertMarker(ResourceRegistry& a_reg, RE::TESObjectREFR& a_ref, RE::FormID a_labyrinth) {
            const RE::FormID refID = a_ref.GetFormID();
            if (auto* existing = a_reg.MarkerPool().FindByKey(refID)) {
                existing->labyrinth = a_labyrinth;
            } else {
                Marker marker;
                marker.handle = a_reg.NextHandle();
                marker.labyrinth = a_labyrinth;
                marker.maxOccupants = kDefaultMarkerMaxOccupants;
                marker.ref = refID;
                a_reg.MarkerPool().Insert(std::move(marker));
            }
        }

        void UpsertFurniture(ResourceRegistry& a_reg, RE::TESObjectREFR& a_ref, RE::FormID a_labyrinth) {
            const RE::FormID refID = a_ref.GetFormID();
            if (auto* existing = a_reg.FurniturePool().FindByKey(refID)) {
                existing->labyrinth = a_labyrinth;
            } else {
                Furniture furniture;
                furniture.handle = a_reg.NextHandle();
                furniture.labyrinth = a_labyrinth;
                furniture.maxOccupants = 1;  // furniture is single-occupant
                furniture.ref = refID;
                a_reg.FurniturePool().Insert(std::move(furniture));
            }
        }

        // Classify a single reference against ALL registered labyrinths: if one of
        // its type linked-refs targets a registered anchor, upsert it under that
        // labyrinth (keyed by the anchor's FormID) and return true. The caller holds
        // the GameState lock.
        bool ClassifyRef(RE::TESObjectREFR& a_ref, const ResourceKeywords& a_kw, const LabyrinthRegistry& a_labs,
                         ResourceRegistry& a_reg) {
            if (const auto* tgt = a_ref.GetLinkedRef(a_kw.cellDoor)) {
                if (a_labs.Contains(tgt->GetFormID())) {
                    UpsertCell(a_reg, a_ref, tgt->GetFormID(), a_kw);
                    return true;
                }
            }
            if (const auto* tgt = a_ref.GetLinkedRef(a_kw.patrolMarker)) {
                if (a_labs.Contains(tgt->GetFormID())) {
                    UpsertMarker(a_reg, a_ref, tgt->GetFormID());
                    return true;
                }
            }
            if (const auto* tgt = a_ref.GetLinkedRef(a_kw.furniture)) {
                if (a_labs.Contains(tgt->GetFormID())) {
                    UpsertFurniture(a_reg, a_ref, tgt->GetFormID());
                    return true;
                }
            }
            return false;
        }
    }

    int ScanAllForms() {
        auto* state = GameState::GetSingleton();
        auto lock = state->Lock();

        auto& labs = state->Labyrinths();
        if (labs.Empty()) {
            logger::warn("ScanAllForms: no labyrinths registered (call RegisterLabyrinth first).");
            return 0;
        }

        const auto& kw = state->Keywords();
        if (!kw.Valid()) {
            logger::warn("ScanAllForms: resource keywords not configured (call ConfigureKeywords first).");
            return 0;
        }

        auto& reg = state->Resources();
        std::uint32_t examined = 0;
        std::uint32_t matched = 0;

        // The global form table holds every INSTANTIATED form: all persistent
        // references at all times, plus temporaries of currently-loaded cells. So this
        // finds resources without their cells being loaded -- but only if those
        // references are persistent (doors / patrol markers / furniture are linked-ref
        // SOURCES and must be flagged persistent in the CK).
        const auto& [map, formLock] = RE::TESForm::GetAllForms();
        const RE::BSReadLockGuard guard{formLock};
        if (map) {
            for (auto& [id, form] : *map) {
                if (!form) {
                    continue;
                }
                auto* ref = form->As<RE::TESObjectREFR>();
                if (!ref) {
                    continue;  // cheap filter: skip the (many) non-reference forms
                }
                ++examined;
                if (ClassifyRef(*ref, kw, labs, reg)) {
                    ++matched;
                }
            }
        }

        logger::info("ScanAllForms: examined {} reference(s), matched {} resource(s) across {} labyrinth(s).", examined,
                     matched, labs.All().size());
        return static_cast<int>(matched);
    }
}
