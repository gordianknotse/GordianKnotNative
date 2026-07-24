#include "Serialization/TemplateStore.h"

#include "State/GameState.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace GK::Serialization {
    namespace {
        constexpr auto kDirectory = "Data/SKSE/Plugins/GordianKnot";
        constexpr auto kFilePath = "Data/SKSE/Plugins/GordianKnot/templates.json";
        constexpr int kFileVersion = 1;

        // "Plugin.esp|0x000D63" for a plugin-defined form; "" for dynamic
        // (save-local) forms, which cannot be referenced across saves.
        [[nodiscard]] std::string EncodeForm(RE::FormID a_id) {
            const auto* form = RE::TESForm::LookupByID(a_id);
            const auto* file = form ? form->GetFile(0) : nullptr;
            if (!file) {
                return {};
            }
            return std::format("{}|0x{:06X}", file->GetFilename(), form->GetLocalFormID());
        }

        // Inverse of EncodeForm: current-session FormID, or 0 when the plugin
        // is absent / the form no longer exists.
        [[nodiscard]] RE::FormID DecodeForm(std::string_view a_text) {
            const auto sep = a_text.find('|');
            if (sep == std::string_view::npos || sep == 0) {
                return 0;
            }
            const auto plugin = a_text.substr(0, sep);
            const std::string idText(a_text.substr(sep + 1));
            const auto localID = static_cast<RE::FormID>(std::strtoul(idText.c_str(), nullptr, 16));
            auto* handler = RE::TESDataHandler::GetSingleton();
            const auto* form = handler ? handler->LookupForm(localID, plugin) : nullptr;
            return form ? form->GetFormID() : 0;
        }
    }

    std::int32_t SaveTemplateFile() {
        nlohmann::json root;
        root["version"] = kFileVersion;
        auto& templates = root["templates"] = nlohmann::json::object();

        std::int32_t written = 0;
        std::uint32_t skippedDynamic = 0;
        {
            auto* state = GK::GameState::GetSingleton();
            auto lock = state->Lock();
            const auto it = state->Outfits().Actors().find(0);  // 0 = template namespace
            if (it != state->Outfits().Actors().end()) {
                for (const auto& [name, slots] : it->second) {
                    nlohmann::json entry;
                    entry["tags"] = nlohmann::json::array();  // reserved for future context tagging
                    auto& slotsJson = entry["slots"] = nlohmann::json::object();
                    std::uint32_t devices = 0;
                    for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                        if (slots[slot] == 0) {
                            continue;
                        }
                        auto encoded = EncodeForm(slots[slot]);
                        if (encoded.empty()) {
                            logger::warn("TemplateStore:   '{}' slot {}: device {:08X} is a dynamic (save-local) "
                                         "form; skipped.",
                                         name, slot, slots[slot]);
                            ++skippedDynamic;
                            continue;
                        }
                        slotsJson[std::to_string(slot)] = std::move(encoded);
                        ++devices;
                    }
                    logger::info("TemplateStore:   saving '{}' ({} device(s)).", name, devices);
                    templates[name] = std::move(entry);
                    ++written;
                }
            }
        }

        std::error_code ec;
        std::filesystem::create_directories(kDirectory, ec);
        std::ofstream out(kFilePath, std::ios::trunc);
        if (!out) {
            logger::error("TemplateStore: cannot open {} for writing.", kFilePath);
            return -1;
        }
        out << root.dump(2) << '\n';
        logger::info("TemplateStore: wrote {} template(s) to {} ({} dynamic device(s) skipped).", written, kFilePath,
                     skippedDynamic);
        return written;
    }

    std::int32_t LoadTemplateFile() {
        std::ifstream in(kFilePath);
        if (!in) {
            logger::info("TemplateStore: no {} yet; nothing to load.", kFilePath);
            return -1;
        }
        nlohmann::json root;
        try {
            in >> root;
        } catch (const std::exception& e) {
            logger::error("TemplateStore: {} is not valid JSON ({}); nothing loaded.", kFilePath, e.what());
            return -1;
        }
        const auto templates = root.find("templates");
        if (templates == root.end() || !templates->is_object()) {
            logger::error("TemplateStore: {} has no 'templates' object; nothing loaded.", kFilePath);
            return -1;
        }

        std::int32_t loaded = 0;
        std::uint32_t droppedDevices = 0;
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (const auto& [name, entry] : templates->items()) {
            if (name.empty() || !entry.is_object()) {
                continue;
            }
            GK::OutfitRegistry::Slots slots{};
            std::uint32_t devices = 0;
            if (const auto slotsIt = entry.find("slots"); slotsIt != entry.end() && slotsIt->is_object()) {
                for (const auto& [slotKey, value] : slotsIt->items()) {
                    const int slot = std::atoi(slotKey.c_str());
                    if (slot < static_cast<int>(GK::kBipedSlotFirst) || slot > static_cast<int>(GK::kBipedSlotLast) ||
                        !value.is_string()) {
                        logger::warn("TemplateStore:   '{}': malformed slot entry '{}'; skipped.", name, slotKey);
                        continue;
                    }
                    const auto text = value.get<std::string>();
                    const auto id = DecodeForm(text);
                    if (id != 0) {
                        slots[static_cast<std::size_t>(slot)] = id;
                        ++devices;
                    } else {
                        logger::warn("TemplateStore:   '{}' slot {}: '{}' did not resolve (plugin missing or form "
                                     "removed); slot left empty.",
                                     name, slot, text);
                        ++droppedDevices;  // device's plugin absent in this load order
                    }
                }
            }
            logger::info("TemplateStore:   loaded '{}' ({} device(s)).", name, devices);
            state->Outfits().GetOrCreate(0, name) = slots;  // disk wins by name; others survive
            ++loaded;
        }
        logger::info("TemplateStore: loaded {} template(s) from {} ({} device(s) dropped).", loaded, kFilePath,
                     droppedDevices);
        return loaded;
    }
}
