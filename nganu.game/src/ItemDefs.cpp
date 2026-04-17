#include "ItemDefs.h"

#include <sstream>
#include <algorithm>
#include <cctype>

/* ------------------------------------------------------------------ */
/* Minimal JSON helpers (no external lib dependency)                  */
/* Handles the flat array format used in item_defs.json               */
/* ------------------------------------------------------------------ */
namespace {

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(start, end - start + 1);
}

/* Extract named string value from a JSON object fragment */
std::string ExtractStr(const std::string& obj, const std::string& key) {
    const std::string search = "\"" + key + "\"";
    auto pos = obj.find(search);
    if (pos == std::string::npos) return {};
    pos = obj.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) ++pos;
    if (pos >= obj.size()) return {};
    if (obj[pos] == '"') {
        ++pos;
        auto end = obj.find('"', pos);
        if (end == std::string::npos) return {};
        return obj.substr(pos, end - pos);
    }
    /* numeric or other bare value */
    auto end = obj.find_first_of(",}\n", pos);
    return Trim(obj.substr(pos, end - pos));
}

/* Split JSON array into object fragments between { } */
std::vector<std::string> SplitObjects(const std::string& json) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = 0;
    bool inString = false;
    for (size_t i = 0; i < json.size(); ++i) {
        if (inString) {
            if (json[i] == '\\') { ++i; continue; }
            if (json[i] == '"') inString = false;
            continue;
        }
        if (json[i] == '"') { inString = true; continue; }
        if (json[i] == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) objects.push_back(json.substr(start, i - start + 1));
        }
    }
    return objects;
}

} // anonymous namespace

/* ------------------------------------------------------------------ */
void ItemDefs::LoadFromJson(const std::string& json) {
    defs_.clear();
    lookup_.clear();

    for (const auto& obj : SplitObjects(json)) {
        ItemDef def;
        try {
            const std::string idStr = ExtractStr(obj, "id");
            if (idStr.empty()) continue;
            def.id       = std::stoi(idStr);
            def.name     = ExtractStr(obj, "name");
            def.icon_key = ExtractStr(obj, "icon");
            def.rarity   = ExtractStr(obj, "rarity");
            def.category = ExtractStr(obj, "category");
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
