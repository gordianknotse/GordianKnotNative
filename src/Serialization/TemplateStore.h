#pragma once

namespace GK::Serialization {
    // Cross-save persistence for TEMPLATE outfits (owner FormID 0): a JSON file
    // at Data/SKSE/Plugins/GordianKnot/templates.json shared by every
    // character/save. Devices are stored as "Plugin.esp|0xLocalID" (load-order
    // independent, ESL-aware); dynamic (save-local) forms cannot cross saves
    // and are skipped on save. Each template carries a reserved "tags" field
    // for future context tagging.
    //
    // Loading MERGES over the in-memory templates: file templates replace
    // same-named ones, templates that exist only in this save's co-save
    // survive. LoadTemplateFile runs automatically on every game load, AFTER
    // the co-save (disk wins by name); saving is explicit (Papyrus binding).

    // Returns the number of templates written, -1 on I/O failure.
    std::int32_t SaveTemplateFile();

    // Returns the number of templates loaded, -1 when the file is absent or
    // unreadable/malformed.
    std::int32_t LoadTemplateFile();
}
