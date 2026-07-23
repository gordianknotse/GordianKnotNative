#include "Serialization/Serialization.h"

#include "Serialization/Records.h"
#include "State/GameState.h"

#include <algorithm>

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
    using GK::AttributeRegistry;
    using GK::GameState;
    using GK::QueueRegistry;
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
        // v4 layout per actor: id, statusLen, status bytes (no terminator),
        // globalRoles, labCount, [labAnchorID, mask]*.
        for (const auto& [id, rec] : records) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(rec.status.size()));
            a_intfc->WriteRecordData(rec.status.data(), static_cast<std::uint32_t>(rec.status.size()));
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
                if (a_version >= 4) {
                    std::uint32_t statusLen = 0;
                    a_intfc->ReadRecordData(statusLen);
                    rec.status.assign(statusLen, '\0');
                    if (statusLen > 0) {
                        a_intfc->ReadRecordData(rec.status.data(), statusLen);
                    } else {
                        rec.status = GK::Status::kIdle;  // early-v4 saves stored "" for idle
                    }
                } else {
                    // Pre-v4: an int32 status code. Keep its decimal spelling so
                    // nothing is silently lost (0 = idle -> the "idle" default).
                    std::int32_t legacyStatus = 0;
                    a_intfc->ReadRecordData(legacyStatus);
                    if (legacyStatus != 0) {
                        rec.status = std::to_string(legacyStatus);
                    }
                }
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
                // Consume it (keep the stream aligned) and keep only status (an
                // int32 code back then; keep its decimal spelling, 0 -> "idle").
                std::uint32_t legacyRoles = 0;
                a_intfc->ReadRecordData(legacyRoles);
                std::int32_t legacyStatus = 0;
                a_intfc->ReadRecordData(legacyStatus);
                if (legacyStatus != 0) {
                    rec.status = std::to_string(legacyStatus);
                }
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

    // --- ATTR -----------------------------------------------------------------

    void SaveAttributes(const SKSE::SerializationInterface* a_intfc, const AttributeRegistry& a_attributes) {
        if (!a_intfc->OpenRecord(Record::kAttribute, Version::kAttribute)) {
            logger::error("Serialization: failed to open ATTR record.");
            return;
        }

        const auto& actors = a_attributes.Actors();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(actors.size()));
        for (const auto& [id, attrs] : actors) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(attrs.size()));
            for (const auto& [key, value] : attrs) {
                a_intfc->WriteRecordData(static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(key.data(), static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(value);
            }
        }
        logger::info("Serialization: wrote attributes for {} actor(s).", actors.size());
    }

    void LoadAttributes(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version,
                        AttributeRegistry& a_attributes) {
        if (a_version > Version::kAttribute) {
            logger::warn("Serialization: ATTR version {} newer than {}; best-effort load.", a_version,
                         Version::kAttribute);
        }

        std::uint32_t actorCount = 0;
        a_intfc->ReadRecordData(actorCount);

        std::uint32_t loaded = 0;
        std::uint32_t dropped = 0;
        std::uint32_t droppedActors = 0;
        for (std::uint32_t i = 0; i < actorCount; ++i) {
            RE::FormID oldID = 0;
            a_intfc->ReadRecordData(oldID);

            std::uint32_t attrCount = 0;
            a_intfc->ReadRecordData(attrCount);
            AttributeRegistry::AttributeMap attrs;
            for (std::uint32_t j = 0; j < attrCount; ++j) {
                std::uint32_t keyLen = 0;
                a_intfc->ReadRecordData(keyLen);
                std::string key(keyLen, '\0');
                if (keyLen > 0) {
                    a_intfc->ReadRecordData(key.data(), keyLen);
                }
                RE::FormID oldValue = 0;
                a_intfc->ReadRecordData(oldValue);
                // Load order can shift between saves — remap the stored FormID.
                RE::FormID newValue = 0;
                if (a_intfc->ResolveFormID(oldValue, newValue)) {
                    attrs[std::move(key)] = newValue;
                } else {
                    ++dropped;  // value reference deleted or its plugin removed
                }
            }

            RE::FormID newID = 0;
            if (!a_intfc->ResolveFormID(oldID, newID)) {
                ++droppedActors;  // actor deleted or its plugin removed
                continue;
            }
            loaded += static_cast<std::uint32_t>(attrs.size());
            a_attributes.Put(newID, std::move(attrs));  // Put skips a fully-dropped map
        }
        logger::info("Serialization: loaded {} attribute(s) across {} actor(s) ({} value(s), {} actor(s) dropped).",
                     loaded, actorCount, dropped, droppedActors);
    }

    // --- QUEU -----------------------------------------------------------------

    void SaveQueues(const SKSE::SerializationInterface* a_intfc, const QueueRegistry& a_queues) {
        if (!a_intfc->OpenRecord(Record::kQueue, Version::kQueue)) {
            logger::error("Serialization: failed to open QUEU record.");
            return;
        }

        const auto& queues = a_queues.Queues();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(queues.size()));
        for (const auto& [name, entries] : queues) {
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(name.size()));
            a_intfc->WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(entries.size()));
            for (const auto id : entries) {
                a_intfc->WriteRecordData(id);
            }
        }

        // v2: delayed enqueues. The session clock's absolute values die with the
        // session, so store REMAINING seconds and rebase on load.
        const auto now = GK::NowSeconds();
        const auto& delayed = a_queues.Delayed();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(delayed.size()));
        for (const auto& entry : delayed) {
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(entry.queue.size()));
            a_intfc->WriteRecordData(entry.queue.data(), static_cast<std::uint32_t>(entry.queue.size()));
            a_intfc->WriteRecordData(entry.actor);
            const double remaining = std::max(0.0, entry.due - now);
            a_intfc->WriteRecordData(remaining);
        }
        logger::info("Serialization: wrote {} actor queue(s) + {} delayed enqueue(s).", queues.size(), delayed.size());
    }

    void LoadQueues(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, QueueRegistry& a_queues) {
        if (a_version > Version::kQueue) {
            logger::warn("Serialization: QUEU version {} newer than {}; best-effort load.", a_version, Version::kQueue);
        }

        std::uint32_t queueCount = 0;
        a_intfc->ReadRecordData(queueCount);

        std::uint32_t loaded = 0;
        std::uint32_t dropped = 0;
        for (std::uint32_t i = 0; i < queueCount; ++i) {
            std::uint32_t nameLen = 0;
            a_intfc->ReadRecordData(nameLen);
            std::string name(nameLen, '\0');
            if (nameLen > 0) {
                a_intfc->ReadRecordData(name.data(), nameLen);
            }

            std::uint32_t entryCount = 0;
            a_intfc->ReadRecordData(entryCount);
            std::deque<RE::FormID> entries;
            for (std::uint32_t j = 0; j < entryCount; ++j) {
                RE::FormID oldID = 0;
                a_intfc->ReadRecordData(oldID);
                // Load order can shift between saves — remap the stored FormID.
                RE::FormID newID = 0;
                if (a_intfc->ResolveFormID(oldID, newID)) {
                    entries.push_back(newID);
                } else {
                    ++dropped;  // actor deleted or its plugin removed
                }
            }
            loaded += static_cast<std::uint32_t>(entries.size());
            a_queues.Put(name, std::move(entries));  // Put skips a fully-dropped queue
        }

        // v2 appends the delayed enqueues; v1 saves have none.
        std::uint32_t delayedLoaded = 0;
        std::uint32_t delayedDropped = 0;
        if (a_version >= 2) {
            const auto now = GK::NowSeconds();
            std::uint32_t delayedCount = 0;
            a_intfc->ReadRecordData(delayedCount);
            std::vector<GK::DelayedEnqueue> delayed;
            delayed.reserve(delayedCount);
            for (std::uint32_t i = 0; i < delayedCount; ++i) {
                std::uint32_t nameLen = 0;
                a_intfc->ReadRecordData(nameLen);
                std::string name(nameLen, '\0');
                if (nameLen > 0) {
                    a_intfc->ReadRecordData(name.data(), nameLen);
                }
                RE::FormID oldID = 0;
                a_intfc->ReadRecordData(oldID);
                double remaining = 0.0;
                a_intfc->ReadRecordData(remaining);
                RE::FormID newID = 0;
                if (a_intfc->ResolveFormID(oldID, newID)) {
                    delayed.push_back({std::move(name), newID, now + remaining});
                } else {
                    ++delayedDropped;  // actor deleted or its plugin removed
                }
            }
            delayedLoaded = static_cast<std::uint32_t>(delayed.size());
            a_queues.PutDelayed(std::move(delayed));
        }
        logger::info("Serialization: loaded {} queue(s) with {} entr(ies) ({} dropped) + {} delayed enqueue(s) ({} "
                     "dropped).",
                     queueCount, loaded, dropped, delayedLoaded, delayedDropped);
    }

    // --- ATTI -----------------------------------------------------------------

    void SaveIntAttributes(const SKSE::SerializationInterface* a_intfc, const AttributeRegistry& a_attributes) {
        if (!a_intfc->OpenRecord(Record::kIntAttribute, Version::kIntAttribute)) {
            logger::error("Serialization: failed to open ATTI record.");
            return;
        }

        const auto& actors = a_attributes.IntActors();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(actors.size()));
        for (const auto& [id, attrs] : actors) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(attrs.size()));
            for (const auto& [key, value] : attrs) {
                a_intfc->WriteRecordData(static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(key.data(), static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(value);
            }
        }
        logger::info("Serialization: wrote int attributes for {} actor(s).", actors.size());
    }

    void LoadIntAttributes(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version,
                           AttributeRegistry& a_attributes) {
        if (a_version > Version::kIntAttribute) {
            logger::warn("Serialization: ATTI version {} newer than {}; best-effort load.", a_version,
                         Version::kIntAttribute);
        }

        std::uint32_t actorCount = 0;
        a_intfc->ReadRecordData(actorCount);

        std::uint32_t loaded = 0;
        std::uint32_t droppedActors = 0;
        for (std::uint32_t i = 0; i < actorCount; ++i) {
            RE::FormID oldID = 0;
            a_intfc->ReadRecordData(oldID);

            std::uint32_t attrCount = 0;
            a_intfc->ReadRecordData(attrCount);
            AttributeRegistry::IntAttributeMap attrs;
            for (std::uint32_t j = 0; j < attrCount; ++j) {
                std::uint32_t keyLen = 0;
                a_intfc->ReadRecordData(keyLen);
                std::string key(keyLen, '\0');
                if (keyLen > 0) {
                    a_intfc->ReadRecordData(key.data(), keyLen);
                }
                std::int32_t value = 0;
                a_intfc->ReadRecordData(value);
                attrs[std::move(key)] = value;
            }

            // Load order can shift between saves — remap the stored FormID.
            RE::FormID newID = 0;
            if (!a_intfc->ResolveFormID(oldID, newID)) {
                ++droppedActors;  // actor deleted or its plugin removed
                continue;
            }
            loaded += static_cast<std::uint32_t>(attrs.size());
            a_attributes.PutInt(newID, std::move(attrs));
        }
        logger::info("Serialization: loaded {} int attribute(s) across {} actor(s) ({} actor(s) dropped).", loaded,
                     actorCount, droppedActors);
    }

    // --- ATTA -----------------------------------------------------------------

    void SaveArrayAttributes(const SKSE::SerializationInterface* a_intfc, const AttributeRegistry& a_attributes) {
        if (!a_intfc->OpenRecord(Record::kArrayAttribute, Version::kArrayAttribute)) {
            logger::error("Serialization: failed to open ATTA record.");
            return;
        }

        const auto& actors = a_attributes.ArrayActors();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(actors.size()));
        for (const auto& [id, attrs] : actors) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(attrs.size()));
            for (const auto& [key, values] : attrs) {
                a_intfc->WriteRecordData(static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(key.data(), static_cast<std::uint32_t>(key.size()));
                a_intfc->WriteRecordData(static_cast<std::uint32_t>(values.size()));
                for (const auto value : values) {
                    a_intfc->WriteRecordData(value);
                }
            }
        }
        logger::info("Serialization: wrote array attributes for {} actor(s).", actors.size());
    }

    void LoadArrayAttributes(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version,
                             AttributeRegistry& a_attributes) {
        if (a_version > Version::kArrayAttribute) {
            logger::warn("Serialization: ATTA version {} newer than {}; best-effort load.", a_version,
                         Version::kArrayAttribute);
        }

        std::uint32_t actorCount = 0;
        a_intfc->ReadRecordData(actorCount);

        std::uint32_t loaded = 0;
        std::uint32_t droppedValues = 0;
        std::uint32_t droppedActors = 0;
        for (std::uint32_t i = 0; i < actorCount; ++i) {
            RE::FormID oldID = 0;
            a_intfc->ReadRecordData(oldID);

            std::uint32_t attrCount = 0;
            a_intfc->ReadRecordData(attrCount);
            AttributeRegistry::ArrayAttributeMap attrs;
            for (std::uint32_t j = 0; j < attrCount; ++j) {
                std::uint32_t keyLen = 0;
                a_intfc->ReadRecordData(keyLen);
                std::string key(keyLen, '\0');
                if (keyLen > 0) {
                    a_intfc->ReadRecordData(key.data(), keyLen);
                }
                std::uint32_t valueCount = 0;
                a_intfc->ReadRecordData(valueCount);
                std::vector<RE::FormID> values;
                values.reserve(valueCount);
                for (std::uint32_t k = 0; k < valueCount; ++k) {
                    RE::FormID oldValue = 0;
                    a_intfc->ReadRecordData(oldValue);
                    // Arrays are POSITIONAL: 0 is a stored None slot, and an entry
                    // that no longer resolves becomes one (never removed -- indices
                    // must stay stable). Load order can shift between saves, so
                    // nonzero FormIDs are remapped.
                    RE::FormID newValue = 0;
                    if (oldValue != 0 && !a_intfc->ResolveFormID(oldValue, newValue)) {
                        ++droppedValues;  // value form deleted or its plugin removed -> None slot
                        newValue = 0;
                    }
                    values.push_back(newValue);
                }
                if (!values.empty()) {
                    attrs[std::move(key)] = std::move(values);
                }
            }

            RE::FormID newID = 0;
            if (!a_intfc->ResolveFormID(oldID, newID)) {
                ++droppedActors;  // actor deleted or its plugin removed
                continue;
            }
            loaded += static_cast<std::uint32_t>(attrs.size());
            a_attributes.PutArray(newID, std::move(attrs));
        }
        logger::info(
            "Serialization: loaded {} array attribute(s) across {} actor(s) ({} value(s) zeroed, {} actor(s) "
            "dropped).",
            loaded, actorCount, droppedValues, droppedActors);
    }

    // --- OUTF -----------------------------------------------------------------

    void SaveOutfits(const SKSE::SerializationInterface* a_intfc, const GK::OutfitRegistry& a_outfits) {
        if (!a_intfc->OpenRecord(Record::kOutfit, Version::kOutfit)) {
            logger::error("Serialization: failed to open OUTF record.");
            return;
        }

        const auto& actors = a_outfits.Actors();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(actors.size()));
        for (const auto& [id, outfits] : actors) {
            a_intfc->WriteRecordData(id);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(outfits.size()));
            for (const auto& [name, slots] : outfits) {
                a_intfc->WriteRecordData(static_cast<std::uint32_t>(name.size()));
                a_intfc->WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
                std::uint32_t entries = 0;
                for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                    entries += slots[slot] != 0;
                }
                a_intfc->WriteRecordData(entries);
                for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                    if (slots[slot] != 0) {
                        a_intfc->WriteRecordData(static_cast<std::uint32_t>(slot));
                        a_intfc->WriteRecordData(slots[slot]);
                    }
                }
            }
        }
        logger::info("Serialization: wrote outfits for {} actor(s).", actors.size());
    }

    void LoadOutfits(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version,
                     GK::OutfitRegistry& a_outfits) {
        if (a_version > Version::kOutfit) {
            logger::warn("Serialization: OUTF version {} newer than {}; best-effort load.", a_version,
                         Version::kOutfit);
        }

        std::uint32_t actorCount = 0;
        a_intfc->ReadRecordData(actorCount);

        std::uint32_t loaded = 0;
        std::uint32_t droppedDevices = 0;
        std::uint32_t droppedActors = 0;
        for (std::uint32_t i = 0; i < actorCount; ++i) {
            RE::FormID oldID = 0;
            a_intfc->ReadRecordData(oldID);

            std::uint32_t outfitCount = 0;
            a_intfc->ReadRecordData(outfitCount);
            GK::OutfitRegistry::OutfitMap outfits;
            for (std::uint32_t j = 0; j < outfitCount; ++j) {
                std::uint32_t nameLen = 0;
                a_intfc->ReadRecordData(nameLen);
                std::string name(nameLen, '\0');
                if (nameLen > 0) {
                    a_intfc->ReadRecordData(name.data(), nameLen);
                }
                std::uint32_t entries = 0;
                a_intfc->ReadRecordData(entries);
                GK::OutfitRegistry::Slots slots{};
                for (std::uint32_t k = 0; k < entries; ++k) {
                    std::uint32_t slot = 0;
                    RE::FormID oldDevice = 0;
                    a_intfc->ReadRecordData(slot);
                    a_intfc->ReadRecordData(oldDevice);
                    // Load order can shift between saves — remap the stored FormID.
                    RE::FormID newDevice = 0;
                    if (slot >= GK::kBipedSlotFirst && slot <= GK::kBipedSlotLast && oldDevice != 0 &&
                        a_intfc->ResolveFormID(oldDevice, newDevice)) {
                        slots[slot] = newDevice;
                    } else {
                        ++droppedDevices;  // device's plugin removed (its slots go empty)
                    }
                }
                // Defined-but-empty outfits are kept: existence is meaningful
                // (ActorOutfitExists, template shadowing).
                outfits[std::move(name)] = slots;
            }

            // Actor 0 is the TEMPLATE namespace (no form to resolve); real actors
            // are remapped, and dropped with their outfits if they no longer exist.
            RE::FormID newID = 0;
            if (oldID != 0 && !a_intfc->ResolveFormID(oldID, newID)) {
                ++droppedActors;  // actor deleted or its plugin removed
                continue;
            }
            loaded += static_cast<std::uint32_t>(outfits.size());
            a_outfits.Put(newID, std::move(outfits));
        }
        logger::info("Serialization: loaded {} outfit(s) across {} actor(s) ({} device(s), {} actor(s) dropped).",
                     loaded, actorCount, droppedDevices, droppedActors);
    }

    // --- callbacks ------------------------------------------------------------

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        auto* state = GameState::GetSingleton();
        auto lock = state->Lock();
        SaveActors(a_intfc, state->Actors());
        SaveAttributes(a_intfc, state->Attributes());
        SaveIntAttributes(a_intfc, state->Attributes());
        SaveArrayAttributes(a_intfc, state->Attributes());
        SaveOutfits(a_intfc, state->Outfits());
        SaveQueues(a_intfc, state->Queues());
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
            case Record::kAttribute:
                LoadAttributes(a_intfc, version, state->Attributes());
                break;
            case Record::kIntAttribute:
                LoadIntAttributes(a_intfc, version, state->Attributes());
                break;
            case Record::kArrayAttribute:
                LoadArrayAttributes(a_intfc, version, state->Attributes());
                break;
            case Record::kOutfit:
                LoadOutfits(a_intfc, version, state->Outfits());
                break;
            case Record::kQueue:
                LoadQueues(a_intfc, version, state->Queues());
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
