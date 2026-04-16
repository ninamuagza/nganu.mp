#pragma once

#include "core/Logger.h"

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
        bool blocksMovement = false;
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
    const std::vector<Rect>& blockedAreas() const { return blockedAreas_; }
    const std::vector<Rect>& waterAreas() const { return waterAreas_; }
    const Object* objectByIndex(int index) const;
    const Object* objectById(const std::string& id) const;
    int objectIndexById(const std::string& id) const;
    bool objectContainsPoint(const Object& object, float px, float py) const;
    bool isWalkable(float worldX, float worldY, float radius) const;

private:
    void loadAtlasMetadata(const std::filesystem::path& atlasRoot, Logger& logger);
    std::optional<AtlasTileMeta> metaForAsset(const std::string& assetRef) const;

    std::string mapId_;
    std::string worldName_;
    int tileSize_ = 48;
    int width_ = 0;
    int height_ = 0;
    float spawnX_ = 160.0f;
    float spawnY_ = 160.0f;
    std::unordered_map<std::string, std::string> properties_;
    std::vector<std::string> mapImageRefs_;
    std::vector<std::string> characterImageRefs_;
    std::vector<Object> objects_;
    std::vector<Stamp> stamps_;
    std::vector<Rect> blockedAreas_;
    std::vector<Rect> waterAreas_;
    std::unordered_map<std::string, AtlasTileMeta> atlasTileMeta_;
};
