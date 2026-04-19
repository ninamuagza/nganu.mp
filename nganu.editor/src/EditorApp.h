#pragma once

#include "raylib.h"

#include <filesystem>
#include <string>
#include <vector>

class EditorApp {
public:
    EditorApp();
    ~EditorApp();

    void Update(float dt);
    void Draw() const;

private:
    enum class EditorMode {
        Atlas = 0,
        Map = 1
    };

    struct AtlasAsset {
        std::string domain;
        std::string filename;
        std::filesystem::path path;
        Texture2D texture {};
        bool loaded = false;
    };

    enum class Domain {
        Map = 0,
        Character = 1
    };

    struct MapProperty {
        std::string key;
        std::string value;
    };

    struct MapLayerDef {
        std::string name;
        std::string kind;
        std::string asset;
        std::string tint = "#FFFFFFFF";
        float parallax = 1.0f;
    };

    struct MapStamp {
        std::string layer;
        int x = 0;
        int y = 0;
        std::string asset;
    };

    struct BrushStamp {
        int offsetX = 0;
        int offsetY = 0;
        std::string asset;
    };

    struct MapRect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct MapObject {
        std::string kind;
        std::string id;
        int x = 0;
        int y = 0;
        int width = 32;
        int height = 32;
        std::vector<MapProperty> properties;
    };

    enum class MapTool {
        Paint = 0,
        Erase = 1,
        Blocked = 2,
        Water = 3,
        Spawn = 4,
        Object = 5
    };

    enum class MapSection {
        Tile = 0,
        Region = 1,
        Object = 2
    };

    std::vector<AtlasAsset> mapAssets_;
    std::vector<AtlasAsset> characterAssets_;
    Font uiFont_ {};
    bool ownsUiFont_ = false;
    EditorMode mode_ = EditorMode::Atlas;
    Domain activeDomain_ = Domain::Map;
    int activeIndex_ = 0;
    int gridWidth_ = 32;
    int gridHeight_ = 32;
    int selectionCols_ = 1;
    int selectionRows_ = 1;
    int selectionX_ = 0;
    int selectionY_ = 0;
    float zoom_ = 3.0f;
    Vector2 pan_ {0.0f, 0.0f};
    bool panning_ = false;
    float leftRepeatTimer_ = 0.0f;
    float rightRepeatTimer_ = 0.0f;
    float upRepeatTimer_ = 0.0f;
    float downRepeatTimer_ = 0.0f;
    std::string statusText_;
    std::vector<std::filesystem::path> mapFiles_;
    int activeMapFileIndex_ = 0;
    std::filesystem::path currentMapFile_;
    std::string mapId_ = "new_map";
    std::string worldName_ = "New Region";
    int mapTileSize_ = 48;
    int mapWidth_ = 24;
    int mapHeight_ = 18;
    Vector2 mapSpawn_ {120.0f, 120.0f};
    std::vector<MapProperty> mapProperties_;
    std::vector<MapLayerDef> mapLayers_;
    std::vector<MapStamp> mapStamps_;
    std::vector<MapObject> mapObjects_;
    std::vector<MapRect> blockedAreas_;
    std::vector<MapRect> waterAreas_;
    float mapZoom_ = 1.5f;
    Vector2 mapPan_ {0.0f, 0.0f};
    bool mapPanning_ = false;
    float mapLeftRepeatTimer_ = 0.0f;
    float mapRightRepeatTimer_ = 0.0f;
    float mapUpRepeatTimer_ = 0.0f;
    float mapDownRepeatTimer_ = 0.0f;
    int activeMapLayerIndex_ = 0;
    MapTool mapTool_ = MapTool::Paint;
    MapSection activeMapSection_ = MapSection::Tile;
    int selectedMapObjectIndex_ = -1;
    std::string objectPlacementKind_ = "prop";

    std::filesystem::path projectRoot_;
    std::filesystem::path mapAssetRoot_;
    std::filesystem::path characterAssetRoot_;
    std::filesystem::path mapsRoot_;

    void ScanAssets();
    void ScanMaps();
    void LoadUiFont();
    std::vector<AtlasAsset>& ActiveAssets();
    const std::vector<AtlasAsset>& ActiveAssets() const;
    AtlasAsset* CurrentAsset();
    const AtlasAsset* CurrentAsset() const;
    void EnsureCurrentTextureLoaded();
    void UnloadAssets();
    void ChangeDomain();
    void StepAsset(int delta);
    void HandleKeyboardInput(float dt);
    void HandleMouseInput(const Rectangle& canvasBounds);
    void NewMapDocument();
    bool LoadMapFile(const std::filesystem::path& path);
    bool SaveCurrentMap() const;
    void StepMapFile(int delta);
    void HandleMapKeyboardInput(float dt);
    void HandleMapMouseInput(const Rectangle& canvasBounds);
    MapLayerDef* ActiveMapLayer();
    const MapLayerDef* ActiveMapLayer() const;
    Rectangle MapCanvasRect(const Rectangle& bounds) const;
    bool ScreenToMapTile(Vector2 screen, const Rectangle& bounds, int& outTileX, int& outTileY) const;
    bool ScreenToMapPixel(Vector2 screen, const Rectangle& bounds, int& outX, int& outY) const;
    void PaintMapTile(int tileX, int tileY);
    void EraseMapTile(int tileX, int tileY);
    void ToggleMapRect(std::vector<MapRect>& areas, int tileX, int tileY);
    bool HasMapRect(const std::vector<MapRect>& areas, int tileX, int tileY) const;
    std::string CurrentBrushRef() const;
    std::vector<BrushStamp> CurrentBrushPattern() const;
    int ObjectIndexAtPixel(int px, int py) const;
    void PlaceOrSelectObject(int px, int py);
    void DeleteSelectedObject();
    void ClampSelectionToTexture();
    Rectangle SelectionRectPixels() const;
    std::string BuildAtlasRef() const;
    void CopyCurrentRef();
    void DrawAtlasCanvas(Rectangle bounds) const;
    void DrawSidePanel(Rectangle bounds) const;
    void DrawMapCanvas(Rectangle bounds) const;
    void DrawMapSidePanel(Rectangle bounds) const;
    static const char* DomainLabel(Domain domain);
};
