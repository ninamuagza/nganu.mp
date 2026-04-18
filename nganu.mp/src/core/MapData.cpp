#include "core/MapData.h"

#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace {
std::vector<std::string> splitEscaped(const std::string& value, char delim) {
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

std::optional<std::pair<std::string, std::string>> splitPropertyAssignment(const std::string& value, char delim) {
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

bool parseFloat(const std::string& value, float& out) {
    try {
        size_t consumed = 0;
        out = std::stof(value, &consumed);
        return consumed == value.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& value, int& out) {
    try {
        size_t consumed = 0;
        out = std::stoi(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool parseRect(const std::string& value, MapData::Rect& out) {
    std::istringstream stream(value);
    std::string token;
    float parts[4] {};
    int index = 0;
    while (std::getline(stream, token, ',') && index < 4) {
        float part = 0.0f;
        if (!parseFloat(token, part)) {
            return false;
        }
        parts[index++] = part;
    }
    if (index != 4) {
        return false;
    }

    out.x = parts[0];
    out.y = parts[1];
    out.width = parts[2];
    out.height = parts[3];
    return true;
}

std::string assetFileName(const std::string& ref) {
    const size_t sep = ref.find('@');
    std::string head = (sep == std::string::npos) ? ref : ref.substr(0, sep);
    head.erase(std::remove(head.begin(), head.end(), '\\'), head.end());
    const size_t domainSep = head.find(':');
    if (domainSep == std::string::npos) {
        return head;
    }
    return head.substr(domainSep + 1);
}

std::string assetDomain(const std::string& ref) {
    const size_t sep = ref.find('@');
    std::string head = (sep == std::string::npos) ? ref : ref.substr(0, sep);
    head.erase(std::remove(head.begin(), head.end(), '\\'), head.end());
    const size_t domainSep = head.find(':');
    if (domainSep == std::string::npos) {
        return {};
    }
    return head.substr(0, domainSep);
}

void addUniqueAssetRef(std::vector<std::string>& refs, const std::string& ref) {
    const std::string file = assetFileName(ref);
    if (file.empty()) {
        return;
    }
    if (std::find(refs.begin(), refs.end(), file) == refs.end()) {
        refs.push_back(file);
    }
}

void addAssetRefForDomain(std::vector<std::string>& mapRefs, std::vector<std::string>& characterRefs, const std::string& ref, const std::string& fallbackDomain) {
    const std::string domain = assetDomain(ref).empty() ? fallbackDomain : assetDomain(ref);
    if (domain == "character") {
        addUniqueAssetRef(characterRefs, ref);
        return;
    }
    addUniqueAssetRef(mapRefs, ref);
}

bool rectsOverlap(const MapData::Rect& a, const MapData::Rect& b) {
    return a.x < (b.x + b.width) &&
           (a.x + a.width) > b.x &&
           a.y < (b.y + b.height) &&
           (a.y + a.height) > b.y;
}

std::string atlasMetaKey(const std::string& file, int x, int y, int w, int h) {
    return file + "@" + std::to_string(x) + "@" + std::to_string(y) + "@" + std::to_string(w) + "@" + std::to_string(h);
}
}

bool MapData::loadFromFile(const std::string& path, Logger& logger) {
    std::ifstream in(path);
    if (!in.is_open()) {
        logger.error("MapData", "Failed to open map file: %s", path.c_str());
        return false;
    }

    mapId_.clear();
    worldName_.clear();
    tileSize_ = 48;
    width_ = 0;
    height_ = 0;
    spawnX_ = 160.0f;
    spawnY_ = 160.0f;
    properties_.clear();
    mapImageRefs_.clear();
    characterImageRefs_.clear();
    objects_.clear();
    stamps_.clear();
    blockedAreas_.clear();
    waterAreas_.clear();
    atlasTileMeta_.clear();

    auto failLine = [&](int lineNumber, const char* reason) {
        logger.error("MapData", "Invalid map file %s at line %d: %s", path.c_str(), lineNumber, reason);
        return false;
    };

    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        const size_t sep = line.find('=');
        if (sep == std::string::npos) continue;

        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "map_id") {
            mapId_ = value;
        } else if (key == "world_name") {
            worldName_ = value;
        } else if (key == "tile") {
            if (!parseInt(value, tileSize_) || tileSize_ <= 0) return failLine(lineNumber, "invalid tile size");
        } else if (key == "width") {
            if (!parseInt(value, width_) || width_ <= 0) return failLine(lineNumber, "invalid map width");
        } else if (key == "height") {
            if (!parseInt(value, height_) || height_ <= 0) return failLine(lineNumber, "invalid map height");
        } else if (key == "spawn") {
            std::istringstream stream(value);
            std::string token;
            if (!std::getline(stream, token, ',') || !parseFloat(token, spawnX_)) {
                return failLine(lineNumber, "invalid spawn x");
            }
            if (!std::getline(stream, token, ',') || !parseFloat(token, spawnY_)) {
                return failLine(lineNumber, "invalid spawn y");
            }
        } else if (key == "property") {
            const auto parts = splitEscaped(value, ',');
            if (parts.size() < 2) return failLine(lineNumber, "invalid property");
            properties_[parts[0]] = parts[1];
            if (parts[0].rfind("player_sprite_", 0) == 0) {
                addAssetRefForDomain(mapImageRefs_, characterImageRefs_, parts[1], "character");
            }
        } else if (key == "layer") {
            const auto parts = splitEscaped(value, ',');
            if (parts.size() >= 3 && !parts[2].empty()) {
                addAssetRefForDomain(mapImageRefs_, characterImageRefs_, parts[2], "map");
            }
        } else if (key == "object") {
            const std::vector<std::string> parts = splitEscaped(value, ',');
            if (parts.size() < 6) return failLine(lineNumber, "invalid object");

            Object object;
            object.kind = parts[0];
            object.id = parts[1];
            if (!parseFloat(parts[2], object.x) ||
                !parseFloat(parts[3], object.y) ||
                !parseFloat(parts[4], object.width) ||
                !parseFloat(parts[5], object.height) ||
                object.width < 0.0f ||
                object.height < 0.0f) {
                return failLine(lineNumber, "invalid object bounds");
            }
            for (size_t i = 6; i < parts.size(); ++i) {
                const auto prop = splitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) continue;
                object.properties[prop->first] = prop->second;
            }
            auto spriteIt = object.properties.find("sprite");
            if (spriteIt != object.properties.end() && !spriteIt->second.empty()) {
                addAssetRefForDomain(mapImageRefs_, characterImageRefs_, spriteIt->second, "map");
            }
            objects_.push_back(std::move(object));
        } else if (key == "stamp") {
            const std::vector<std::string> parts = splitEscaped(value, ',');
            if (parts.size() < 4) return failLine(lineNumber, "invalid stamp");
            Stamp stamp;
            stamp.layer = parts[0];
            if (!parseInt(parts[1], stamp.x) || !parseInt(parts[2], stamp.y)) {
                return failLine(lineNumber, "invalid stamp coordinates");
            }
            stamp.asset = parts[3];
            addAssetRefForDomain(mapImageRefs_, characterImageRefs_, stamp.asset, "map");
            stamps_.push_back(std::move(stamp));
        } else if (key == "blocked") {
            Rect area;
            if (!parseRect(value, area)) return failLine(lineNumber, "invalid blocked rect");
            blockedAreas_.push_back(area);
        } else if (key == "water") {
            Rect area;
            if (!parseRect(value, area)) return failLine(lineNumber, "invalid water rect");
            waterAreas_.push_back(area);
        }
    }

    if (mapId_.empty()) {
        logger.error("MapData", "Map file %s has no map_id", path.c_str());
        return false;
    }

    loadAtlasMetadata(std::filesystem::path(path).parent_path().parent_path() / "map_images", logger);

    return true;
}

void MapData::loadAtlasMetadata(const std::filesystem::path& atlasRoot, Logger& logger) {
    std::unordered_map<std::string, bool> loadedFiles;
    for (const std::string& file : mapImageRefs_) {
        if (file.empty() || loadedFiles[file]) {
            continue;
        }
        loadedFiles[file] = true;

        const std::filesystem::path metaPath = atlasRoot / (std::filesystem::path(file).stem().string() + ".atlas");
        if (!std::filesystem::exists(metaPath)) {
            continue;
        }

        std::ifstream in(metaPath);
        if (!in.is_open()) {
            logger.warn("MapData", "Failed to open atlas metadata: %s", metaPath.string().c_str());
            continue;
        }

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const size_t sep = line.find('=');
            if (sep == std::string::npos) continue;
            if (line.substr(0, sep) != "tile") continue;

            const std::vector<std::string> parts = splitEscaped(line.substr(sep + 1), ',');
            if (parts.size() < 4) continue;

            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            try {
                x = std::stoi(parts[0]);
                y = std::stoi(parts[1]);
                w = std::stoi(parts[2]);
                h = std::stoi(parts[3]);
            } catch (...) {
                continue;
            }

            AtlasTileMeta meta;
            for (size_t i = 4; i < parts.size(); ++i) {
                const auto prop = splitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) continue;
                if (prop->first == "collision" && prop->second == "block") {
                    meta.blocksMovement = true;
                } else if (prop->first == "tag") {
                    meta.tag = prop->second;
                }
            }
            atlasTileMeta_[atlasMetaKey(file, x, y, w, h)] = std::move(meta);
        }
    }
}

std::optional<MapData::AtlasTileMeta> MapData::metaForAsset(const std::string& assetRef) const {
    const size_t sep = assetRef.find('@');
    if (sep == std::string::npos) {
        return std::nullopt;
    }
    const std::string file = assetFileName(assetRef);
    const std::vector<std::string> parts = splitEscaped(assetRef.substr(sep + 1), '@');
    if (parts.size() != 4) {
        return std::nullopt;
    }

    try {
        const std::string key = atlasMetaKey(file, std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3]));
        auto it = atlasTileMeta_.find(key);
        if (it == atlasTileMeta_.end()) {
            return std::nullopt;
        }
        return it->second;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MapData::property(const std::string& key) const {
    auto it = properties_.find(key);
    if (it == properties_.end()) return std::nullopt;
    return it->second;
}

const MapData::Object* MapData::objectByIndex(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= objects_.size()) return nullptr;
    return &objects_[static_cast<size_t>(index)];
}

const MapData::Object* MapData::objectById(const std::string& id) const {
    for (const Object& object : objects_) {
        if (object.id == id) return &object;
    }
    return nullptr;
}

int MapData::objectIndexById(const std::string& id) const {
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (objects_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

bool MapData::objectContainsPoint(const Object& object, float px, float py) const {
    return px >= object.x && py >= object.y &&
           px <= (object.x + object.width) &&
           py <= (object.y + object.height);
}

bool MapData::isWalkable(float worldX, float worldY, float radius) const {
    const Rect body {
        worldX - radius,
        worldY - radius,
        radius * 2.0f,
        radius * 2.0f
    };

    if (body.x < 0.0f || body.y < 0.0f) {
        return false;
    }

    if (width_ > 0 && height_ > 0 && tileSize_ > 0) {
        const float maxWidth = static_cast<float>(width_ * tileSize_);
        const float maxHeight = static_cast<float>(height_ * tileSize_);
        if (body.x + body.width > maxWidth || body.y + body.height > maxHeight) {
            return false;
        }
    }

    if (std::isnan(worldX) || std::isnan(worldY) || std::isinf(worldX) || std::isinf(worldY)) {
        return false;
    }

    for (const Rect& area : blockedAreas_) {
        if (rectsOverlap(body, area)) {
            return false;
        }
    }

    for (const Rect& area : waterAreas_) {
        if (rectsOverlap(body, area)) {
            return false;
        }
    }

    for (const Stamp& stamp : stamps_) {
        const auto meta = metaForAsset(stamp.asset);
        if (!meta.has_value() || !meta->blocksMovement) {
            continue;
        }
        const Rect stampRect {
            static_cast<float>(stamp.x * tileSize_),
            static_cast<float>(stamp.y * tileSize_),
            static_cast<float>(tileSize_),
            static_cast<float>(tileSize_)
        };
        if (rectsOverlap(body, stampRect)) {
            return false;
        }
    }

    return true;
}
