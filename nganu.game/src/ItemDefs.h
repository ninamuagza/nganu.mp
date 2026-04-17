#pragma once

/* ------------------------------------------------------------------ */
/* ItemDefs.h — display-side definitions loaded from item_defs.json   */
/* No external JSON library; uses a minimal hand-written parser.      */
/* ------------------------------------------------------------------ */

#include <string>
#include <unordered_map>
#include <vector>

struct ItemDef {
    int         id       = 0;
    std::string name;
    std::string icon_key;   /* e.g. "ui:icons/potion_health.png" */
    std::string rarity;     /* "common", "uncommon", "rare", ... */
    std::string category;
};

class ItemDefs {
public:
    /* Parse item_defs.json content (as delivered by ASSET_BLOB) */
    void LoadFromJson(const std::string& json);

    /* Find by item_def_id. Returns nullptr if not found. */
    const ItemDef* Find(int id) const;

    /* Fallback display name when no definition exists */
    static std::string UnknownName(int id);

    size_t Count() const { return defs_.size(); }
    void Clear() { defs_.clear(); lookup_.clear(); }

private:
    std::vector<ItemDef>            defs_;
    std::unordered_map<int, size_t> lookup_; /* id → index in defs_ */
};
