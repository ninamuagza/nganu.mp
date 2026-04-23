#pragma once

#include "raylib.h"

#include <filesystem>
#include <string>
#include <unordered_map>
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

    struct AtlasTileMeta {
        std::string collision;
        std::string tag;
        bool hasCollider = false;
        Rectangle collider {};
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
        Spawn = 2,
        Object = 3
    };

    enum class MapSection {
        Tile = 0,
        Spawn = 1,
        Object = 2
    };

    enum class ColliderEditTarget {
        None = 0,
        AtlasX,
        AtlasY,
        AtlasWidth,
        AtlasHeight,
        ObjectX,
        ObjectY,
        ObjectWidth,
        ObjectHeight
    };

    enum class ColliderDragMode {
        None = 0,
        Move,
        ResizeLeft,
        ResizeRight,
        ResizeTop,
        ResizeBottom,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomLeft,
        ResizeBottomRight
    };

    struct EditorSnapshot {
        EditorMode mode = EditorMode::Atlas;
        Domain activeDomain = Domain::Map;
        int activeIndex = 0;
        int gridWidth = 32;
        int gridHeight = 32;
        int selectionCols = 1;
        int selectionRows = 1;
        int selectionX = 0;
        int selectionY = 0;
        int activeMapFileIndex = 0;
        std::filesystem::path currentMapFile;
        std::string mapId;
        std::string worldName;
        int mapTileSize = 32;
        int mapWidth = 24;
        int mapHeight = 18;
        Vector2 mapSpawn {};
        std::vector<MapProperty> mapProperties;
        std::vector<MapLayerDef> mapLayers;
        std::vector<MapStamp> mapStamps;
        std::vector<MapObject> mapObjects;
        int activeMapLayerIndex = 0;
        MapTool mapTool = MapTool::Paint;
        MapSection activeMapSection = MapSection::Tile;
        int selectedMapObjectIndex = -1;
        std::string objectPlacementKind;
        std::unordered_map<std::string, AtlasTileMeta> atlasMetadata;
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
    bool atlasSelectionDragging_ = false;
    int atlasSelectionAnchorX_ = 0;
    int atlasSelectionAnchorY_ = 0;
    double lastAtlasClickTime_ = -10.0;
    int lastAtlasClickTileX_ = -1;
    int lastAtlasClickTileY_ = -1;
    std::string statusText_;
    std::vector<std::filesystem::path> mapFiles_;
    int activeMapFileIndex_ = 0;
    std::filesystem::path currentMapFile_;
    std::string mapId_ = "new_map";
    std::string worldName_ = "New Region";
    int mapTileSize_ = 32;
    int mapWidth_ = 24;
    int mapHeight_ = 18;
    Vector2 mapSpawn_ {64.0f, 64.0f};
    std::vector<MapProperty> mapProperties_;
    std::vector<MapLayerDef> mapLayers_;
    std::vector<MapStamp> mapStamps_;
    std::vector<MapObject> mapObjects_;
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
    std::unordered_map<std::string, AtlasTileMeta> atlasMetadata_;
    std::unordered_map<std::string, bool> loadedAtlasMetadata_;
    ColliderEditTarget colliderEditTarget_ = ColliderEditTarget::None;
    std::string colliderEditBuffer_;
    ColliderDragMode atlasColliderDragMode_ = ColliderDragMode::None;
    ColliderDragMode objectColliderDragMode_ = ColliderDragMode::None;
    Vector2 colliderDragStartMouse_ {0.0f, 0.0f};
    Rectangle colliderDragStartRect_ {};
    bool atlasColliderDirtyDuringDrag_ = false;
    std::vector<EditorSnapshot> undoStack_;
    std::vector<EditorSnapshot> redoStack_;
    bool restoringSnapshot_ = false;

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
    void EnsureAssetTextureLoaded(AtlasAsset& asset);
    void EnsureCurrentTextureLoaded();
    void EnsureMapRenderTexturesLoaded();
    void UnloadAssets();
    void LoadAtlasMetadataForAsset(const AtlasAsset& asset);
    bool SaveAtlasMetadataForAsset(const AtlasAsset& asset) const;
    void SaveAllAtlasMetadata() const;
    EditorSnapshot CaptureSnapshot() const;
    void RestoreSnapshot(const EditorSnapshot& snapshot);
    void PushUndoSnapshot();
    void Undo();
    void Redo();
    void HandleUndoRedoInput();
    std::string CurrentAtlasMetaKey() const;
    const AtlasTileMeta* CurrentAtlasMeta() const;
    const AtlasTileMeta* MetaForAtlasRef(const std::string& assetRef) const;
    void SetCurrentAtlasCollision(const std::string& collision);
    void SetCurrentAtlasTrunkCollider();
    void ClearCurrentAtlasCollision();
    void AdjustCurrentAtlasCollider(int moveX, int moveY, int growWidth, int growHeight);
    Rectangle CurrentAtlasColliderOrDefault() const;
    void SetCurrentAtlasColliderRect(Rectangle rect, bool saveNow);
    void SetSelectedObjectCollision(const std::string& collision);
    void SetSelectedObjectTrunkCollider();
    void AdjustSelectedObjectCollider(int moveX, int moveY, int growWidth, int growHeight);
    Rectangle SelectedObjectColliderOrDefault() const;
    void SetSelectedObjectColliderRect(Rectangle rect);
    void BeginColliderTextEdit(ColliderEditTarget target);
    void CommitColliderTextEdit();
    void CancelColliderTextEdit();
    void HandleColliderTextInput();
    bool HandleAtlasColliderMouseInput(const Rectangle& canvasBounds);
    bool HandleSelectedObjectColliderMouseInput(const Rectangle& canvasBounds);
    bool HandleAtlasColliderPanelInput(const Rectangle& panelBounds);
    bool HandleObjectColliderPanelInput(const Rectangle& panelBounds);
    void AddMapLayer();
    void RemoveActiveMapLayer();
    void MoveActiveMapLayer(int delta);
    void ChangeDomain();
    void StepAsset(int delta);
    void HandleKeyboardInput(float dt);
    void HandleMouseInput(const Rectangle& canvasBounds, const Rectangle& sideBounds);
    void NewMapDocument();
    bool LoadMapFile(const std::filesystem::path& path);
    bool SaveCurrentMap() const;
    void StepMapFile(int delta);
    void HandleMapKeyboardInput(float dt);
    void HandleMapMouseInput(const Rectangle& canvasBounds, const Rectangle& sideBounds);
    MapLayerDef* ActiveMapLayer();
    const MapLayerDef* ActiveMapLayer() const;
    Rectangle MapCanvasRect(const Rectangle& bounds) const;
    bool ScreenToMapTile(Vector2 screen, const Rectangle& bounds, int& outTileX, int& outTileY) const;
    bool ScreenToMapPixel(Vector2 screen, const Rectangle& bounds, int& outX, int& outY) const;
    void PaintMapTile(int tileX, int tileY);
    void EraseMapTile(int tileX, int tileY);
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
    void DrawAtlasColliderInputs(Rectangle bounds) const;
    void DrawMapCanvas(Rectangle bounds) const;
    void DrawMapSidePanel(Rectangle bounds) const;
    void DrawObjectColliderInputs(Rectangle bounds) const;
    static const char* DomainLabel(Domain domain);
};
