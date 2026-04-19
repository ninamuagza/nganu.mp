#include "ItemDefs.h"
#include "shared/JsonRuntime.h"

/* ------------------------------------------------------------------ */
void ItemDefs::LoadFromJson(const std::string& json) {
    defs_.clear();
    lookup_.clear();

    for (const auto& obj : Nganu::JsonRuntime::SplitTopLevelObjects(json)) {
        const auto fields = Nganu::JsonRuntime::ParseFlatObject(obj);
        ItemDef def;
        try {
            const auto id = Nganu::JsonRuntime::GetInt(fields, "id");
            if (!id.has_value()) continue;
            def.id       = *id;
            def.name     = Nganu::JsonRuntime::GetString(fields, "name").value_or("");
            def.icon_key = Nganu::JsonRuntime::GetString(fields, "icon").value_or("");
            def.rarity   = Nganu::JsonRuntime::GetString(fields, "rarity").value_or("");
            def.category = Nganu::JsonRuntime::GetString(fields, "category").value_or("");
        } catch (...) {
            continue;
        }
        if (def.id <= 0) continue;
        lookup_[def.id] = defs_.size();
        defs_.push_back(std::move(def));
    }
}

const ItemDef* ItemDefs::Find(int id) const {
    auto it = lookup_.find(id);
    if (it == lookup_.end()) return nullptr;
    return &defs_[it->second];
}

std::string ItemDefs::UnknownName(int id) {
    return "Item #" + std::to_string(id);
}
