#pragma once

#include "raylib.h"
#include <optional>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <vector>

struct WorldLayer {
    std::string name;
    std::string kind;
    std::string asset;
    Color tint {255, 255, 255, 255};
    float parallax = 1.0f;
};

enum class AssetDomain {
    Map,
    Character
};

struct WorldObject {
    std::string kind;
    std::string id;
    Rectangle bounds {};
    std::unordered_map<std::string, std::string> properties;
};

struct WorldStamp {
    std::string layer;
    int x = 0;
    int y = 0;
    std::string asset;
};

struct WorldAtlasTileMeta {
    bool blocksMovement = false;
    std::string tag;
};

class World {
public:
    World();
    ~World();

    bool IsWalkable(Vector2 worldPosition, float radius) const;
    Rectangle Bounds() const;
    void DrawGround(Rectangle visibleArea) const;
    void DrawDecorations(Rectangle visibleArea) const;
    bool LoadFromMapAsset(const std::string& rawAsset);
    void SetMapAssetRoot(std::filesystem::path assetRoot) { mapAssetRoot_ = std::move(assetRoot); }
    void SetCharacterAssetRoot(std::filesystem::path assetRoot) { characterAssetRoot_ = std::move(assetRoot); }
    std::string mapId() const { return mapId_; }
    std::string worldName() const { return worldName_; }
    Vector2 spawnPoint() const { return spawnPoint_; }
    std::optional<std::string> property(const std::string& key) const;
    std::optional<std::string> objectProperty(const WorldObject& object, const std::string& key) const;
    const WorldObject* findObjectById(const std::string& id) const;
    const WorldObject* regionAt(Vector2 worldPosition) const;
    Vector2 objectCenter(const WorldObject& object) const;
    int objectZLayer(const WorldObject& object) const;
    Vector2 objectPivot(const WorldObject& object) const;
    float objectFacing(const WorldObject& object) const;
    bool DrawSpriteRef(const std::string& spriteRef, Rectangle dest, Vector2 origin = Vector2 {}, float rotation = 0.0f, Color tint = WHITE) const;
    bool DrawObjectSprite(const WorldObject& object, Color tint = WHITE) const;
    std::vector<std::string> referencedMapImageFiles() const;
    std::vector<std::string> referencedCharacterImageFiles() const;
    const std::vector<WorldLayer>& layers() const { return layers_; }
    const std::vector<WorldObject>& objects() const { return objects_; }
    const std::vector<WorldStamp>& stamps() const { return stamps_; }

private:
    void LoadDefaults();
    void UnloadTextures();
    void LoadAtlasMetadataForRef(const std::string& assetRef);
    std::optional<WorldAtlasTileMeta> metaForAsset(const std::string& assetRef) const;

    int tileSize_ = 48;
    int width_ = 42;
    int height_ = 30;
    std::string mapId_ = "starter_field";
    std::string worldName_ = "Greenfields";
    Vector2 spawnPoint_ {160.0f, 640.0f};
    std::unordered_map<std::string, std::string> properties_;
    std::vector<WorldLayer> layers_;
    std::vector<WorldStamp> stamps_;
    std::vector<WorldObject> objects_;
    std::vector<Rectangle> blockedAreas_;
    std::vector<Rectangle> waterAreas_;
    std::unordered_map<std::string, WorldAtlasTileMeta> atlasTileMeta_;
    mutable std::unordered_map<std::string, Texture2D> textures_;
    std::filesystem::path mapAssetRoot_;
    std::filesystem::path characterAssetRoot_;
};
