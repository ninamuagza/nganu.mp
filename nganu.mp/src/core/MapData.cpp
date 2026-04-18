#include "core/MapData.h"

#include "shared/MapFormat.h"

#include <fstream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iterator>

namespace {
bool rectsOverlap(const MapData::Rect& a, const MapData::Rect& b) {
    return a.x < (b.x + b.width) &&
           (a.x + a.width) > b.x &&
           a.y < (b.y + b.height) &&
           (a.y + a.height) > b.y;
}
}

bool MapData::loadFromFile(const std::string& path, Logger& logger) {
    std::ifstream in(path);
    if (!in.is_open()) {
        logger.error("MapData", "Failed to open map file: %s", path.c_str());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Nganu::MapFormat::ParseResult parsed = Nganu::MapFormat::ParseDocument(text);
    if (!parsed.ok) {
        if (parsed.line > 0) {
            logger.error("MapData", "Invalid map file %s at line %d: %s", path.c_str(), parsed.line, parsed.error.c_str());
        } else {
            logger.error("MapData", "Invalid map file %s: %s", path.c_str(), parsed.error.c_str());
        }
        return false;
    }
    const Nganu::MapFormat::Document& document = parsed.document;

    mapId_ = document.mapId;
    worldName_ = document.worldName;
    tileSize_ = document.tileSize;
    width_ = document.width;
    height_ = document.height;
    spawnX_ = document.spawn.x;
    spawnY_ = document.spawn.y;
    properties_.clear();
    mapImageRefs_.clear();
    characterImageRefs_.clear();
    objects_.clear();
    stamps_.clear();
    blockedAreas_.clear();
    waterAreas_.clear();
    atlasTileMeta_.clear();

    properties_ = document.properties;
    Nganu::MapFormat::CollectReferencedAssets(document, mapImageRefs_, characterImageRefs_);
    objects_.reserve(document.objects.size());
    for (const Nganu::MapFormat::Object& source : document.objects) {
        Object object;
        object.kind = source.kind;
        object.id = source.id;
        object.x = source.bounds.x;
        object.y = source.bounds.y;
        object.width = source.bounds.width;
        object.height = source.bounds.height;
        object.properties = source.properties;
        objects_.push_back(std::move(object));
    }
    stamps_.reserve(document.stamps.size());
    for (const Nganu::MapFormat::Stamp& source : document.stamps) {
        stamps_.push_back(Stamp {source.layer, source.x, source.y, source.asset});
    }
    blockedAreas_.reserve(document.blockedAreas.size());
    for (const Nganu::MapFormat::Rect& source : document.blockedAreas) {
        blockedAreas_.push_back(Rect {source.x, source.y, source.width, source.height});
    }
    waterAreas_.reserve(document.waterAreas.size());
    for (const Nganu::MapFormat::Rect& source : document.waterAreas) {
        waterAreas_.push_back(Rect {source.x, source.y, source.width, source.height});
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

            const std::vector<std::string> parts = Nganu::MapFormat::SplitEscaped(line.substr(sep + 1), ',');
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
                const auto prop = Nganu::MapFormat::SplitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) continue;
                if (prop->first == "collision" && prop->second == "block") {
                    meta.blocksMovement = true;
                } else if (prop->first == "tag") {
                    meta.tag = prop->second;
                }
            }
            atlasTileMeta_[Nganu::MapFormat::AtlasMetaKey(file, x, y, w, h)] = std::move(meta);
        }
    }
}

std::optional<MapData::AtlasTileMeta> MapData::metaForAsset(const std::string& assetRef) const {
    const auto ref = Nganu::MapFormat::ParseAtlasRef(assetRef);
    if (!ref.has_value()) {
        return std::nullopt;
    }
    try {
        const std::string key = Nganu::MapFormat::AtlasMetaKey(ref->file,
                                                               static_cast<int>(ref->source.x),
                                                               static_cast<int>(ref->source.y),
                                                               static_cast<int>(ref->source.width),
                                                               static_cast<int>(ref->source.height));
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
