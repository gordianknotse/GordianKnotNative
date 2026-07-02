#include "Serialization/Serialization.h"

#include "Serialization/Records.h"
#include "State/GameState.h"

// =============================================================================
// Phase 2: actor records ('ACTR') + Revert. Resource/labyrinth/orphan records
// arrive in Phase 5 (see docs/PLAN-V1.md §9) — add cases to LoadCallback and a
// writer in SaveCallback; the schema is additive.
//
// SKSE call order on loading a save: Revert callback, then Load callback. So the
// loader always starts from clean containers.
// =============================================================================

namespace {
    using GK::ActorRecord;
    using GK::ActorRegistry;
    using GK::GameState;
    namespace Record = GK::Serialization::Record;
    namespace Version = GK::Serialization::Version;

    // --- ACTR -----------------------------------------------------------------

    void SaveActors(const SKSE::SerializationInterface* a_intfc, const ActorRegistry& a_actors) {
        if (!a_intfc->OpenRecord(Record::kActor, Version::kActor)) {
            logger::error("Serialization: failed to open ACTR record.");
            return;
        }

        const auto& records = a_actors.Records();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(records.size()));
        // Field-by-field (not the raw struct) so the schema is robust to layout.
        // v3 layout per actor: id, status, globalRoles, labCount, [labAnchorID, mask]*.
        for (const auto& [id, rec] : records) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(rec.status);
            a_intfc->WriteRecordData(rec.globalRoles);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(rec.rolesByLab.size()));
            for (const auto& [lab, mask] : rec.rolesByLab) {
                a_intfc->WriteRecordData(lab);
                a_intfc->WriteRecordData(mask);
            }
        }
        logger::info("Serialization: wrote {} actor record(s).", records.size());
    }

    void LoadActors(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, ActorRegistry& a_actors) {
        if (a_version > Version::kActor) {
            logger::warn("Serialization: ACTR version {} newer than {}; best-effort load.", a_version, Version::kActor);
        }

        std::uint32_t count = 0;
        a_intfc->ReadRecordData(count);

        std::uint32_t loaded = 0;
        std::uint32_t dropped = 0;
        std::uint32_t droppedLabs = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldID = 0;
            ActorRecord rec{};
            a_intfc->ReadRecordData(oldID);

            if (a_version >= 2) {
                a_intfc->ReadRecordData(rec.status);
                if (a_version >= 3) {
                    a_intfc->ReadRecordData(rec.globalRoles);  // v3+: global (Wanderer) mask
                }
                std::uint32_t labCount = 0;
                a_intfc->ReadRecordData(labCount);
                for (std::uint32_t j = 0; j < labCount; ++j) {
                    RE::FormID oldLab = 0;
                    std::uint32_t mask = 0;
                    a_intfc->ReadRecordData(oldLab);
                    a_intfc->ReadRecordData(mask);
                    RE::FormID newLab = 0;
                    if (a_intfc->ResolveFormID(oldLab, newLab)) {
                        rec.rolesByLab[newLab] = mask;
                    } else {
                        ++droppedLabs;  // labyrinth anchor's plugin removed
                    }
                }
            } else {
                // v1: a single unscoped role mask we can't attribute to a labyrinth.
                // Consume it (keep the stream aligned) and keep only status.
                std::uint32_t legacyRoles = 0;
                a_intfc->ReadRecordData(legacyRoles);
                a_intfc->ReadRecordData(rec.status);
            }

            // Load order can shift between saves — remap the stored FormID.
            RE::FormID newID = 0;
            if (!a_intfc->ResolveFormID(oldID, newID)) {
                ++dropped;  // actor deleted or its plugin removed
                continue;
            }
            a_actors.Put(newID, rec);
            ++loaded;
        }
        logger::info("Serialization: loaded {} actor record(s) ({} dropped, {} labyrinth assoc dropped).", loaded,
                     dropped, droppedLabs);
    }

    // --- callbacks ------------------------------------------------------------

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        auto* state = GameState::GetSingleton();
        auto lock = state->Lock();
        SaveActors(a_intfc, state->Actors());
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc) {
        auto* state = GameState::GetSingleton();
        auto lock = state->Lock();

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            switch (type) {
            case Record::kActor:
                LoadActors(a_intfc, version, state->Actors());
                break;
            default:
                logger::warn("Serialization: unknown record {:08X} (len {}); skipping.", type, length);
                break;
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        GameState::GetSingleton()->Reset();
        logger::info("Serialization: state reverted.");
    }
}

namespace GK::Serialization {
    void Install() {
        auto* intfc = SKSE::GetSerializationInterface();
        if (!intfc) {
            logger::error("Serialization: interface unavailable; persistence disabled.");
            return;
        }
        intfc->SetUniqueID(kUniqueID);
        intfc->SetSaveCallback(SaveCallback);
        intfc->SetLoadCallback(LoadCallback);
        intfc->SetRevertCallback(RevertCallback);
        logger::info("Serialization: callbacks registered (uid {:08X}).", kUniqueID);
    }
}
