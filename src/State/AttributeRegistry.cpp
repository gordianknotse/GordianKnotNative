#include "State/AttributeRegistry.h"

#include "State/CaseFold.h"

namespace GK {
    void AttributeRegistry::Set(RE::FormID a_actor, std::string_view a_key, RE::FormID a_value) {
        if (a_value == 0) {
            const auto it = _attributes.find(a_actor);
            if (it == _attributes.end()) {
                return;
            }
            it->second.erase(FoldCase(a_key));
            if (it->second.empty()) {
                _attributes.erase(it);  // keep the invariant: no empty attribute maps stored
            }
            return;
        }
        _attributes[a_actor][FoldCase(a_key)] = a_value;
    }

    RE::FormID AttributeRegistry::Get(RE::FormID a_actor, std::string_view a_key) const {
        const auto it = _attributes.find(a_actor);
        if (it == _attributes.end()) {
            return 0;
        }
        const auto attr = it->second.find(FoldCase(a_key));
        return attr != it->second.end() ? attr->second : 0;
    }

    void AttributeRegistry::Put(RE::FormID a_actor, AttributeMap a_attributes) {
        if (a_attributes.empty()) {
            return;
        }
        _attributes[a_actor] = std::move(a_attributes);
    }
}
