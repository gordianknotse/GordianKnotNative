#include "UI/DebugOverlay.h"

#include "State/AliasPool.h"
#include "State/GameClock.h"
#include "State/GameState.h"
#include "State/Labyrinth.h"

#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// In-game debug overlay: Dear ImGui rendered from a hook on the game's D3D11
// present call, fed input from a hook on the game's input-event dispatch. This
// is the established SKSE community pattern (dMenu / Wheeler): no Win32 backend,
// no WndProc -- Skyrim reads input through DirectInput and hides the OS cursor,
// so ImGui gets a software cursor and input translated from the game's own
// event stream.
//
// Input policy (per design): while the panel is open, MOUSE events are consumed
// by the panel (camera stops turning); KEYBOARD stays with the game unless an
// ImGui text field is focused (io.WantTextInput). Ctrl+Shift+G always toggles.
//
// Everything drawn is read-only over GameState (under its lock); the only
// mutating actions are explicitly-safe buttons ("Run ScanAllLabyrinths",
// per-labyrinth "Teleport player to anchor" -- the latter queued through the
// SKSE task interface, since game mutations may not run on this hook's thread).
// =============================================================================

namespace {
    // --- DirectInput scan codes (kbd ButtonEvent idCode) -----------------------
    constexpr std::uint32_t kDIK_Escape = 0x01;
    constexpr std::uint32_t kDIK_Minus = 0x0C;
    constexpr std::uint32_t kDIK_Backspace = 0x0E;
    constexpr std::uint32_t kDIK_Tab = 0x0F;
    constexpr std::uint32_t kDIK_Enter = 0x1C;
    constexpr std::uint32_t kDIK_LCtrl = 0x1D;
    constexpr std::uint32_t kDIK_G = 0x22;
    constexpr std::uint32_t kDIK_LShift = 0x2A;
    constexpr std::uint32_t kDIK_Period = 0x34;
    constexpr std::uint32_t kDIK_RShift = 0x36;
    constexpr std::uint32_t kDIK_Space = 0x39;
    constexpr std::uint32_t kDIK_RCtrl = 0x9D;
    constexpr std::uint32_t kDIK_Home = 0xC7;
    constexpr std::uint32_t kDIK_Up = 0xC8;
    constexpr std::uint32_t kDIK_Left = 0xCB;
    constexpr std::uint32_t kDIK_Right = 0xCD;
    constexpr std::uint32_t kDIK_End = 0xCF;
    constexpr std::uint32_t kDIK_Down = 0xD0;
    constexpr std::uint32_t kDIK_Delete = 0xD3;

    // Mouse ButtonEvent idCodes.
    constexpr std::uint32_t kMouse_WheelUp = 8;
    constexpr std::uint32_t kMouse_WheelDown = 9;

    // --- overlay state ----------------------------------------------------------
    std::atomic<bool> g_visible{false};
    bool g_imguiReady = false;
    IDXGISwapChain* g_swapChain = nullptr;
    float g_mouseX = 0.0f;
    float g_mouseY = 0.0f;
    bool g_ctrlHeld = false;
    bool g_shiftHeld = false;

    // --- input translation --------------------------------------------------------

    // Printable character for a scan code (US layout; enough for the filter box).
    char DIKToChar(std::uint32_t a_dik, bool a_shift) {
        constexpr std::string_view digits = "1234567890";  // 0x02 - 0x0B
        constexpr std::string_view rowQ = "qwertyuiop";    // 0x10 - 0x19
        constexpr std::string_view rowA = "asdfghjkl";     // 0x1E - 0x26
        constexpr std::string_view rowZ = "zxcvbnm";       // 0x2C - 0x32
        char c = 0;
        if (a_dik >= 0x02 && a_dik <= 0x0B) {
            c = digits[a_dik - 0x02];
        } else if (a_dik >= 0x10 && a_dik <= 0x19) {
            c = rowQ[a_dik - 0x10];
        } else if (a_dik >= 0x1E && a_dik <= 0x26) {
            c = rowA[a_dik - 0x1E];
        } else if (a_dik >= 0x2C && a_dik <= 0x32) {
            c = rowZ[a_dik - 0x2C];
        } else if (a_dik == kDIK_Space) {
            c = ' ';
        } else if (a_dik == kDIK_Minus) {
            c = a_shift ? '_' : '-';
        } else if (a_dik == kDIK_Period) {
            c = '.';
        }
        if (c >= 'a' && c <= 'z' && a_shift) {
            c = static_cast<char>(c - 'a' + 'A');
        }
        return c;
    }

    ImGuiKey DIKToImGuiKey(std::uint32_t a_dik) {
        switch (a_dik) {
        case kDIK_Backspace:
            return ImGuiKey_Backspace;
        case kDIK_Delete:
            return ImGuiKey_Delete;
        case kDIK_Left:
            return ImGuiKey_LeftArrow;
        case kDIK_Right:
            return ImGuiKey_RightArrow;
        case kDIK_Up:
            return ImGuiKey_UpArrow;
        case kDIK_Down:
            return ImGuiKey_DownArrow;
        case kDIK_Home:
            return ImGuiKey_Home;
        case kDIK_End:
            return ImGuiKey_End;
        case kDIK_Enter:
            return ImGuiKey_Enter;
        case kDIK_Escape:
            return ImGuiKey_Escape;
        case kDIK_Tab:
            return ImGuiKey_Tab;
        default:
            return ImGuiKey_None;
        }
    }

    // --- small formatting helpers -------------------------------------------------

    std::string HexID(RE::FormID a_id) { return fmt::format("{:08X}", a_id); }

    // "<hex> Name" for any reference (name blank for e.g. XMarkers).
    std::string RefLabel(RE::FormID a_id) {
        auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
        auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
        const char* name = ref ? ref->GetDisplayFullName() : "";
        return (name && name[0]) ? fmt::format("{:08X} {}", a_id, name) : HexID(a_id);
    }

    std::string RoleNames(std::uint32_t a_mask) {
        std::string out;
        const auto append = [&](const char* a_name) {
            if (!out.empty()) {
                out += '|';
            }
            out += a_name;
        };
        if (a_mask & GK::Role::kWanderer) {
            append("Wanderer");
        }
        if (a_mask & GK::Role::kWarden) {
            append("Warden");
        }
        if (a_mask & GK::Role::kPrisoner) {
            append("Prisoner");
        }
        return out.empty() ? "-" : out;
    }

    bool MatchesFilter(const std::string& a_haystack, const char* a_filter) {
        if (!a_filter || !a_filter[0]) {
            return true;
        }
        const auto lower = [](std::string s) {
            for (auto& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        };
        return lower(a_haystack).find(lower(a_filter)) != std::string::npos;
    }


    // --- ActorUtil package-override count (async VM poll) ------------------------
    // PapyrusUtil keeps its package overrides in its own plugin storage; the only
    // cross-plugin surface is the Papyrus global ActorUtil.CountPackageOverride --
    // the override LIST is not readable at all. The actor detail panel therefore
    // polls the COUNT through the VM: a throttled static call whose callback
    // stashes the result in atomics. The callback runs inside the VM -- never take
    // the GameState lock there (see VmCall.h).
    constexpr std::int32_t kPkgCountNone = -1;    // no result yet
    constexpr std::int32_t kPkgCountFailed = -2;  // dispatch/call failed (PapyrusUtil missing?)
    std::atomic<RE::FormID> g_pkgCountActor{0};   // actor the cached count belongs to
    std::atomic<std::int32_t> g_pkgCount{kPkgCountNone};
    std::atomic<bool> g_pkgCountPending{false};

    class PkgCountReceiver : public RE::BSScript::IStackCallbackFunctor {
    public:
        explicit PkgCountReceiver(RE::FormID a_actor) : _actor(a_actor) {}

        void operator()(RE::BSScript::Variable a_result) override {
            if (g_pkgCountActor.load() == _actor) {  // drop results for a stale selection
                g_pkgCount.store(a_result.IsInt() ? a_result.GetSInt() : kPkgCountFailed);
            }
            g_pkgCountPending.store(false);
        }

        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

    private:
        RE::FormID _actor;
    };

    // Refresh the cached count for a_actor (at most one call in flight, at most
    // one per second). Called every frame the detail panel shows a live actor.
    void PollPackageOverrideCount(RE::Actor& a_actor) {
        const auto id = a_actor.GetFormID();
        static std::chrono::steady_clock::time_point lastPoll{};
        if (g_pkgCountActor.exchange(id) != id) {
            g_pkgCount.store(kPkgCountNone);  // new selection: forget the old actor's count
            lastPoll = {};
        }
        const auto now = std::chrono::steady_clock::now();
        if (g_pkgCountPending.load() || now - lastPoll < std::chrono::seconds(1)) {
            return;
        }
        lastPoll = now;
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return;
        }
        const std::unique_ptr<RE::BSScript::IFunctionArguments> args{RE::MakeFunctionArguments(&a_actor)};
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new PkgCountReceiver(id)};
        g_pkgCountPending.store(true);
        if (!vm->DispatchStaticCall("ActorUtil", "CountPackageOverride", args.get(), callback)) {
            g_pkgCountPending.store(false);
            g_pkgCount.store(kPkgCountFailed);
        }
    }

    // --- panels ---------------------------------------------------------------------

    void DrawActorDetail(GK::GameState& a_state, RE::FormID a_id, const GK::ActorRecord& a_rec) {
        auto* form = RE::TESForm::LookupByID(a_id);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;

        ImGui::SeparatorText("Load / persistence");
        if (!actor) {
            ImGui::TextUnformatted("(FormID does not resolve to an Actor)");
        } else {
            const bool persistent = (actor->GetFormFlags() & RE::TESObjectREFR::RecordFlags::kPersistent) != 0;
            ImGui::Text("3D loaded: %s    persistent: %s    dead: %s    disabled: %s",
                        actor->Is3DLoaded() ? "yes" : "no", persistent ? "yes" : "no", actor->IsDead() ? "yes" : "no",
                        actor->IsDisabled() ? "yes" : "no");
            const auto* cell = actor->GetParentCell();
            const std::string cellLabel =
                cell ? ((cell->GetName() && cell->GetName()[0]) ? fmt::format("{:08X} {}", cell->GetFormID(),
                                                                              cell->GetName())
                                                                : HexID(cell->GetFormID()))
                     : std::string("(detached)");
            ImGui::Text("parent cell: %s", cellLabel.c_str());

            ImGui::SeparatorText("Position");
            const auto pos = actor->GetPosition();
            ImGui::Text("pos: (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                ImGui::Text("distance to player: %.0f", player->GetPosition().GetDistance(pos));
            }
            for (const auto& [lab, mask] : a_rec.rolesByLab) {
                auto* labForm = RE::TESForm::LookupByID(lab);
                auto* anchor = labForm ? labForm->As<RE::TESObjectREFR>() : nullptr;
                if (anchor) {
                    ImGui::Text("distance to lab %s: %.0f", HexID(lab).c_str(),
                                anchor->GetPosition().GetDistance(pos));
                }
            }

            ImGui::SeparatorText("AI");
            const auto* pkg = actor->GetCurrentPackage();
            const std::string pkgLabel =
                pkg ? fmt::format("{:08X} {}", pkg->GetFormID(), pkg->GetFormEditorID() ? pkg->GetFormEditorID() : "")
                    : std::string("(none)");
            ImGui::Text("current package: %s    in combat: %s", pkgLabel.c_str(), actor->IsInCombat() ? "yes" : "no");

            PollPackageOverrideCount(*actor);
            const auto pkgCount = g_pkgCount.load();
            if (pkgCount >= 0) {
                ImGui::Text("ActorUtil package overrides: %d", pkgCount);
            } else if (pkgCount == kPkgCountFailed) {
                ImGui::TextDisabled("ActorUtil package overrides: n/a (call failed -- PapyrusUtil missing?)");
            } else {
                ImGui::TextDisabled("ActorUtil package overrides: ...");
            }
        }

        ImGui::SeparatorText("Raw record");
        ImGui::Text("globalRoles: 0x%08X (%s)    status: \"%s\"", a_rec.globalRoles,
                    RoleNames(a_rec.globalRoles).c_str(), a_rec.status.c_str());
        for (const auto& [lab, mask] : a_rec.rolesByLab) {
            ImGui::Text("  lab %s -> 0x%X (%s)", RefLabel(lab).c_str(), mask, RoleNames(mask).c_str());
        }
        if (a_rec.rolesByLab.empty()) {
            ImGui::TextUnformatted("  (no labyrinth associations)");
        }

        // Delayed enqueues targeting this actor (usually at most one).
        const auto now = GK::NowSeconds();
        for (const auto& entry : a_state.Queues().Delayed()) {
            if (entry.actor != a_id) {
                continue;
            }
            const double left = entry.due - now;
            if (left > 0.0) {
                ImGui::Text("delayed enqueue -> \"%s\" in %.1f s", entry.queue.c_str(), left);
            } else {
                ImGui::Text("delayed enqueue -> \"%s\" (due)", entry.queue.c_str());
            }
        }
    }

    // Pool alias holding an actor (name), or "-" -- for the Actors table column.
    std::string AliasColumnFor(GK::GameState& a_state, RE::FormID a_id) {
        auto* quest = a_state.AliasQuest();
        if (!quest) {
            return "-";
        }
        auto* form = RE::TESForm::LookupByID(a_id);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;
        if (!actor) {
            return "-";
        }
        const RE::BSReadLockGuard guard{quest->aliasAccessLock};
        for (const auto* base : quest->aliases) {
            if (!base || !GK::AliasPool::IsPoolAlias(*base)) {
                continue;
            }
            const auto* refAlias = skyrim_cast<const RE::BGSRefAlias*>(base);
            if (refAlias && refAlias->GetActorReference() == actor) {
                return base->aliasName.c_str();
            }
        }
        return "-";
    }

    void DrawActorsTab(GK::GameState& a_state) {
        static char filter[64] = "";
        static RE::FormID selected = 0;

        const auto& records = a_state.Actors().Records();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Filter", filter, sizeof(filter));
        ImGui::SameLine();
        ImGui::Text("%zu tracked", records.size());

        constexpr auto tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                    ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##actors", 6, tableFlags, ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Roles");
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Alias");
            ImGui::TableSetupColumn("3D", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableHeadersRow();

            for (const auto& [id, rec] : records) {
                auto* form = RE::TESForm::LookupByID(id);
                auto* actor = form ? form->As<RE::Actor>() : nullptr;
                const char* name = actor ? actor->GetDisplayFullName() : "(unresolved)";

                // Roles summary: global roles + Role@lab for each association.
                std::string roles;
                if (rec.globalRoles != GK::Role::kNone) {
                    roles = RoleNames(rec.globalRoles);
                }
                for (const auto& [lab, mask] : rec.rolesByLab) {
                    if (!roles.empty()) {
                        roles += ", ";
                    }
                    roles += fmt::format("{}@{:08X}", RoleNames(mask), lab);
                }
                if (roles.empty()) {
                    roles = "-";
                }

                if (!MatchesFilter(fmt::format("{:08X} {}", id, name), filter)) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(HexID(id).c_str(), selected == id,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected = (selected == id) ? 0 : id;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(roles.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(rec.status.empty() ? "-" : rec.status.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(AliasColumnFor(a_state, id).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(actor && actor->Is3DLoaded() ? "yes" : "no");
            }
            ImGui::EndTable();
        }

        if (selected != 0) {
            const auto it = records.find(selected);
            if (it != records.end()) {
                DrawActorDetail(a_state, selected, it->second);
            } else {
                selected = 0;
            }
        } else {
            ImGui::TextDisabled("(click a row for detail)");
        }
    }

    void DrawLabyrinthsTab(GK::GameState& a_state) {
        const auto& anchors = a_state.Labyrinths().All();
        if (anchors.empty()) {
            ImGui::TextUnformatted("No labyrinths registered.");
            return;
        }
        auto& reg = a_state.Resources();
        for (const auto& [anchor, factions] : anchors) {
            if (!ImGui::TreeNode(RefLabel(anchor).c_str())) {
                continue;
            }
            ImGui::Text("factions: warden %s   prisoner %s",
                        factions.warden ? HexID(factions.warden->GetFormID()).c_str() : "(none)",
                        factions.prisoner ? HexID(factions.prisoner->GetFormID()).c_str() : "(none)");
            // MoveTo must run on the main thread, not this render-hook thread.
            if (ImGui::Button("Teleport player to anchor")) {
                SKSE::GetTaskInterface()->AddTask([anchorID = anchor]() {
                    auto* form = RE::TESForm::LookupByID(anchorID);
                    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (ref && player) {
                        player->MoveTo(ref);
                    } else {
                        logger::warn("DebugOverlay: teleport failed, anchor {:08X} did not resolve.", anchorID);
                    }
                });
            }
            ImGui::SeparatorText("Cells");
            for (const auto& [handle, cell] : reg.CellPool().All()) {
                if (cell.labyrinth != anchor) {
                    continue;
                }
                ImGui::Text("#%d door %s   occ %zu/%u   flags \"%s\"   in %s   out %s", handle,
                            RefLabel(cell.door).c_str(), cell.occupants.size(), cell.maxOccupants,
                            cell.flags.c_str(), HexID(cell.inMarker).c_str(), HexID(cell.outMarker).c_str());
            }
            ImGui::SeparatorText("Patrol markers");
            for (const auto& [handle, marker] : reg.MarkerPool().All()) {
                if (marker.labyrinth != anchor) {
                    continue;
                }
                ImGui::Text("#%d %s   occ %zu/%u", handle, RefLabel(marker.ref).c_str(), marker.occupants.size(),
                            marker.maxOccupants);
            }
            ImGui::SeparatorText("Furniture");
            for (const auto& [handle, furn] : reg.FurniturePool().All()) {
                if (furn.labyrinth != anchor) {
                    continue;
                }
                ImGui::Text("#%d %s   occ %zu/%u", handle, RefLabel(furn.ref).c_str(), furn.occupants.size(),
                            furn.maxOccupants);
            }
            ImGui::SeparatorText("Wardens");
            for (const auto wardenID : a_state.Actors().GetByRole(anchor, GK::Role::kWarden)) {
                ImGui::TextUnformatted(RefLabel(wardenID).c_str());
            }
            ImGui::TreePop();
        }
    }

    void DrawAliasPoolTab(GK::GameState& a_state) {
        auto* quest = a_state.AliasQuest();
        if (!quest) {
            ImGui::TextUnformatted("No alias quest configured (ConfigureAliasQuest).");
            return;
        }
        ImGui::Text("quest: %s", RefLabel(quest->GetFormID()).c_str());

        const auto now = std::chrono::steady_clock::now();
        std::uint32_t total = 0;
        std::uint32_t filled = 0;
        std::uint32_t reserved = 0;

        constexpr auto tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##aliases", 3, tableFlags, ImVec2(0.0f, 260.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Alias", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Holder");
            ImGui::TableHeadersRow();

            const RE::BSReadLockGuard guard{quest->aliasAccessLock};
            for (const auto* base : quest->aliases) {
                if (!base || !GK::AliasPool::IsPoolAlias(*base)) {
                    continue;
                }
                ++total;
                const auto* refAlias = skyrim_cast<const RE::BGSRefAlias*>(base);
                const auto* holder = refAlias ? refAlias->GetActorReference() : nullptr;

                std::string holderLabel = "(empty)";
                if (holder) {
                    ++filled;
                    holderLabel = RefLabel(holder->GetFormID());
                } else {
                    const auto key = GK::AliasPool::Key(*quest, base->aliasID);
                    const auto it = a_state.AliasReservations().find(key);
                    if (it != a_state.AliasReservations().end() && now < it->second.expiry) {
                        ++reserved;
                        holderLabel = "(reserved - fill pending)";
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(base->aliasName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", base->aliasID);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(holderLabel.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::Text("%u total   %u filled   %u reserved   %u free", total, filled, reserved,
                    total - filled - reserved);
    }

    void DrawQueuesTab(GK::GameState& a_state) {
        const auto& queues = a_state.Queues().Queues();
        const auto& delayed = a_state.Queues().Delayed();
        if (queues.empty() && delayed.empty()) {
            ImGui::TextUnformatted("No queues (a queue exists only while actors wait in it).");
            return;
        }
        ImGui::Text("%zu queue(s)", queues.size());
        for (const auto& [name, entries] : queues) {
            if (!ImGui::TreeNode(name.c_str(), "%s (%zu waiting)", name.c_str(), entries.size())) {
                continue;
            }
            // Front-to-back; #0 is what the next DequeueActor returns (unresolved
            // entries get skipped there, so flag them here).
            int pos = 0;
            for (const auto id : entries) {
                auto* form = RE::TESForm::LookupByID(id);
                auto* actor = form ? form->As<RE::Actor>() : nullptr;
                const char* note = !actor ? "  (unresolved - skipped at dequeue)" : (actor->IsDead() ? "  (dead)" : "");
                ImGui::Text("#%d%s %s%s", pos, pos == 0 ? " (next)" : "", RefLabel(id).c_str(), note);
                ++pos;
            }
            ImGui::TreePop();
        }
        if (!delayed.empty()) {
            // Read-only view: due entries join their queue on the next queue call
            // from Papyrus (PromoteDue), not from here -- hence "due" once ripe.
            ImGui::SeparatorText("Delayed enqueues");
            const auto now = GK::NowSeconds();
            for (const auto& entry : delayed) {
                const double left = entry.due - now;
                if (left > 0.0) {
                    ImGui::Text("%s -> \"%s\" in %.1f s", RefLabel(entry.actor).c_str(), entry.queue.c_str(), left);
                } else {
                    ImGui::Text("%s -> \"%s\" (due)", RefLabel(entry.actor).c_str(), entry.queue.c_str());
                }
            }
        }
    }

    // "<hex> EditorID [Type]" for a package; editor IDs are usually empty at
    // runtime (the engine drops them) and alias fills run on engine-instanced
    // copies with dynamic FF-range FormIDs, so both are flagged rather than
    // relied on.
    std::string PackageLabel(const RE::TESPackage& a_pkg) {
        std::string label = HexID(a_pkg.GetFormID());
        if (const char* edid = a_pkg.GetFormEditorID(); edid && edid[0]) {
            label += fmt::format(" {}", edid);
        }
        if (const char* type = a_pkg.GetObjectTypeName(); type && type[0]) {
            label += fmt::format(" [{}]", type);
        }
        if (a_pkg.GetFormID() >= 0xFF000000) {
            label += " (instanced)";
        }
        return label;
    }

    void DrawActorPackageDetail(RE::Actor& a_actor) {
        // FormIDs seen in the base/alias lists, to classify the current package.
        std::vector<RE::FormID> listed;

        ImGui::SeparatorText("Base package list (ActorBase, PKID)");
        // Non-const: BSSimpleList's const begin() doesn't compile (constructs its
        // iterator from a const Node*).
        auto* npc = a_actor.GetActorBase();
        bool any = false;
        if (npc) {
            for (const auto* pkg : npc->aiPackages.packages) {
                if (!pkg) {
                    continue;
                }
                any = true;
                listed.push_back(pkg->GetFormID());
                ImGui::Text("  %s", PackageLabel(*pkg).c_str());
            }
        }
        if (!any) {
            ImGui::TextDisabled("  (empty)");
        }

        ImGui::SeparatorText("Alias-injected packages (override the base list)");
        const auto* aliasArray = a_actor.extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!aliasArray || aliasArray->aliases.empty()) {
            ImGui::TextDisabled("  (no alias instances on this reference)");
        } else {
            const RE::BSReadLockGuard guard{aliasArray->lock};
            for (const auto* inst : aliasArray->aliases) {
                if (!inst) {
                    continue;
                }
                std::string questLabel = "(null quest)";
                if (inst->quest) {
                    const char* qid = inst->quest->GetFormEditorID();
                    questLabel = fmt::format("{:08X} {}", inst->quest->GetFormID(),
                                             (qid && qid[0]) ? qid : inst->quest->GetName());
                }
                std::string aliasLabel = "(null alias)";
                if (inst->alias) {
                    aliasLabel = inst->alias->aliasName.empty()
                                     ? fmt::format("#{}", inst->alias->aliasID)
                                     : std::string(inst->alias->aliasName.c_str());
                }
                ImGui::Text("%s / %s", questLabel.c_str(), aliasLabel.c_str());
                if (!inst->instancedPackages || inst->instancedPackages->empty()) {
                    ImGui::TextDisabled("    (no packages)");
                    continue;
                }
                for (const auto* pkg : *inst->instancedPackages) {
                    if (!pkg) {
                        continue;
                    }
                    listed.push_back(pkg->GetFormID());
                    ImGui::Text("    %s", PackageLabel(*pkg).c_str());
                }
            }
        }

        ImGui::SeparatorText("Current (running) package");
        const auto* current = a_actor.GetCurrentPackage();
        if (!current) {
            ImGui::TextDisabled("  (none)");
        } else {
            ImGui::Text("  %s", PackageLabel(*current).c_str());
            const bool fromLists =
                std::find(listed.begin(), listed.end(), current->GetFormID()) != listed.end();
            if (!fromLists) {
                // Not in either enumerable source: scene/combat package, or an
                // eval-hook override (e.g. PapyrusUtil AddPackageOverride).
                ImGui::TextDisabled("  (not in base/alias lists: scene, combat, or an override "
                                    "such as ActorUtil.AddPackageOverride)");
            }
        }
    }

    void DrawPackagesTab(GK::GameState& a_state) {
        static RE::FormID selected = 0;

        // Candidates: the player, then every actor held by a pool alias.
        std::vector<std::pair<RE::FormID, std::string>> candidates;
        if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
            candidates.emplace_back(player->GetFormID(),
                                    fmt::format("{} (player)", RefLabel(player->GetFormID())));
        }
        if (auto* quest = a_state.AliasQuest()) {
            const RE::BSReadLockGuard guard{quest->aliasAccessLock};
            for (const auto* base : quest->aliases) {
                if (!base || !GK::AliasPool::IsPoolAlias(*base)) {
                    continue;
                }
                const auto* refAlias = skyrim_cast<const RE::BGSRefAlias*>(base);
                const auto* holder = refAlias ? refAlias->GetActorReference() : nullptr;
                if (!holder) {
                    continue;
                }
                candidates.emplace_back(holder->GetFormID(), fmt::format("{}  [{}]", RefLabel(holder->GetFormID()),
                                                                         base->aliasName.c_str()));
            }
        }

        if (ImGui::BeginChild("##pkg_actors", ImVec2(280.0f, 0.0f), ImGuiChildFlags_Borders)) {
            for (const auto& [id, label] : candidates) {
                if (ImGui::Selectable(label.c_str(), selected == id)) {
                    selected = (selected == id) ? 0 : id;
                }
            }
            if (candidates.empty()) {
                ImGui::TextDisabled("(no player / pool holders)");
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        if (ImGui::BeginChild("##pkg_detail", ImVec2(0.0f, 0.0f))) {
            auto* form = selected ? RE::TESForm::LookupByID(selected) : nullptr;
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            if (actor) {
                DrawActorPackageDetail(*actor);
            } else {
                ImGui::TextDisabled(selected ? "(FormID no longer resolves to an actor)"
                                             : "(select an actor on the left)");
            }
        }
        ImGui::EndChild();
    }

    void DrawSessionTab(GK::GameState& a_state) {
        ImGui::SeparatorText("Config");
        const auto& kw = a_state.Keywords();
        const auto kwLabel = [](const RE::BGSKeyword* a_kw) {
            return a_kw ? HexID(a_kw->GetFormID()) : std::string("(not set)");
        };
        ImGui::Text("keywords valid: %s", kw.Valid() ? "yes" : "NO");
        ImGui::Text("  cellDoor %s   patrolMarker %s   furniture %s", kwLabel(kw.cellDoor).c_str(),
                    kwLabel(kw.patrolMarker).c_str(), kwLabel(kw.furniture).c_str());
        ImGui::Text("  inMarker %s   outMarker %s   warden %s   wanderer %s", kwLabel(kw.inMarker).c_str(),
                    kwLabel(kw.outMarker).c_str(), kwLabel(kw.warden).c_str(), kwLabel(kw.wanderer).c_str());
        ImGui::Text("alias quest: %s",
                    a_state.AliasQuest() ? HexID(a_state.AliasQuest()->GetFormID()).c_str() : "(not set)");

        ImGui::SeparatorText("Counts");
        ImGui::Text("actors: %zu   labyrinths: %zu   queues: %zu", a_state.Actors().Records().size(),
                    a_state.Labyrinths().All().size(), a_state.Queues().Queues().size());
        auto& reg = a_state.Resources();
        ImGui::Text("cells: %zu   markers: %zu   furniture: %zu   next handle: %d", reg.CellPool().All().size(),
                    reg.MarkerPool().All().size(), reg.FurniturePool().All().size(), reg.PeekNextHandle());

        ImGui::SeparatorText("Actions");
        static int lastScan = -1;
        if (ImGui::Button("Run ScanAllLabyrinths")) {
            lastScan = GK::ScanAllForms();  // safe/idempotent; re-locks (recursive)
        }
        if (lastScan >= 0) {
            ImGui::SameLine();
            ImGui::Text("last scan matched %d resource(s)", lastScan);
        }
    }

    void Draw() {
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_FirstUseEver);
        bool open = true;
        if (!ImGui::Begin("Gordian Knot", &open)) {
            ImGui::End();
            return;
        }
        if (!open) {  // titlebar [x]
            g_visible.store(false, std::memory_order_relaxed);
        }

        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();

        if (ImGui::BeginTabBar("##gk_tabs")) {
            if (ImGui::BeginTabItem("Actors")) {
                DrawActorsTab(*state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Labyrinths")) {
                DrawLabyrinthsTab(*state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Alias Pool")) {
                DrawAliasPoolTab(*state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Queues")) {
                DrawQueuesTab(*state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Packages")) {
                DrawPackagesTab(*state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Session")) {
                DrawSessionTab(*state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // --- hooks ----------------------------------------------------------------------

    // Runs right after the game creates its D3D11 device + swap chain.
    struct D3DInitHook {
        static void thunk() {
            func();

            auto* manager = RE::BSRenderManager::GetSingleton();
            if (!manager) {
                logger::error("DebugOverlay: BSRenderManager unavailable; overlay disabled.");
                return;
            }
            const auto& data = manager->GetRuntimeData();
            if (!data.forwarder || !data.context || !data.swapChain) {
                logger::error("DebugOverlay: D3D device/context/swapchain missing; overlay disabled.");
                return;
            }
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(data.swapChain->GetDesc(&desc))) {
                logger::error("DebugOverlay: failed to query swap chain; overlay disabled.");
                return;
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width),
                                    static_cast<float>(desc.BufferDesc.Height));
            io.MouseDrawCursor = true;  // the game hides the OS cursor; draw our own
            io.IniFilename = nullptr;   // no imgui.ini next to the game exe
            ImGui_ImplDX11_Init(data.forwarder, data.context);

            g_swapChain = data.swapChain;
            g_mouseX = io.DisplaySize.x * 0.5f;
            g_mouseY = io.DisplaySize.y * 0.5f;
            g_imguiReady = true;
            logger::info("DebugOverlay: ImGui initialized ({}x{}). Toggle with Ctrl+Shift+G.",
                         desc.BufferDesc.Width, desc.BufferDesc.Height);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // Runs every frame at present time; ticks the pause-respecting session clock
    // and renders the overlay when visible.
    struct PresentHook {
        static void thunk(std::uint32_t a_p1) {
            func(a_p1);
            GK::GameClock::Tick();  // per-frame, independent of overlay visibility
            if (!g_imguiReady || !g_visible.load(std::memory_order_relaxed)) {
                return;
            }

            auto& io = ImGui::GetIO();
            // Track window resizes (cheap; once per frame while visible).
            DXGI_SWAP_CHAIN_DESC desc{};
            if (g_swapChain && SUCCEEDED(g_swapChain->GetDesc(&desc))) {
                io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width),
                                        static_cast<float>(desc.BufferDesc.Height));
            }
            static auto last = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            io.DeltaTime = std::max(std::chrono::duration<float>(now - last).count(), 1.0e-4f);
            last = now;

            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            Draw();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // Taps the game's input-event dispatch: watches for the toggle combo, feeds
    // ImGui, and filters events the panel consumes out of the game's stream.
    struct InputDispatchHook {
        // Returns true if the event should be consumed (not forwarded to the game).
        static bool HandleEvent(RE::InputEvent* a_event) {
            const bool visible = g_visible.load(std::memory_order_relaxed);
            auto& io = ImGui::GetIO();

            if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
                if (!visible || !g_imguiReady) {
                    return false;
                }
                const auto* move = static_cast<const RE::MouseMoveEvent*>(a_event);
                g_mouseX = std::clamp(g_mouseX + static_cast<float>(move->mouseInputX), 0.0f, io.DisplaySize.x);
                g_mouseY = std::clamp(g_mouseY + static_cast<float>(move->mouseInputY), 0.0f, io.DisplaySize.y);
                io.AddMousePosEvent(g_mouseX, g_mouseY);
                return true;  // panel owns the mouse
            }

            const auto* button = a_event->AsButtonEvent();
            if (!button) {
                return false;  // thumbstick/char/etc.: always the game's
            }
            const auto code = button->GetIDCode();

            if (a_event->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                // Modifier tracking runs regardless of visibility.
                if (code == kDIK_LCtrl || code == kDIK_RCtrl) {
                    g_ctrlHeld = button->IsPressed();
                } else if (code == kDIK_LShift || code == kDIK_RShift) {
                    g_shiftHeld = button->IsPressed();
                }
                // The toggle always wins (even while a text field is focused).
                if (code == kDIK_G && button->IsDown() && g_ctrlHeld && g_shiftHeld) {
                    const bool nowVisible = !g_visible.load(std::memory_order_relaxed);
                    g_visible.store(nowVisible, std::memory_order_relaxed);
                    if (nowVisible && g_imguiReady) {
                        g_mouseX = io.DisplaySize.x * 0.5f;
                        g_mouseY = io.DisplaySize.y * 0.5f;
                        io.AddMousePosEvent(g_mouseX, g_mouseY);
                    }
                    return true;
                }
                if (!visible || !g_imguiReady) {
                    return false;
                }
                // Keyboard goes to the panel only while it wants text (filter box).
                if (io.WantTextInput) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, g_ctrlHeld);
                    io.AddKeyEvent(ImGuiMod_Shift, g_shiftHeld);
                    if (const auto key = DIKToImGuiKey(code); key != ImGuiKey_None) {
                        io.AddKeyEvent(key, button->IsPressed());
                    }
                    if (button->IsDown()) {
                        if (const char c = DIKToChar(code, g_shiftHeld); c != 0) {
                            io.AddInputCharacter(static_cast<unsigned int>(c));
                        }
                    }
                    return true;
                }
                return false;  // game keeps the keyboard
            }

            if (a_event->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                if (!visible || !g_imguiReady) {
                    return false;
                }
                if (code == kMouse_WheelUp || code == kMouse_WheelDown) {
                    if (button->IsDown()) {
                        io.AddMouseWheelEvent(0.0f, code == kMouse_WheelUp ? 1.0f : -1.0f);
                    }
                } else if (code <= 2) {  // 0 left / 1 right / 2 middle
                    io.AddMouseButtonEvent(static_cast<int>(code), button->IsPressed());
                }
                return true;  // panel owns the mouse
            }

            return false;
        }

        static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events) {
            if (!a_events || !*a_events) {
                func(a_source, a_events);
                return;
            }

            // Filter the linked list, keeping only events the panel didn't consume.
            RE::InputEvent* head = nullptr;
            RE::InputEvent** tail = &head;
            RE::InputEvent* next = nullptr;
            for (auto* event = *a_events; event; event = next) {
                next = event->next;
                if (!HandleEvent(event)) {
                    *tail = event;
                    tail = &event->next;
                }
            }
            *tail = nullptr;

            RE::InputEvent* list[] = {head};  // head may be nullptr (everything consumed)
            func(a_source, list);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
}

namespace GK::UI {
    void Install() {
        if (REL::Module::IsVR()) {
            logger::info("DebugOverlay: VR runtime detected; overlay disabled.");
            return;
        }

        SKSE::AllocTrampoline(14 * 3);
        auto& trampoline = SKSE::GetTrampoline();

        // Community-established call sites (same as dMenu/Wheeler-style overlays):
        // D3D init, DXGI present, and the input-event dispatch.
        const REL::Relocation<std::uintptr_t> d3dInit{REL::RelocationID(75595, 77226),
                                                      REL::VariantOffset(0x9, 0x275, 0x0)};
        D3DInitHook::func = trampoline.write_call<5>(d3dInit.address(), D3DInitHook::thunk);

        const REL::Relocation<std::uintptr_t> present{REL::RelocationID(75461, 77246),
                                                      REL::VariantOffset(0x9, 0x9, 0x0)};
        PresentHook::func = trampoline.write_call<5>(present.address(), PresentHook::thunk);

        const REL::Relocation<std::uintptr_t> input{REL::RelocationID(67315, 68617),
                                                    REL::VariantOffset(0x7B, 0x7B, 0x0)};
        InputDispatchHook::func = trampoline.write_call<5>(input.address(), InputDispatchHook::thunk);

        logger::info("DebugOverlay: hooks installed (toggle: Ctrl+Shift+G).");
    }
}
