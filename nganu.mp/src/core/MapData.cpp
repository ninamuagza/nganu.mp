#include "core/MapData.h"

#include "shared/MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {
constexpr int kSpatialChunkTiles = 16;

bool rectsOverlap(const MapData::Rect& a, const MapData::Rect& b) {
    return a.x < (b.x + b.width) &&
           (a.x + a.width) > b.x &&
           a.y < (b.y + b.height) &&
           (a.y + a.height) > b.y;
}

bool hasArea(const MapData::Rect& rect) {
    return rect.width > 0.0f && rect.height > 0.0f;
}

std::uint64_t spatialBucketKey(int chunkX, int chunkY) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint32_t>(chunkY);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool collisionBlocksMovement(const std::string& collision) {
    const std::string lowered = toLower(collision);
    return lowered == "block" ||
           lowered == "solid" ||
           lowered == "wall";
}

bool collisionDisablesMovementBlock(const std::string& collision) {
    const std::string lowered = toLower(collision);
    return lowered == "none" ||
           lowered == "pass" ||
           lowered == "passable" ||
           lowered == "false" ||
           lowered == "off";
}

std::optional<std::string> propertyValue(const std::unordered_map<std::string, std::string>& properties, const std::string& key) {
    auto it = properties.find(key);
    if (it == properties.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool parseLocalRect(const std::string& value, MapData::Rect& out) {
    const std::vector<std::string> parts = Nganu::MapFormat::SplitEscaped(value, '|');
    if (parts.size() != 4) {
        return false;
    }
    return Nganu::MapFormat::ParseFloatStrict(parts[0], out.x) &&
           Nganu::MapFormat::ParseFloatStrict(parts[1], out.y) &&
           Nganu::MapFormat::ParseFloatStrict(parts[2], out.width) &&
           Nganu::MapFormat::ParseFloatStrict(parts[3], out.height) &&
           out.width > 0.0f &&
           out.height > 0.0f;
}

std::optional<MapData::Rect> propertyCollider(const MapData::Object& object) {
    const std::optional<std::string> collider = propertyValue(object.properties, "collider");
    const std::optional<std::string> hitbox = propertyValue(object.properties, "hitbox");
    const std::string* value = collider.has_value() ? &*collider : (hitbox.has_value() ? &*hitbox : nullptr);
    if (value == nullptr) {
        return std::nullopt;
    }

    MapData::Rect rect;
    if (!parseLocalRect(*value, rect)) {
        return std::nullopt;
    }
    return rect;
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
    atlasTileMeta_.clear();
    stampBuckets_.clear();
    objectBuckets_.clear();
    stampQueryMarks_.clear();
    stampQueryToken_ = 1;

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

    loadAtlasMetadata(std::filesystem::path(path).parent_path().parent_path() / "map_images", logger);
    rebuildSpatialIndex();

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

        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto parsedMetadata = Nganu::MapFormat::ParseAtlasMetadata(text, file);
        for (const auto& [key, source] : parsedMetadata) {
            AtlasTileMeta meta;
            meta.collision = source.collision;
            meta.blocksMovement = source.blocksMovement;
            meta.hasCollider = source.hasCollider;
            meta.collider = Rect {source.collider.x, source.collider.y, source.collider.width, source.collider.height};
            meta.tag = source.tag;
            atlasTileMeta_[key] = std::move(meta);
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

void MapData::rebuildSpatialIndex() {
    stampBuckets_.clear();
    objectBuckets_.clear();

    if (tileSize_ <= 0) {
        return;
    }

    stampQueryMarks_.assign(stamps_.size(), 0);
    stampQueryToken_ = 1;
    const float chunkWorldSize = static_cast<float>(tileSize_ * kSpatialChunkTiles);
    for (size_t i = 0; i < stamps_.size(); ++i) {
        const Stamp& stamp = stamps_[i];
        const Rect bounds = stampBounds(stamp);
        if (!hasArea(bounds)) {
            continue;
        }

        const int minChunkX = static_cast<int>(std::floor(bounds.x / chunkWorldSize));
        const int minChunkY = static_cast<int>(std::floor(bounds.y / chunkWorldSize));
        const int maxChunkX = static_cast<int>(std::floor((bounds.x + bounds.width) / chunkWorldSize));
        const int maxChunkY = static_cast<int>(std::floor((bounds.y + bounds.height) / chunkWorldSize));
        for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
            for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                stampBuckets_[spatialBucketKey(chunkX, chunkY)].push_back(i);
            }
        }
    }

    for (size_t i = 0; i < objects_.size(); ++i) {
        const Object& object = objects_[i];
        const Rect bounds {object.x, object.y, object.width, object.height};
        if (!hasArea(bounds)) {
            continue;
        }

        const int minChunkX = static_cast<int>(std::floor(bounds.x / chunkWorldSize));
        const int minChunkY = static_cast<int>(std::floor(bounds.y / chunkWorldSize));
        const int maxChunkX = static_cast<int>(std::floor((bounds.x + bounds.width) / chunkWorldSize));
        const int maxChunkY = static_cast<int>(std::floor((bounds.y + bounds.height) / chunkWorldSize));
        for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
            for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                objectBuckets_[spatialBucketKey(chunkX, chunkY)].push_back(i);
            }
        }
    }
}

void MapData::collectStampCandidates(const Rect& area, std::vector<size_t>& out) const {
    out.clear();
    if (stampBuckets_.empty() || tileSize_ <= 0 || !hasArea(area)) {
        return;
    }
    if (stampQueryMarks_.size() != stamps_.size()) {
        stampQueryMarks_.assign(stamps_.size(), 0);
        stampQueryToken_ = 1;
    }
    ++stampQueryToken_;
    if (stampQueryToken_ == 0) {
        std::fill(stampQueryMarks_.begin(), stampQueryMarks_.end(), 0);
        stampQueryToken_ = 1;
    }

    const float chunkWorldSize = static_cast<float>(tileSize_ * kSpatialChunkTiles);
    const int minChunkX = static_cast<int>(std::floor(area.x / chunkWorldSize));
    const int minChunkY = static_cast<int>(std::floor(area.y / chunkWorldSize));
    const int maxChunkX = static_cast<int>(std::floor((area.x + area.width) / chunkWorldSize));
    const int maxChunkY = static_cast<int>(std::floor((area.y + area.height) / chunkWorldSize));

    for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            auto it = stampBuckets_.find(spatialBucketKey(chunkX, chunkY));
            if (it == stampBuckets_.end()) {
                continue;
            }
            for (size_t stampIndex : it->second) {
                if (stampIndex >= stamps_.size() || stampQueryMarks_[stampIndex] == stampQueryToken_) {
                    continue;
                }
                stampQueryMarks_[stampIndex] = stampQueryToken_;
                const Rect bounds = stampBounds(stamps_[stampIndex]);
                if (rectsOverlap(bounds, area)) {
                    out.push_back(stampIndex);
                }
            }
        }
    }
}

void MapData::collectObjectCandidates(const Rect& area, std::vector<size_t>& out) const {
    out.clear();
    if (objectBuckets_.empty() || tileSize_ <= 0 || !hasArea(area)) {
        return;
    }

    const float chunkWorldSize = static_cast<float>(tileSize_ * kSpatialChunkTiles);
    const int minChunkX = static_cast<int>(std::floor(area.x / chunkWorldSize));
    const int minChunkY = static_cast<int>(std::floor(area.y / chunkWorldSize));
    const int maxChunkX = static_cast<int>(std::floor((area.x + area.width) / chunkWorldSize));
    const int maxChunkY = static_cast<int>(std::floor((area.y + area.height) / chunkWorldSize));

    for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            auto it = objectBuckets_.find(spatialBucketKey(chunkX, chunkY));
            if (it == objectBuckets_.end()) {
                continue;
            }
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
}

MapData::Rect MapData::stampBounds(const Stamp& stamp) const {
    const auto ref = Nganu::MapFormat::ParseAtlasRef(stamp.asset);
    const float width = ref.has_value() ? std::max(1.0f, ref->source.width) : static_cast<float>(tileSize_);
    const float height = ref.has_value() ? std::max(1.0f, ref->source.height) : static_cast<float>(tileSize_);
    return Rect {
        static_cast<float>(stamp.x * tileSize_) + ((static_cast<float>(tileSize_) - width) * 0.5f),
        static_cast<float>((stamp.y + 1) * tileSize_) - height,
        width,
        height
    };
}

MapData::Rect MapData::stampCollisionRect(const Stamp& stamp, const AtlasTileMeta& meta) const {
    const Rect bounds = stampBounds(stamp);
    if (!meta.hasCollider) {
        return bounds;
    }

    const auto ref = Nganu::MapFormat::ParseAtlasRef(stamp.asset);
    const float sourceWidth = ref.has_value() ? std::max(1.0f, ref->source.width) : static_cast<float>(tileSize_);
    const float sourceHeight = ref.has_value() ? std::max(1.0f, ref->source.height) : static_cast<float>(tileSize_);
    return Rect {
        bounds.x + (meta.collider.x / sourceWidth) * bounds.width,
        bounds.y + (meta.collider.y / sourceHeight) * bounds.height,
        (meta.collider.width / sourceWidth) * bounds.width,
        (meta.collider.height / sourceHeight) * bounds.height
    };
}

std::optional<MapData::Rect> MapData::objectCollisionRect(const Object& object) const {
    const std::optional<std::string> collision = propertyValue(object.properties, "collision");
    if (collision.has_value() && collisionDisablesMovementBlock(*collision)) {
        return std::nullopt;
    }

    std::optional<AtlasTileMeta> spriteMeta;
    const std::optional<std::string> sprite = propertyValue(object.properties, "sprite");
    if (sprite.has_value()) {
        spriteMeta = metaForAsset(*sprite);
    }

    const bool blocks = collision.has_value()
        ? collisionBlocksMovement(*collision)
        : (spriteMeta.has_value() && spriteMeta->blocksMovement);
    if (!blocks) {
        return std::nullopt;
    }

    const Rect bounds {object.x, object.y, object.width, object.height};
    if (!hasArea(bounds)) {
        return std::nullopt;
    }

    if (const std::optional<Rect> localCollider = propertyCollider(object)) {
        return Rect {
            bounds.x + localCollider->x,
            bounds.y + localCollider->y,
            localCollider->width,
            localCollider->height
        };
    }

    if (spriteMeta.has_value() && spriteMeta->hasCollider && sprite.has_value()) {
        const auto ref = Nganu::MapFormat::ParseAtlasRef(*sprite);
        const float sourceWidth = ref.has_value() ? std::max(1.0f, ref->source.width) : std::max(1.0f, bounds.width);
        const float sourceHeight = ref.has_value() ? std::max(1.0f, ref->source.height) : std::max(1.0f, bounds.height);
        return Rect {
            bounds.x + (spriteMeta->collider.x / sourceWidth) * bounds.width,
            bounds.y + (spriteMeta->collider.y / sourceHeight) * bounds.height,
            (spriteMeta->collider.width / sourceWidth) * bounds.width,
            (spriteMeta->collider.height / sourceHeight) * bounds.height
        };
    }

    return bounds;
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

    std::vector<size_t> candidateStamps;
    collectStampCandidates(body, candidateStamps);
    for (size_t stampIndex : candidateStamps) {
        if (stampIndex >= stamps_.size()) {
            continue;
        }
        const Stamp& stamp = stamps_[stampIndex];
        const auto meta = metaForAsset(stamp.asset);
        if (!meta.has_value() || !meta->blocksMovement) {
            continue;
        }
        const Rect stampRect = stampCollisionRect(stamp, *meta);
        if (rectsOverlap(body, stampRect)) {
            return false;
        }
    }

    std::vector<size_t> candidateObjects;
    collectObjectCandidates(body, candidateObjects);
    for (size_t objectIndex : candidateObjects) {
        if (objectIndex >= objects_.size()) {
            continue;
        }
        const std::optional<Rect> collider = objectCollisionRect(objects_[objectIndex]);
        if (collider.has_value() && rectsOverlap(body, *collider)) {
            return false;
        }
    }

    return true;
}
