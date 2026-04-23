#pragma once

#include "core/Logger.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class MapData {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct Object {
        std::string kind;
        std::string id;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        std::unordered_map<std::string, std::string> properties;
    };

    struct Stamp {
        std::string layer;
        int x = 0;
        int y = 0;
        std::string asset;
    };

    struct AtlasTileMeta {
        std::string collision;
        bool blocksMovement = false;
        bool hasCollider = false;
        Rect collider;
        std::string tag;
    };

    bool loadFromFile(const std::string& path, Logger& logger);

    const std::string& mapId() const { return mapId_; }
    const std::string& worldName() const { return worldName_; }
    int tileSize() const { return tileSize_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float spawnX() const { return spawnX_; }
    float spawnY() const { return spawnY_; }

    std::optional<std::string> property(const std::string& key) const;
    const std::vector<std::string>& mapImageRefs() const { return mapImageRefs_; }
    const std::vector<std::string>& characterImageRefs() const { return characterImageRefs_; }
    const std::vector<Object>& objects() const { return objects_; }
    const std::vector<Stamp>& stamps() const { return stamps_; }
    const Object* objectByIndex(int index) const;
    const Object* objectById(const std::string& id) const;
    int objectIndexById(const std::string& id) const;
    bool objectContainsPoint(const Object& object, float px, float py) const;
    bool isWalkable(float worldX, float worldY, float radius) const;

private:
    void loadAtlasMetadata(const std::filesystem::path& atlasRoot, Logger& logger);
    void rebuildSpatialIndex();
    void collectStampCandidates(const Rect& area, std::vector<size_t>& out) const;
    void collectObjectCandidates(const Rect& area, std::vector<size_t>& out) const;
    std::optional<AtlasTileMeta> metaForAsset(const std::string& assetRef) const;
    Rect stampBounds(const Stamp& stamp) const;
    std::optional<Rect> objectCollisionRect(const Object& object) const;
    Rect stampCollisionRect(const Stamp& stamp, const AtlasTileMeta& meta) const;

    std::string mapId_;
    std::string worldName_;
    int tileSize_ = 32;
    int width_ = 0;
    int height_ = 0;
    float spawnX_ = 96.0f;
    float spawnY_ = 96.0f;
    std::unordered_map<std::string, std::string> properties_;
    std::vector<std::string> mapImageRefs_;
    std::vector<std::string> characterImageRefs_;
    std::vector<Object> objects_;
    std::vector<Stamp> stamps_;
    std::unordered_map<std::string, AtlasTileMeta> atlasTileMeta_;
    std::unordered_map<std::uint64_t, std::vector<size_t>> stampBuckets_;
    std::unordered_map<std::uint64_t, std::vector<size_t>> objectBuckets_;
    mutable std::vector<std::uint32_t> stampQueryMarks_;
    mutable std::uint32_t stampQueryToken_ = 1;
};
