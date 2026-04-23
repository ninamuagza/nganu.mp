#include "MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace Nganu {
namespace MapFormat {
namespace {

ParseResult Fail(int line, std::string error) {
    ParseResult result;
    result.line = line;
    result.error = std::move(error);
    return result;
}

void AddAssetRefForDomain(std::vector<std::string>& mapRefs,
                          std::vector<std::string>& characterRefs,
                          const std::string& ref,
                          AssetDomain fallbackDomain) {
    const std::string file = AssetFileName(ref);
    if (file.empty()) {
        return;
    }
    const AssetDomain domain = AssetDomainForRef(ref, fallbackDomain);
    AddUniqueAssetRef(domain == AssetDomain::Character ? characterRefs : mapRefs, file);
}

std::string DomainPrefix(AssetDomain domain) {
    return domain == AssetDomain::Character ? "character:" : "map:";
}

std::optional<std::string> ResolveAssetRef(const std::string& value,
                                           AssetDomain fallbackDomain,
                                           const std::unordered_map<std::string, std::pair<AssetDomain, std::string>>& assets) {
    const std::vector<std::string> parts = SplitEscaped(value, '@');
    if (parts.size() == 5) {
        auto assetIt = assets.find(parts[0]);
        if (assetIt != assets.end()) {
            return DomainPrefix(assetIt->second.first) + assetIt->second.second + "@" + parts[1] + "@" + parts[2] + "@" + parts[3] + "@" + parts[4];
        }
    }

    if (ParseAtlasRef(value).has_value()) {
        return value;
    }

    if (parts.size() != 5) {
        return std::nullopt;
    }

    if (parts[0].find(".png") != std::string::npos) {
        return DomainPrefix(fallbackDomain) + value;
    }

    return std::nullopt;
}

bool AppendTile(Document& document, const std::string& layer, int x, int y, const std::string& asset) {
    if (layer.empty() || asset.empty()) {
        return false;
    }
    document.stamps.push_back(Stamp {layer, x, y, asset});
    return true;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ParseLocalRect(const std::string& value, Rect& out) {
    const std::vector<std::string> parts = SplitEscaped(value, '|');
    if (parts.size() != 4) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) &&
           ParseFloatStrict(parts[1], out.y) &&
           ParseFloatStrict(parts[2], out.width) &&
           ParseFloatStrict(parts[3], out.height) &&
           out.width > 0.0f &&
           out.height > 0.0f;
}

bool CollisionBlocksMovement(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "block" ||
           lowered == "solid" ||
           lowered == "wall";
}

bool CollisionDisablesMovementBlock(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "none" ||
           lowered == "pass" ||
           lowered == "passable" ||
           lowered == "false" ||
           lowered == "off";
}

bool IsSupportedCollisionValue(const std::string& collision) {
    return CollisionBlocksMovement(collision) || CollisionDisablesMovementBlock(collision);
}

}

std::vector<std::string> SplitEscaped(const std::string& value, char delim) {
    std::vector<std::string> parts;
    std::string current;
    bool escaping = false;
    for (char ch : value) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == delim) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(current);
    return parts;
}

std::optional<std::pair<std::string, std::string>> SplitPropertyAssignment(const std::string& value, char delim) {
    std::string key;
    std::string remainder;
    bool escaping = false;
    bool foundDelim = false;
    for (char ch : value) {
        if (escaping) {
            if (foundDelim) {
                remainder.push_back(ch);
            } else {
                key.push_back(ch);
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (!foundDelim && ch == delim) {
            foundDelim = true;
            continue;
        }
        if (foundDelim) {
            remainder.push_back(ch);
        } else {
            key.push_back(ch);
        }
    }
    if (!foundDelim) {
        return std::nullopt;
    }
    return std::make_pair(key, remainder);
}

std::string EscapeValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == ',' || ch == ':') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool ParseFloatStrict(const std::string& value, float& out) {
    try {
        size_t consumed = 0;
        out = std::stof(value, &consumed);
        return consumed == value.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

bool ParseIntStrict(const std::string& value, int& out) {
    try {
        size_t consumed = 0;
        out = std::stoi(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool ParseRectStrict(const std::string& value, Rect& out) {
    const std::vector<std::string> parts = SplitEscaped(value, ',');
    if (parts.size() != 4) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) &&
           ParseFloatStrict(parts[1], out.y) &&
           ParseFloatStrict(parts[2], out.width) &&
           ParseFloatStrict(parts[3], out.height);
}

bool ParseVec2Strict(const std::string& value, Vec2& out) {
    const std::vector<std::string> parts = SplitEscaped(value, ',');
    if (parts.size() != 2) {
        return false;
    }
    return ParseFloatStrict(parts[0], out.x) && ParseFloatStrict(parts[1], out.y);
}

ParseResult ParseDocument(const std::string& text) {
    Document document;
    std::unordered_map<std::string, std::pair<AssetDomain, std::string>> assets;
    std::unordered_map<std::string, size_t> objectIndexById;
    bool hasFormat = false;
    std::istringstream stream(text);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "map_format") {
            int version = 0;
            if (!ParseIntStrict(value, version) || version != 2) {
                return Fail(lineNumber, "unsupported map_format");
            }
            hasFormat = true;
        } else if (key == "map_id") {
            document.mapId = value;
        } else if (key == "world_name") {
            document.worldName = value;
        } else if (key == "tile_size") {
            if (!ParseIntStrict(value, document.tileSize) || document.tileSize <= 0) {
                return Fail(lineNumber, "invalid tile size");
            }
        } else if (key == "size") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 2 ||
                !ParseIntStrict(parts[0], document.width) ||
                !ParseIntStrict(parts[1], document.height) ||
                document.width <= 0 ||
                document.height <= 0) {
                return Fail(lineNumber, "invalid map size");
            }
        } else if (key == "spawn") {
            if (!ParseVec2Strict(value, document.spawn)) {
                return Fail(lineNumber, "invalid spawn");
            }
        } else if (key == "property") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 2) {
                return Fail(lineNumber, "invalid property");
            }
            std::string propertyValue = parts[1];
            if (parts[0].rfind("player_sprite_", 0) == 0) {
                const auto resolved = ResolveAssetRef(propertyValue, AssetDomain::Character, assets);
                if (!resolved.has_value()) {
                    return Fail(lineNumber, "invalid player sprite");
                }
                propertyValue = *resolved;
            }
            document.properties[parts[0]] = propertyValue;
        } else if (key == "asset") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 3 || parts[0].empty() || parts[2].empty()) {
                return Fail(lineNumber, "invalid asset");
            }
            AssetDomain domain = AssetDomain::Map;
            if (parts[1] == "character") {
                domain = AssetDomain::Character;
            } else if (parts[1] != "map") {
                return Fail(lineNumber, "invalid asset domain");
            }
            assets[parts[0]] = std::make_pair(domain, parts[2]);
        } else if (key == "layer") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 2) {
                return Fail(lineNumber, "invalid layer");
            }
            Layer layer;
            layer.name = parts[0];
            layer.kind = parts[1];
            for (size_t i = 2; i < parts.size(); ++i) {
                const auto prop = SplitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) {
                    return Fail(lineNumber, "invalid layer property");
                }
                if (prop->first == "asset") {
                    const auto resolved = ResolveAssetRef(prop->second, AssetDomain::Map, assets);
                    if (!resolved.has_value()) {
                        return Fail(lineNumber, "invalid layer asset");
                    }
                    layer.asset = *resolved;
                    layer.kind = "image";
                } else if (prop->first == "tint") {
                    layer.tint = prop->second;
                } else if (prop->first == "parallax") {
                    if (!ParseFloatStrict(prop->second, layer.parallax)) {
                        return Fail(lineNumber, "invalid layer parallax");
                    }
                }
            }
            document.layers.push_back(std::move(layer));
        } else if (key == "tile") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 4) {
                return Fail(lineNumber, "invalid tile");
            }
            Stamp stamp;
            stamp.layer = parts[0];
            if (!ParseIntStrict(parts[1], stamp.x) || !ParseIntStrict(parts[2], stamp.y)) {
                return Fail(lineNumber, "invalid tile coordinates");
            }
            const auto resolved = ResolveAssetRef(parts[3], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid tile asset");
            }
            stamp.asset = *resolved;
            if (!AppendTile(document, stamp.layer, stamp.x, stamp.y, stamp.asset)) {
                return Fail(lineNumber, "invalid tile");
            }
        } else if (key == "line") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid line");
            }
            int x1 = 0;
            int y1 = 0;
            int x2 = 0;
            int y2 = 0;
            if (!ParseIntStrict(parts[1], x1) ||
                !ParseIntStrict(parts[2], y1) ||
                !ParseIntStrict(parts[3], x2) ||
                !ParseIntStrict(parts[4], y2)) {
                return Fail(lineNumber, "invalid line coordinates");
            }
            if (x1 != x2 && y1 != y2) {
                return Fail(lineNumber, "line must be horizontal or vertical");
            }
            const auto resolved = ResolveAssetRef(parts[5], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid line asset");
            }
            const int minX = std::min(x1, x2);
            const int maxX = std::max(x1, x2);
            const int minY = std::min(y1, y2);
            const int maxY = std::max(y1, y2);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    AppendTile(document, parts[0], x, y, *resolved);
                }
            }
        } else if (key == "fill") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid fill");
            }
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!ParseIntStrict(parts[1], x) ||
                !ParseIntStrict(parts[2], y) ||
                !ParseIntStrict(parts[3], width) ||
                !ParseIntStrict(parts[4], height) ||
                width <= 0 ||
                height <= 0) {
                return Fail(lineNumber, "invalid fill bounds");
            }
            const auto resolved = ResolveAssetRef(parts[5], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid fill asset");
            }
            for (int ty = y; ty < y + height; ++ty) {
                for (int tx = x; tx < x + width; ++tx) {
                    AppendTile(document, parts[0], tx, ty, *resolved);
                }
            }
        } else if (key == "tiles") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 3) {
                return Fail(lineNumber, "invalid tiles");
            }
            const auto resolved = ResolveAssetRef(parts[1], AssetDomain::Map, assets);
            if (!resolved.has_value()) {
                return Fail(lineNumber, "invalid tiles asset");
            }
            for (size_t i = 2; i < parts.size(); ++i) {
                const std::vector<std::string> point = SplitEscaped(parts[i], ':');
                int x = 0;
                int y = 0;
                if (point.size() != 2 ||
                    !ParseIntStrict(point[0], x) ||
                    !ParseIntStrict(point[1], y)) {
                    return Fail(lineNumber, "invalid tiles coordinate");
                }
                AppendTile(document, parts[0], x, y, *resolved);
            }
        } else if (key == "entity") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 6) {
                return Fail(lineNumber, "invalid entity");
            }
            Object object;
            object.kind = parts[0];
            object.id = parts[1];
            if (!ParseFloatStrict(parts[2], object.bounds.x) ||
                !ParseFloatStrict(parts[3], object.bounds.y) ||
                !ParseFloatStrict(parts[4], object.bounds.width) ||
                !ParseFloatStrict(parts[5], object.bounds.height) ||
                object.bounds.width < 0.0f ||
                object.bounds.height < 0.0f) {
                return Fail(lineNumber, "invalid entity bounds");
            }
            if (objectIndexById.find(object.id) != objectIndexById.end()) {
                return Fail(lineNumber, "duplicate entity id");
            }
            objectIndexById[object.id] = document.objects.size();
            document.objects.push_back(std::move(object));
        } else if (key == "prop") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() != 3) {
                return Fail(lineNumber, "invalid entity property");
            }
            auto objectIt = objectIndexById.find(parts[0]);
            if (objectIt == objectIndexById.end()) {
                return Fail(lineNumber, "property references unknown entity");
            }
            std::string propValue = parts[2];
            if (parts[1] == "sprite") {
                const auto resolved = ResolveAssetRef(propValue, AssetDomain::Map, assets);
                if (!resolved.has_value()) {
                    return Fail(lineNumber, "invalid entity sprite");
                }
                propValue = *resolved;
            } else if (parts[1] == "collision" && !IsSupportedCollisionValue(propValue)) {
                return Fail(lineNumber, "invalid entity collision");
            }
            document.objects[objectIt->second].properties[parts[1]] = propValue;
        } else {
            return Fail(lineNumber, "unsupported map v2 key: " + key);
        }
    }

    if (!hasFormat) {
        return Fail(0, "missing map_format=2");
    }
    if (document.mapId.empty()) {
        return Fail(0, "missing map_id");
    }
    if (document.tileSize < 16 || document.tileSize > 128) {
        return Fail(0, "tile size out of supported range");
    }
    if (document.width < 8 || document.height < 8) {
        return Fail(0, "map dimensions out of supported range");
    }

    ParseResult result;
    result.document = std::move(document);
    result.ok = true;
    return result;
}

std::optional<AtlasRef> ParseAtlasRef(const std::string& asset) {
    const std::vector<std::string> parts = SplitEscaped(asset, '@');
    if (parts.size() != 5) {
        return std::nullopt;
    }

    AtlasRef ref;
    ref.file = parts[0];
    const size_t domainSep = ref.file.find(':');
    if (domainSep != std::string::npos) {
        const std::string domain = ref.file.substr(0, domainSep);
        ref.file = ref.file.substr(domainSep + 1);
        ref.domain = (domain == "character") ? AssetDomain::Character : AssetDomain::Map;
    }

    if (!ParseFloatStrict(parts[1], ref.source.x) ||
        !ParseFloatStrict(parts[2], ref.source.y) ||
        !ParseFloatStrict(parts[3], ref.source.width) ||
        !ParseFloatStrict(parts[4], ref.source.height)) {
        return std::nullopt;
    }

    ref.valid = !ref.file.empty() && ref.source.width > 0.0f && ref.source.height > 0.0f;
    if (!ref.valid) {
        return std::nullopt;
    }
    return ref;
}

std::string AssetFileName(const std::string& ref) {
    if (const std::optional<AtlasRef> atlasRef = ParseAtlasRef(ref)) {
        return atlasRef->file;
    }
    std::string head = ref;
    const size_t domainSep = head.find(':');
    if (domainSep != std::string::npos) {
        head = head.substr(domainSep + 1);
    }
    return head;
}

AssetDomain AssetDomainForRef(const std::string& ref, AssetDomain fallbackDomain) {
    if (const std::optional<AtlasRef> atlasRef = ParseAtlasRef(ref)) {
        return atlasRef->domain;
    }
    const size_t domainSep = ref.find(':');
    if (domainSep == std::string::npos) {
        return fallbackDomain;
    }
    return ref.substr(0, domainSep) == "character" ? AssetDomain::Character : AssetDomain::Map;
}

std::string AtlasMetaKey(const std::string& file, int x, int y, int w, int h) {
    return file + "@" + std::to_string(x) + "@" + std::to_string(y) + "@" + std::to_string(w) + "@" + std::to_string(h);
}

std::unordered_map<std::string, AtlasTileMeta> ParseAtlasMetadata(const std::string& text, const std::string& fileName) {
    std::unordered_map<std::string, AtlasTileMeta> metadata;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t sep = line.find('=');
        if (sep == std::string::npos || line.substr(0, sep) != "tile") {
            continue;
        }

        const std::vector<std::string> parts = SplitEscaped(line.substr(sep + 1), ',');
        if (parts.size() < 4) {
            continue;
        }

        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if (!ParseIntStrict(parts[0], x) ||
            !ParseIntStrict(parts[1], y) ||
            !ParseIntStrict(parts[2], w) ||
            !ParseIntStrict(parts[3], h) ||
            w <= 0 ||
            h <= 0) {
            continue;
        }

        AtlasTileMeta meta;
        for (size_t i = 4; i < parts.size(); ++i) {
            const auto prop = SplitPropertyAssignment(parts[i], ':');
            if (!prop.has_value()) {
                continue;
            }
            if (prop->first == "collision") {
                meta.collision = ToLower(prop->second);
                meta.blocksMovement = CollisionBlocksMovement(meta.collision);
            } else if (prop->first == "tag") {
                meta.tag = prop->second;
            } else if (prop->first == "collider" || prop->first == "hitbox") {
                Rect collider;
                if (ParseLocalRect(prop->second, collider)) {
                    meta.collider = collider;
                    meta.hasCollider = true;
                }
            }
        }

        metadata[AtlasMetaKey(fileName, x, y, w, h)] = std::move(meta);
    }
    return metadata;
}

void AddUniqueAssetRef(std::vector<std::string>& refs, const std::string& file) {
    if (file.empty()) {
        return;
    }
    if (std::find(refs.begin(), refs.end(), file) == refs.end()) {
        refs.push_back(file);
    }
}

void CollectReferencedAssets(const Document& document,
                             std::vector<std::string>& mapImageRefs,
                             std::vector<std::string>& characterImageRefs) {
    mapImageRefs.clear();
    characterImageRefs.clear();
    for (const auto& [key, value] : document.properties) {
        if (key.rfind("player_sprite_", 0) == 0) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, value, AssetDomain::Character);
        }
    }
    for (const Layer& layer : document.layers) {
        if (layer.kind == "image" && !layer.asset.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, layer.asset, AssetDomain::Map);
        }
    }
    for (const Object& object : document.objects) {
        auto spriteIt = object.properties.find("sprite");
        if (spriteIt != object.properties.end() && !spriteIt->second.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, spriteIt->second, AssetDomain::Map);
        }
    }
    for (const Stamp& stamp : document.stamps) {
        if (!stamp.asset.empty()) {
            AddAssetRefForDomain(mapImageRefs, characterImageRefs, stamp.asset, AssetDomain::Map);
        }
    }
}

}
}
