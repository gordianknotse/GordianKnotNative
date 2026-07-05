#include "State/ActorRegistry.h"

#include "State/CaseFold.h"

namespace GK {
    void ActorRegistry::AddGlobalRole(RE::FormID a_actor, std::uint32_t a_role) {
        GetOrCreate(a_actor).globalRoles |= a_role;  // creates the actor even if a_role is 0
    }

    void ActorRegistry::RemoveGlobalRole(RE::FormID a_actor, std::uint32_t a_role) {
        const auto it = _records.find(a_actor);
        if (it != _records.end()) {  // clearing never tracks: untracked actor is a no-op
            it->second.globalRoles &= ~a_role;
        }
    }

    std::uint32_t ActorRegistry::GetGlobalRoles(RE::FormID a_actor) const {
        const auto it = _records.find(a_actor);
        return it != _records.end() ? it->second.globalRoles : Role::kNone;
    }

    std::vector<RE::FormID> ActorRegistry::GetByGlobalRole(std::uint32_t a_roleMask) const {
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            if ((rec.globalRoles & a_roleMask) != 0) {
                out.push_back(id);
            }
        }
        return out;
    }

    void ActorRegistry::SetRoles(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_roles) {
        if (a_roles == Role::kNone) {
            // Clearing every role drops the association; never tracks an untracked actor.
            const auto it = _records.find(a_actor);
            if (it != _records.end()) {
                it->second.rolesByLab.erase(a_lab);
            }
        } else {
            GetOrCreate(a_actor).rolesByLab[a_lab] = a_roles;
        }
    }

    void ActorRegistry::AddRole(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_role) {
        auto& rec = GetOrCreate(a_actor);  // ensure the actor is tracked
        if (a_role != Role::kNone) {
            rec.rolesByLab[a_lab] |= a_role;  // avoid creating an empty (0-mask) association
        }
    }

    void ActorRegistry::RemoveRole(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_role) {
        const auto it = _records.find(a_actor);
        if (it == _records.end()) {
            return;  // clearing never tracks: untracked actor is a no-op
        }
        auto& byLab = it->second.rolesByLab;
        const auto lit = byLab.find(a_lab);
        if (lit == byLab.end()) {
            return;
        }
        lit->second &= ~a_role;
        if (lit->second == Role::kNone) {
            byLab.erase(lit);  // last role in this labyrinth cleared -> drop it
        }
    }

    std::uint32_t ActorRegistry::GetRoles(RE::FormID a_actor, RE::FormID a_lab) const {
        const auto it = _records.find(a_actor);
        if (it == _records.end()) {
            return Role::kNone;
        }
        const auto lit = it->second.rolesByLab.find(a_lab);
        return lit != it->second.rolesByLab.end() ? lit->second : Role::kNone;
    }

    void ActorRegistry::SetStatus(RE::FormID a_actor, std::string_view a_status) {
        GetOrCreate(a_actor).status = Status::Normalize(a_status);
    }

    void ActorRegistry::ClearStatus(RE::FormID a_actor) {
        const auto it = _records.find(a_actor);
        if (it != _records.end()) {  // clearing never tracks: untracked actor is a no-op
            it->second.status = Status::kIdle;
        }
    }

    std::string ActorRegistry::GetStatus(RE::FormID a_actor) const {
        const auto it = _records.find(a_actor);
        return it != _records.end() ? it->second.status : std::string(Status::kIdle);
    }

    std::vector<RE::FormID> ActorRegistry::GetByRole(RE::FormID a_lab, std::uint32_t a_roleMask) const {
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            const auto lit = rec.rolesByLab.find(a_lab);
            if (lit != rec.rolesByLab.end() && (lit->second & a_roleMask) != 0) {
                out.push_back(id);
            }
        }
        return out;
    }

    std::vector<RE::FormID> ActorRegistry::GetByRoleAnywhere(std::uint32_t a_roleMask) const {
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            for (const auto& [lab, mask] : rec.rolesByLab) {
                if ((mask & a_roleMask) != 0) {
                    out.push_back(id);  // once per actor, however many labyrinths match
                    break;
                }
            }
        }
        return out;
    }

    std::vector<RE::FormID> ActorRegistry::GetByStatus(std::string_view a_status) const {
        const auto folded = FoldCase(Status::Normalize(a_status));
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            if (FoldCase(rec.status) == folded) {
                out.push_back(id);
            }
        }
        return out;
    }

    std::vector<RE::FormID> ActorRegistry::GetLabyrinths(RE::FormID a_actor) const {
        std::vector<RE::FormID> out;
        const auto it = _records.find(a_actor);
        if (it == _records.end()) {
            return out;
        }
        out.reserve(it->second.rolesByLab.size());
        for (const auto& [lab, mask] : it->second.rolesByLab) {
            out.push_back(lab);
        }
        return out;
    }

    std::vector<RE::FormID> ActorRegistry::GetLabyrinthsByRole(RE::FormID a_actor, std::uint32_t a_roleMask) const {
        std::vector<RE::FormID> out;
        const auto it = _records.find(a_actor);
        if (it == _records.end()) {
            return out;
        }
        for (const auto& [lab, mask] : it->second.rolesByLab) {
            if ((mask & a_roleMask) != 0) {
                out.push_back(lab);
            }
        }
        return out;
    }

    bool ActorRegistry::HasRoleAnywhere(RE::FormID a_actor, std::uint32_t a_roleMask) const {
        const auto it = _records.find(a_actor);
        if (it == _records.end()) {
            return false;
        }
        if ((it->second.globalRoles & a_roleMask) != 0) {
            return true;  // global roles (e.g. Wanderer) count as "anywhere"
        }
        for (const auto& [lab, mask] : it->second.rolesByLab) {
            if ((mask & a_roleMask) != 0) {
                return true;
            }
        }
        return false;
    }

    void ActorRegistry::Forget(RE::FormID a_actor) { _records.erase(a_actor); }

    void ActorRegistry::Clear() { _records.clear(); }
}
