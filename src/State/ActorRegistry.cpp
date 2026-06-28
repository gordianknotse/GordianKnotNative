#include "State/ActorRegistry.h"

namespace GK {
    void ActorRegistry::SetRoles(RE::FormID a_actor, std::uint32_t a_roles) {
        GetOrCreate(a_actor).roles = a_roles;
    }

    void ActorRegistry::AddRole(RE::FormID a_actor, std::uint32_t a_role) {
        GetOrCreate(a_actor).roles |= a_role;
    }

    void ActorRegistry::RemoveRole(RE::FormID a_actor, std::uint32_t a_role) {
        GetOrCreate(a_actor).roles &= ~a_role;
    }

    std::uint32_t ActorRegistry::GetRoles(RE::FormID a_actor) const {
        const auto it = _records.find(a_actor);
        return it != _records.end() ? it->second.roles : Role::kNone;
    }

    void ActorRegistry::SetStatus(RE::FormID a_actor, std::int32_t a_status) {
        GetOrCreate(a_actor).status = a_status;
    }

    std::int32_t ActorRegistry::GetStatus(RE::FormID a_actor) const {
        const auto it = _records.find(a_actor);
        return it != _records.end() ? it->second.status : Status::kIdle;
    }

    std::vector<RE::FormID> ActorRegistry::GetByRole(std::uint32_t a_roleMask) const {
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            if ((rec.roles & a_roleMask) != 0) {
                out.push_back(id);
            }
        }
        return out;
    }

    std::vector<RE::FormID> ActorRegistry::GetByStatus(std::int32_t a_status) const {
        std::vector<RE::FormID> out;
        for (const auto& [id, rec] : _records) {
            if (rec.status == a_status) {
                out.push_back(id);
            }
        }
        return out;
    }

    void ActorRegistry::Forget(RE::FormID a_actor) { _records.erase(a_actor); }

    void ActorRegistry::Clear() { _records.clear(); }
}
