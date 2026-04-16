#include "AtlasEditor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
Color BackgroundColor() { return Color {238, 232, 220, 255}; }
Color PanelColor() { return Color {34, 43, 47, 255}; }
Color AccentColor() { return Color {219, 142, 71, 255}; }
Color AccentSoft() { return Color {241, 191, 140, 255}; }
Color InkColor() { return Color {20, 24, 26, 255}; }
Color MapGridColor() { return Color {40, 51, 58, 70}; }
Color BlockedColor() { return Color {184, 83, 67, 180}; }
Color WaterColor() { return Color {64, 139, 196, 170}; }
constexpr float kUiFontScale = 0.7f;

bool HasPngExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".png";
}

float ClampZoom(float value) {
    return std::clamp(value, 0.35f, 12.0f);
}

int UiFont(int px) {
    return std::max(10, static_cast<int>(std::round(static_cast<float>(px) * kUiFontScale)));
}

void DrawUiText(const Font& font, const char* text, float x, float y, int size, Color color) {
    DrawTextEx(font, text, Vector2 {x, y}, static_cast<float>(size), 0.0f, color);
}

void DrawUiText(const Font& font, const std::string& text, float x, float y, int size, Color color) {
    DrawUiText(font, text.c_str(), x, y, size, color);
}

bool ConsumeRepeat(bool pressed, bool down, float& timer, float dt) {
    constexpr float kInitialDelay = 0.28f;
    constexpr float kRepeatInterval = 0.06f;
    if (pressed) {
        timer = kInitialDelay;
        return true;
    }
    if (!down) {
        timer = 0.0f;
        return false;
    }
    timer -= dt;
    if (timer > 0.0f) {
        return false;
    }
    timer += kRepeatInterval;
    return true;
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

struct AtlasRef {
    std::string domain;
    std::string file;
    Rectangle source {};
    bool valid = false;
};

AtlasRef ParseAtlasRef(const std::string& asset) {
    AtlasRef ref;
    const std::vector<std::string> parts = SplitEscaped(asset, '@');
    if (parts.size() != 5) {
        return ref;
    }

    ref.file = parts[0];
    const size_t domainSep = ref.file.find(':');
    if (domainSep != std::string::npos) {
        ref.domain = ref.file.substr(0, domainSep);
        ref.file = ref.file.substr(domainSep + 1);
    } else {
        ref.domain = "map";
    }

    try {
        ref.source.x = std::stof(parts[1]);
        ref.source.y = std::stof(parts[2]);
        ref.source.width = std::stof(parts[3]);
        ref.source.height = std::stof(parts[4]);
    } catch (...) {
        return ref;
    }

    ref.valid = !ref.file.empty() && ref.source.width > 0.0f && ref.source.height > 0.0f;
    return ref;
}

template <typename PropertyCollection>
std::string PropertyValue(const PropertyCollection& properties, const std::string& key) {
    for (const auto& property : properties) {
        if (property.key == key) {
            return property.value;
        }
    }
    return {};
}

template <typename PropertyCollection>
void SetPropertyValue(PropertyCollection& properties, const std::string& key, const std::string& value) {
    for (auto& property : properties) {
        if (property.key == key) {
            property.value = value;
            return;
        }
    }
    properties.push_back({key, value});
}
}

AtlasEditor::AtlasEditor() {
    projectRoot_ = std::filesystem::current_path().parent_path();
    mapAssetRoot_ = projectRoot_ / "nganu.mp" / "game-server" / "assets" / "map_images";
    characterAssetRoot_ = projectRoot_ / "nganu.mp" / "game-server" / "assets" / "characters";
    mapsRoot_ = projectRoot_ / "nganu.mp" / "game-server" / "assets" / "maps";
    LoadUiFont();
    ScanAssets();
    ScanMaps();
    if (!mapFiles_.empty()) {
        LoadMapFile(mapFiles_.front());
    } else {
        NewMapDocument();
    }
    statusText_ = "Atlas mode: choose a brush, then switch to Map mode with F2.";
}

AtlasEditor::~AtlasEditor() {
    if (ownsUiFont_ && uiFont_.texture.id > 0) {
        UnloadFont(uiFont_);
    }
    UnloadAssets();
}

void AtlasEditor::LoadUiFont() {
    const std::vector<std::filesystem::path> candidates {
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf"
    };

    for (const auto& path : candidates) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        uiFont_ = LoadFontEx(path.string().c_str(), 64, nullptr, 0);
        ownsUiFont_ = uiFont_.texture.id > 0;
        if (ownsUiFont_) {
            GenTextureMipmaps(&uiFont_.texture);
            SetTextureFilter(uiFont_.texture, TEXTURE_FILTER_TRILINEAR);
            break;
        }
    }
    if (!ownsUiFont_) {
        uiFont_ = GetFontDefault();
        SetTextureFilter(uiFont_.texture, TEXTURE_FILTER_BILINEAR);
    }
}

void AtlasEditor::ScanAssets() {
    UnloadAssets();
    mapAssets_.clear();
    characterAssets_.clear();

    const auto scanDir = [](const std::filesystem::path& root, const std::string& domain, std::vector<AtlasAsset>& out) {
        if (!std::filesystem::exists(root)) {
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file() || !HasPngExtension(entry.path())) {
                continue;
            }
            AtlasAsset asset;
            asset.domain = domain;
            asset.filename = entry.path().filename().string();
            asset.path = entry.path();
            out.push_back(std::move(asset));
        }
        std::sort(out.begin(), out.end(), [](const AtlasAsset& a, const AtlasAsset& b) {
            return a.filename < b.filename;
        });
    };

    scanDir(mapAssetRoot_, "map", mapAssets_);
    scanDir(characterAssetRoot_, "character", characterAssets_);
    if (ActiveAssets().empty() && activeDomain_ == Domain::Map && !characterAssets_.empty()) {
        activeDomain_ = Domain::Character;
    }
    activeIndex_ = std::clamp(activeIndex_, 0, std::max(0, static_cast<int>(ActiveAssets().size()) - 1));
}

void AtlasEditor::ScanMaps() {
    mapFiles_.clear();
    if (!std::filesystem::exists(mapsRoot_)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(mapsRoot_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".map") {
            continue;
        }
        mapFiles_.push_back(entry.path());
    }
    std::sort(mapFiles_.begin(), mapFiles_.end());
    if (!mapFiles_.empty()) {
        activeMapFileIndex_ = std::clamp(activeMapFileIndex_, 0, std::max(0, static_cast<int>(mapFiles_.size()) - 1));
    } else {
        activeMapFileIndex_ = 0;
    }
}

std::vector<AtlasEditor::AtlasAsset>& AtlasEditor::ActiveAssets() {
    return activeDomain_ == Domain::Map ? mapAssets_ : characterAssets_;
}

const std::vector<AtlasEditor::AtlasAsset>& AtlasEditor::ActiveAssets() const {
    return activeDomain_ == Domain::Map ? mapAssets_ : characterAssets_;
}

AtlasEditor::AtlasAsset* AtlasEditor::CurrentAsset() {
    auto& assets = ActiveAssets();
    if (assets.empty() || activeIndex_ < 0 || activeIndex_ >= static_cast<int>(assets.size())) {
        return nullptr;
    }
    return &assets[activeIndex_];
}

const AtlasEditor::AtlasAsset* AtlasEditor::CurrentAsset() const {
    const auto& assets = ActiveAssets();
    if (assets.empty() || activeIndex_ < 0 || activeIndex_ >= static_cast<int>(assets.size())) {
        return nullptr;
    }
    return &assets[activeIndex_];
}

void AtlasEditor::EnsureCurrentTextureLoaded() {
    AtlasAsset* asset = CurrentAsset();
    if (!asset || asset->loaded) {
        return;
    }
    asset->texture = LoadTexture(asset->path.string().c_str());
    asset->loaded = asset->texture.id > 0;
    ClampSelectionToTexture();
}

void AtlasEditor::UnloadAssets() {
    auto unload = [](std::vector<AtlasAsset>& assets) {
        for (AtlasAsset& asset : assets) {
            if (asset.loaded && asset.texture.id > 0) {
                UnloadTexture(asset.texture);
            }
            asset.texture = Texture2D {};
            asset.loaded = false;
        }
    };
    unload(mapAssets_);
    unload(characterAssets_);
}

void AtlasEditor::ChangeDomain() {
    activeDomain_ = activeDomain_ == Domain::Map ? Domain::Character : Domain::Map;
    if (ActiveAssets().empty()) {
        activeDomain_ = activeDomain_ == Domain::Map ? Domain::Character : Domain::Map;
    }
    activeIndex_ = std::clamp(activeIndex_, 0, std::max(0, static_cast<int>(ActiveAssets().size()) - 1));
    ClampSelectionToTexture();
}

void AtlasEditor::StepAsset(int delta) {
    auto& assets = ActiveAssets();
    if (assets.empty()) {
        activeIndex_ = 0;
        return;
    }
    activeIndex_ += delta;
    if (activeIndex_ < 0) {
        activeIndex_ = static_cast<int>(assets.size()) - 1;
    } else if (activeIndex_ >= static_cast<int>(assets.size())) {
        activeIndex_ = 0;
    }
    ClampSelectionToTexture();
}

void AtlasEditor::NewMapDocument() {
    currentMapFile_.clear();
    mapId_ = "new_map";
    worldName_ = "New Region";
    mapTileSize_ = 48;
    mapWidth_ = 24;
    mapHeight_ = 18;
    mapSpawn_ = Vector2 {static_cast<float>(mapTileSize_ * 2), static_cast<float>(mapTileSize_ * 2)};
    mapProperties_.clear();
    mapProperties_.push_back({"music", "prototype_day"});
    mapProperties_.push_back({"climate", "temperate"});
    mapLayers_.clear();
    mapLayers_.push_back({"ground", "image", "", "#FFFFFFFF", 1.0f});
    mapLayers_.push_back({"road", "image", "", "#FFFFFFFF", 1.0f});
    mapLayers_.push_back({"detail", "image", "", "#FFFFFFFF", 1.0f});
    mapStamps_.clear();
    mapObjects_.clear();
    blockedAreas_.clear();
    waterAreas_.clear();
    activeMapLayerIndex_ = 0;
    mapTool_ = MapTool::Paint;
    activeMapSection_ = MapSection::Tile;
    selectedMapObjectIndex_ = -1;
    objectPlacementKind_ = "prop";
}

bool AtlasEditor::LoadMapFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        statusText_ = "Failed to open map: " + path.filename().string();
        return false;
    }

    NewMapDocument();
    currentMapFile_ = path;
    mapId_ = path.stem().string();
    worldName_ = path.stem().string();
    mapProperties_.clear();
    mapLayers_.clear();

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        try {
            if (key == "map_id") {
                mapId_ = value;
            } else if (key == "world_name") {
                worldName_ = value;
            } else if (key == "tile") {
                mapTileSize_ = std::stoi(value);
            } else if (key == "width") {
                mapWidth_ = std::stoi(value);
            } else if (key == "height") {
                mapHeight_ = std::stoi(value);
            } else if (key == "spawn") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 2) {
                    mapSpawn_.x = std::stof(parts[0]);
                    mapSpawn_.y = std::stof(parts[1]);
                }
            } else if (key == "property") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 2) {
                    mapProperties_.push_back({parts[0], parts[1]});
                }
            } else if (key == "layer") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 5) {
                    MapLayerDef layer;
                    layer.name = parts[0];
                    layer.kind = parts[1];
                    layer.asset = parts[2];
                    layer.tint = parts[3];
                    layer.parallax = std::stof(parts[4]);
                    mapLayers_.push_back(std::move(layer));
                }
            } else if (key == "stamp") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 4) {
                    MapStamp stamp;
                    stamp.layer = parts[0];
                    stamp.x = std::stoi(parts[1]);
                    stamp.y = std::stoi(parts[2]);
                    stamp.asset = parts[3];
                    mapStamps_.push_back(std::move(stamp));
                }
            } else if (key == "object") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 6) {
                    MapObject object;
                    object.kind = parts[0];
                    object.id = parts[1];
                    object.x = std::stoi(parts[2]);
                    object.y = std::stoi(parts[3]);
                    object.width = std::stoi(parts[4]);
                    object.height = std::stoi(parts[5]);
                    for (size_t i = 6; i < parts.size(); ++i) {
                        const auto kv = SplitEscaped(parts[i], ':');
                        if (kv.size() >= 2) {
                            object.properties.push_back({kv[0], kv[1]});
                        }
                    }
                    mapObjects_.push_back(std::move(object));
                }
            } else if (key == "blocked") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 4) {
                    blockedAreas_.push_back({std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3])});
                }
            } else if (key == "water") {
                const auto parts = SplitEscaped(value, ',');
                if (parts.size() >= 4) {
                    waterAreas_.push_back({std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3])});
                }
            }
        } catch (...) {
            statusText_ = "Map parse issue near: " + line;
            return false;
        }
    }

    if (mapLayers_.empty()) {
        mapLayers_.push_back({"ground", "image", "map:terrain_atlas.png@0@0@32@32", "#FFFFFFFF", 1.0f});
    }
    activeMapLayerIndex_ = std::clamp(activeMapLayerIndex_, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    statusText_ = "Loaded map " + path.filename().string();
    return true;
}

bool AtlasEditor::SaveCurrentMap() const {
    std::filesystem::path output = currentMapFile_;
    if (output.empty()) {
        output = mapsRoot_ / (mapId_ + ".map");
    }
    try {
        std::filesystem::create_directories(output.parent_path());
        std::ofstream out(output, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }

        out << "map_id=" << mapId_ << "\n";
        out << "world_name=" << worldName_ << "\n";
        out << "tile=" << mapTileSize_ << "\n";
        out << "width=" << mapWidth_ << "\n";
        out << "height=" << mapHeight_ << "\n";
        out << "spawn=" << static_cast<int>(mapSpawn_.x) << "," << static_cast<int>(mapSpawn_.y) << "\n";
        for (const MapProperty& property : mapProperties_) {
            out << "property=" << EscapeValue(property.key) << "," << EscapeValue(property.value) << "\n";
        }
        for (const MapLayerDef& layer : mapLayers_) {
            out << "layer=" << EscapeValue(layer.name) << "," << EscapeValue(layer.kind) << "," << EscapeValue(layer.asset)
                << "," << layer.tint << "," << layer.parallax << "\n";
        }
        for (const MapStamp& stamp : mapStamps_) {
            out << "stamp=" << EscapeValue(stamp.layer) << "," << stamp.x << "," << stamp.y << "," << EscapeValue(stamp.asset) << "\n";
        }
        for (const MapObject& object : mapObjects_) {
            out << "object=" << EscapeValue(object.kind) << "," << EscapeValue(object.id) << ","
                << object.x << "," << object.y << "," << object.width << "," << object.height;
            for (const MapProperty& property : object.properties) {
                out << "," << EscapeValue(property.key) << ":" << EscapeValue(property.value);
            }
            out << "\n";
        }
        for (const MapRect& rect : blockedAreas_) {
            out << "blocked=" << rect.x << "," << rect.y << "," << rect.width << "," << rect.height << "\n";
        }
        for (const MapRect& rect : waterAreas_) {
            out << "water=" << rect.x << "," << rect.y << "," << rect.width << "," << rect.height << "\n";
        }
        return out.good();
    } catch (...) {
        return false;
    }
}

void AtlasEditor::StepMapFile(int delta) {
    if (mapFiles_.empty()) {
        return;
    }
    activeMapFileIndex_ += delta;
    if (activeMapFileIndex_ < 0) {
        activeMapFileIndex_ = static_cast<int>(mapFiles_.size()) - 1;
    } else if (activeMapFileIndex_ >= static_cast<int>(mapFiles_.size())) {
        activeMapFileIndex_ = 0;
    }
    LoadMapFile(mapFiles_[activeMapFileIndex_]);
}

AtlasEditor::MapLayerDef* AtlasEditor::ActiveMapLayer() {
    if (mapLayers_.empty() || activeMapLayerIndex_ < 0 || activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        return nullptr;
    }
    return &mapLayers_[activeMapLayerIndex_];
}

const AtlasEditor::MapLayerDef* AtlasEditor::ActiveMapLayer() const {
    if (mapLayers_.empty() || activeMapLayerIndex_ < 0 || activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        return nullptr;
    }
    return &mapLayers_[activeMapLayerIndex_];
}

std::string AtlasEditor::CurrentBrushRef() const {
    const AtlasAsset* asset = CurrentAsset();
    if (!asset) {
        return {};
    }
    if (asset->domain != "map") {
        return {};
    }
    return BuildAtlasRef();
}

std::vector<AtlasEditor::BrushStamp> AtlasEditor::CurrentBrushPattern() const {
    std::vector<BrushStamp> pattern;
    const AtlasAsset* asset = CurrentAsset();
    if (!asset || asset->domain != "map") {
        return pattern;
    }

    const Rectangle selection = SelectionRectPixels();
    const int cols = std::max(1, selectionCols_);
    const int rows = std::max(1, selectionRows_);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            std::ostringstream ref;
            ref << asset->domain << ":" << asset->filename
                << "@" << static_cast<int>(selection.x) + (x * gridWidth_)
                << "@" << static_cast<int>(selection.y) + (y * gridHeight_)
                << "@" << gridWidth_
                << "@" << gridHeight_;
            pattern.push_back(BrushStamp {x, y, ref.str()});
        }
    }
    return pattern;
}

void AtlasEditor::HandleKeyboardInput(float dt) {
    const float panStep = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? 18.0f : 9.0f;
    if (IsKeyPressed(KEY_F1)) {
        mode_ = EditorMode::Atlas;
        statusText_ = "Atlas mode.";
    }
    if (IsKeyPressed(KEY_F2)) {
        mode_ = EditorMode::Map;
        activeDomain_ = Domain::Map;
        statusText_ = "Map mode.";
    }
    if (mode_ != EditorMode::Atlas) {
        return;
    }

    if (IsKeyPressed(KEY_TAB)) ChangeDomain();
    if (IsKeyPressed(KEY_Q)) StepAsset(-1);
    if (IsKeyPressed(KEY_E)) StepAsset(1);
    if (IsKeyPressed(KEY_R)) {
        zoom_ = 3.0f;
        pan_ = Vector2 {0.0f, 0.0f};
    }
    if (IsKeyPressed(KEY_C)) CopyCurrentRef();
    if (IsKeyPressed(KEY_ONE)) { gridWidth_ = 16; gridHeight_ = 16; }
    if (IsKeyPressed(KEY_TWO)) { gridWidth_ = 24; gridHeight_ = 24; }
    if (IsKeyPressed(KEY_THREE)) { gridWidth_ = 32; gridHeight_ = 32; }
    if (IsKeyPressed(KEY_FOUR)) { gridWidth_ = 48; gridHeight_ = 48; }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) gridWidth_ = std::max(8, gridWidth_ - 8);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) gridWidth_ = std::min(128, gridWidth_ + 8);
    if (IsKeyPressed(KEY_SEMICOLON)) gridHeight_ = std::max(8, gridHeight_ - 8);
    if (IsKeyPressed(KEY_APOSTROPHE)) gridHeight_ = std::min(128, gridHeight_ + 8);

    if (IsKeyDown(KEY_A)) pan_.x += panStep;
    if (IsKeyDown(KEY_D)) pan_.x -= panStep;
    if (IsKeyDown(KEY_W)) pan_.y += panStep;
    if (IsKeyDown(KEY_S)) pan_.y -= panStep;

    const bool resizeMode = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), leftRepeatTimer_, dt)) {
        if (resizeMode) selectionCols_ = std::max(1, selectionCols_ - 1);
        else selectionX_ = std::max(0, selectionX_ - 1);
    }
    if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), rightRepeatTimer_, dt)) {
        if (resizeMode) selectionCols_ += 1;
        else selectionX_ += 1;
    }
    if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), upRepeatTimer_, dt)) {
        if (resizeMode) selectionRows_ = std::max(1, selectionRows_ - 1);
        else selectionY_ = std::max(0, selectionY_ - 1);
    }
    if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), downRepeatTimer_, dt)) {
        if (resizeMode) selectionRows_ += 1;
        else selectionY_ += 1;
    }

    ClampSelectionToTexture();
}

void AtlasEditor::HandleMouseInput(const Rectangle& canvasBounds) {
    if (mode_ != EditorMode::Atlas) {
        return;
    }

    AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(GetMousePosition(), canvasBounds)) {
        zoom_ = ClampZoom(zoom_ + wheel * 0.25f);
    }

    const bool panDragHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ||
        (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    if (CheckCollisionPointRec(GetMousePosition(), canvasBounds) && panDragHeld) {
        const Vector2 delta = GetMouseDelta();
        pan_.x += delta.x;
        pan_.y += delta.y;
        panning_ = true;
    } else if (panning_ && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        panning_ = false;
    }

    const bool leftPick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsKeyDown(KEY_SPACE) &&
        !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (!CheckCollisionPointRec(GetMousePosition(), canvasBounds) || !leftPick) {
        return;
    }

    const float drawWidth = static_cast<float>(asset->texture.width) * zoom_;
    const float drawHeight = static_cast<float>(asset->texture.height) * zoom_;
    const Rectangle drawRect {canvasBounds.x + 18.0f + pan_.x, canvasBounds.y + 18.0f + pan_.y, drawWidth, drawHeight};
    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, drawRect)) {
        return;
    }

    const float localX = (mouse.x - drawRect.x) / zoom_;
    const float localY = (mouse.y - drawRect.y) / zoom_;
    selectionX_ = std::max(0, static_cast<int>(std::floor(localX / static_cast<float>(gridWidth_))));
    selectionY_ = std::max(0, static_cast<int>(std::floor(localY / static_cast<float>(gridHeight_))));
    ClampSelectionToTexture();
}

Rectangle AtlasEditor::MapCanvasRect(const Rectangle& bounds) const {
    return Rectangle {
        bounds.x + mapPan_.x,
        bounds.y + mapPan_.y,
        static_cast<float>(mapWidth_ * mapTileSize_) * mapZoom_,
        static_cast<float>(mapHeight_ * mapTileSize_) * mapZoom_
    };
}

bool AtlasEditor::ScreenToMapTile(Vector2 screen, const Rectangle& bounds, int& outTileX, int& outTileY) const {
    const Rectangle canvas = MapCanvasRect(bounds);
    if (!CheckCollisionPointRec(screen, canvas)) {
        return false;
    }
    const float localX = (screen.x - canvas.x) / mapZoom_;
    const float localY = (screen.y - canvas.y) / mapZoom_;
    outTileX = std::clamp(static_cast<int>(std::floor(localX / static_cast<float>(mapTileSize_))), 0, std::max(0, mapWidth_ - 1));
    outTileY = std::clamp(static_cast<int>(std::floor(localY / static_cast<float>(mapTileSize_))), 0, std::max(0, mapHeight_ - 1));
    return true;
}

bool AtlasEditor::ScreenToMapPixel(Vector2 screen, const Rectangle& bounds, int& outX, int& outY) const {
    const Rectangle canvas = MapCanvasRect(bounds);
    if (!CheckCollisionPointRec(screen, canvas)) {
        return false;
    }
    outX = std::clamp(static_cast<int>((screen.x - canvas.x) / mapZoom_), 0, std::max(0, mapWidth_ * mapTileSize_ - 1));
    outY = std::clamp(static_cast<int>((screen.y - canvas.y) / mapZoom_), 0, std::max(0, mapHeight_ * mapTileSize_ - 1));
    return true;
}

void AtlasEditor::PaintMapTile(int tileX, int tileY) {
    MapLayerDef* layer = ActiveMapLayer();
    const std::vector<BrushStamp> brush = CurrentBrushPattern();
    if (!layer || brush.empty()) {
        statusText_ = "Pick a map-domain atlas brush in Atlas mode first.";
        return;
    }

    for (const BrushStamp& part : brush) {
        const int targetX = tileX + part.offsetX;
        const int targetY = tileY + part.offsetY;
        if (targetX < 0 || targetY < 0 || targetX >= mapWidth_ || targetY >= mapHeight_) {
            continue;
        }

        bool replaced = false;
        for (MapStamp& stamp : mapStamps_) {
            if (stamp.layer == layer->name && stamp.x == targetX && stamp.y == targetY) {
                stamp.asset = part.asset;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            mapStamps_.push_back({layer->name, targetX, targetY, part.asset});
        }
    }
}

void AtlasEditor::EraseMapTile(int tileX, int tileY) {
    MapLayerDef* layer = ActiveMapLayer();
    if (!layer) {
        return;
    }
    mapStamps_.erase(std::remove_if(mapStamps_.begin(), mapStamps_.end(), [&](const MapStamp& stamp) {
        return stamp.layer == layer->name && stamp.x == tileX && stamp.y == tileY;
    }), mapStamps_.end());
}

bool AtlasEditor::HasMapRect(const std::vector<MapRect>& areas, int tileX, int tileY) const {
    const int px = tileX * mapTileSize_;
    const int py = tileY * mapTileSize_;
    for (const MapRect& rect : areas) {
        if (px >= rect.x && py >= rect.y && px < rect.x + rect.width && py < rect.y + rect.height) {
            return true;
        }
    }
    return false;
}

void AtlasEditor::ToggleMapRect(std::vector<MapRect>& areas, int tileX, int tileY) {
    const int px = tileX * mapTileSize_;
    const int py = tileY * mapTileSize_;
    for (size_t i = 0; i < areas.size(); ++i) {
        const MapRect& rect = areas[i];
        if (rect.x == px && rect.y == py && rect.width == mapTileSize_ && rect.height == mapTileSize_) {
            areas.erase(areas.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
    areas.push_back({px, py, mapTileSize_, mapTileSize_});
}

int AtlasEditor::ObjectIndexAtPixel(int px, int py) const {
    for (int i = static_cast<int>(mapObjects_.size()) - 1; i >= 0; --i) {
        const MapObject& object = mapObjects_[static_cast<size_t>(i)];
        if (px >= object.x && py >= object.y && px < object.x + object.width && py < object.y + object.height) {
            return i;
        }
    }
    return -1;
}

void AtlasEditor::PlaceOrSelectObject(int px, int py) {
    const int existing = ObjectIndexAtPixel(px, py);
    if (existing >= 0) {
        selectedMapObjectIndex_ = existing;
        statusText_ = "Selected object " + mapObjects_[static_cast<size_t>(existing)].id;
        return;
    }

    MapObject object;
    object.kind = objectPlacementKind_;
    object.id = objectPlacementKind_ + "_" + std::to_string(static_cast<int>(mapObjects_.size()) + 1);
    object.x = (px / mapTileSize_) * mapTileSize_;
    object.y = (py / mapTileSize_) * mapTileSize_;
    object.width = mapTileSize_;
    object.height = mapTileSize_;
    const std::string brush = CurrentBrushRef();
    if (!brush.empty()) {
        SetPropertyValue(object.properties, "sprite", brush);
    }
    if (object.kind == "portal") {
        SetPropertyValue(object.properties, "title", "Portal");
        SetPropertyValue(object.properties, "target_x", std::to_string(object.x));
        SetPropertyValue(object.properties, "target_y", std::to_string(object.y));
    } else if (object.kind == "npc") {
        SetPropertyValue(object.properties, "title", "NPC");
        SetPropertyValue(object.properties, "name", "Guide");
    } else if (object.kind == "region") {
        object.width = mapTileSize_ * 8;
        object.height = mapTileSize_ * 6;
        SetPropertyValue(object.properties, "title", "Region");
        SetPropertyValue(object.properties, "climate", "temperate");
    } else if (object.kind == "trigger") {
        object.width = mapTileSize_ * 2;
        object.height = mapTileSize_ * 2;
        SetPropertyValue(object.properties, "title", "Trigger");
    } else {
        SetPropertyValue(object.properties, "title", "Prop");
    }
    mapObjects_.push_back(std::move(object));
    selectedMapObjectIndex_ = static_cast<int>(mapObjects_.size()) - 1;
    statusText_ = "Placed object " + mapObjects_.back().id;
}

void AtlasEditor::DeleteSelectedObject() {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        return;
    }
    const std::string removedId = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)].id;
    mapObjects_.erase(mapObjects_.begin() + selectedMapObjectIndex_);
    selectedMapObjectIndex_ = -1;
    statusText_ = "Deleted object " + removedId;
}

void AtlasEditor::HandleMapKeyboardInput(float dt) {
    if (mode_ != EditorMode::Map) {
        return;
    }

    const float panStep = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? 20.0f : 10.0f;
    if (IsKeyPressed(KEY_N) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        NewMapDocument();
        statusText_ = "New map document.";
    }
    if (IsKeyPressed(KEY_S) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (SaveCurrentMap()) {
            statusText_ = "Map saved.";
            ScanMaps();
        } else {
            statusText_ = "Map save failed.";
        }
    }
    if (IsKeyPressed(KEY_PAGE_UP)) StepMapFile(-1);
    if (IsKeyPressed(KEY_PAGE_DOWN)) StepMapFile(1);
    if (IsKeyPressed(KEY_TAB)) {
        if (activeMapSection_ == MapSection::Tile) activeMapSection_ = MapSection::Region;
        else if (activeMapSection_ == MapSection::Region) activeMapSection_ = MapSection::Object;
        else activeMapSection_ = MapSection::Tile;
    }
    if (IsKeyPressed(KEY_ONE)) activeMapLayerIndex_ = std::min(0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_TWO)) activeMapLayerIndex_ = std::clamp(1, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_THREE)) activeMapLayerIndex_ = std::clamp(2, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_FOUR)) activeMapLayerIndex_ = std::clamp(3, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_P)) { mapTool_ = MapTool::Paint; activeMapSection_ = MapSection::Tile; }
    if (IsKeyPressed(KEY_X)) { mapTool_ = MapTool::Erase; activeMapSection_ = MapSection::Tile; }
    if (IsKeyPressed(KEY_B)) { mapTool_ = MapTool::Blocked; activeMapSection_ = MapSection::Region; }
    if (IsKeyPressed(KEY_V)) { mapTool_ = MapTool::Water; activeMapSection_ = MapSection::Region; }
    if (IsKeyPressed(KEY_G)) { mapTool_ = MapTool::Spawn; activeMapSection_ = MapSection::Region; }
    if (IsKeyPressed(KEY_O)) { mapTool_ = MapTool::Object; activeMapSection_ = MapSection::Object; }
    if (IsKeyPressed(KEY_NINE)) objectPlacementKind_ = "npc";
    if (IsKeyPressed(KEY_ZERO)) objectPlacementKind_ = "portal";
    if (IsKeyPressed(KEY_EIGHT)) objectPlacementKind_ = "trigger";
    if (IsKeyPressed(KEY_SEVEN)) objectPlacementKind_ = "prop";
    if (IsKeyPressed(KEY_SIX)) objectPlacementKind_ = "region";
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) DeleteSelectedObject();
    if (IsKeyPressed(KEY_R)) {
        mapZoom_ = 1.5f;
        mapPan_ = Vector2 {0.0f, 0.0f};
    }
    if (IsKeyDown(KEY_A) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL)) mapPan_.x += panStep;
    if (IsKeyDown(KEY_D) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL)) mapPan_.x -= panStep;
    if (IsKeyDown(KEY_W)) mapPan_.y += panStep;
    if (IsKeyDown(KEY_S) && !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) mapPan_.y -= panStep;

    if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), mapLeftRepeatTimer_, dt)) mapPan_.x += panStep;
    if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), mapRightRepeatTimer_, dt)) mapPan_.x -= panStep;
    if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), mapUpRepeatTimer_, dt)) mapPan_.y += panStep;
    if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), mapDownRepeatTimer_, dt)) mapPan_.y -= panStep;
}

void AtlasEditor::HandleMapMouseInput(const Rectangle& canvasBounds) {
    if (mode_ != EditorMode::Map) {
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(GetMousePosition(), canvasBounds)) {
        mapZoom_ = ClampZoom(mapZoom_ + wheel * 0.15f);
    }

    const bool panDragHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ||
        (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    if (CheckCollisionPointRec(GetMousePosition(), canvasBounds) && panDragHeld) {
        const Vector2 delta = GetMouseDelta();
        mapPan_.x += delta.x;
        mapPan_.y += delta.y;
        mapPanning_ = true;
        return;
    }
    if (mapPanning_ && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        mapPanning_ = false;
    }

    int tileX = 0;
    int tileY = 0;
    int px = 0;
    int py = 0;
    if (!ScreenToMapTile(GetMousePosition(), canvasBounds, tileX, tileY)) {
        return;
    }
    ScreenToMapPixel(GetMousePosition(), canvasBounds, px, py);

    const Rectangle sideBounds {static_cast<float>(GetScreenWidth()) - 336.0f, 76.0f, 318.0f, static_cast<float>(GetScreenHeight()) - 94.0f};
    if (CheckCollisionPointRec(GetMousePosition(), sideBounds)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const float left = sideBounds.x + 18.0f;
            const float width = sideBounds.width - 36.0f;
            const float tabWidth = (width - 12.0f) / 3.0f;
            const float tabY = sideBounds.y + 148.0f;

            for (int i = 0; i < 3; ++i) {
                const Rectangle tab {left + (i * (tabWidth + 6.0f)), tabY, tabWidth, 28.0f};
                if (CheckCollisionPointRec(GetMousePosition(), tab)) {
                    activeMapSection_ = static_cast<MapSection>(i);
                    if (activeMapSection_ == MapSection::Tile && mapTool_ != MapTool::Paint && mapTool_ != MapTool::Erase) mapTool_ = MapTool::Paint;
                    if (activeMapSection_ == MapSection::Region && mapTool_ != MapTool::Blocked && mapTool_ != MapTool::Water && mapTool_ != MapTool::Spawn) mapTool_ = MapTool::Blocked;
                    if (activeMapSection_ == MapSection::Object) mapTool_ = MapTool::Object;
                    return;
                }
            }

            float y = tabY + 62.0f;
            const Rectangle toolA {left, y, width, 24.0f};
            const Rectangle toolB {left, y + 28.0f, width, 24.0f};
            const Rectangle toolC {left, y + 56.0f, width, 24.0f};
            if (activeMapSection_ == MapSection::Tile) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Paint; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolB)) { mapTool_ = MapTool::Erase; return; }
                y += 64.0f;
                y += 24.0f;
                for (int i = 0; i < static_cast<int>(mapLayers_.size()); ++i) {
                    const Rectangle row {left, y + (i * 26.0f), width, 22.0f};
                    if (CheckCollisionPointRec(GetMousePosition(), row)) {
                        activeMapLayerIndex_ = i;
                        return;
                    }
                }
            } else if (activeMapSection_ == MapSection::Region) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Blocked; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolB)) { mapTool_ = MapTool::Water; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolC)) { mapTool_ = MapTool::Spawn; return; }
            } else if (activeMapSection_ == MapSection::Object) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "prop"; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolB)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "npc"; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolC)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "portal"; return; }
                const Rectangle toolD {left, y + 84.0f, width, 24.0f};
                if (CheckCollisionPointRec(GetMousePosition(), toolD)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "trigger"; return; }
                const Rectangle toolE {left, y + 112.0f, width, 24.0f};
                if (CheckCollisionPointRec(GetMousePosition(), toolE)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "region"; return; }
                y += 144.0f;
                y += 28.0f;
                y += 24.0f;
                for (int i = 0; i < static_cast<int>(mapObjects_.size()) && i < 8; ++i) {
                    const Rectangle row {left, y + (i * 24.0f), width, 20.0f};
                    if (CheckCollisionPointRec(GetMousePosition(), row)) {
                        selectedMapObjectIndex_ = i;
                        return;
                    }
                }
            }
        }
        return;
    }

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        return;
    }

    if (mapTool_ == MapTool::Paint) PaintMapTile(tileX, tileY);
    else if (mapTool_ == MapTool::Erase) EraseMapTile(tileX, tileY);
    else if (mapTool_ == MapTool::Blocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ToggleMapRect(blockedAreas_, tileX, tileY);
    else if (mapTool_ == MapTool::Water && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ToggleMapRect(waterAreas_, tileX, tileY);
    else if (mapTool_ == MapTool::Spawn && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        mapSpawn_ = Vector2 {(tileX * mapTileSize_) + mapTileSize_ * 0.5f, (tileY * mapTileSize_) + mapTileSize_ * 0.5f};
    } else if (mapTool_ == MapTool::Object && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        PlaceOrSelectObject(px, py);
    }
}

void AtlasEditor::ClampSelectionToTexture() {
    AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        selectionX_ = std::max(0, selectionX_);
        selectionY_ = std::max(0, selectionY_);
        selectionCols_ = std::max(1, selectionCols_);
        selectionRows_ = std::max(1, selectionRows_);
        return;
    }
    const int maxCols = std::max(1, asset->texture.width / std::max(1, gridWidth_));
    const int maxRows = std::max(1, asset->texture.height / std::max(1, gridHeight_));
    selectionCols_ = std::clamp(selectionCols_, 1, maxCols);
    selectionRows_ = std::clamp(selectionRows_, 1, maxRows);
    selectionX_ = std::clamp(selectionX_, 0, std::max(0, maxCols - selectionCols_));
    selectionY_ = std::clamp(selectionY_, 0, std::max(0, maxRows - selectionRows_));
}

Rectangle AtlasEditor::SelectionRectPixels() const {
    return Rectangle {
        static_cast<float>(selectionX_ * gridWidth_),
        static_cast<float>(selectionY_ * gridHeight_),
        static_cast<float>(selectionCols_ * gridWidth_),
        static_cast<float>(selectionRows_ * gridHeight_)
    };
}

std::string AtlasEditor::BuildAtlasRef() const {
    const AtlasAsset* asset = CurrentAsset();
    if (!asset) {
        return {};
    }
    const Rectangle selection = SelectionRectPixels();
    std::ostringstream out;
    out << asset->domain << ":" << asset->filename
        << "@" << static_cast<int>(selection.x)
        << "@" << static_cast<int>(selection.y)
        << "@" << static_cast<int>(selection.width)
        << "@" << static_cast<int>(selection.height);
    return out.str();
}

void AtlasEditor::CopyCurrentRef() {
    const std::string ref = BuildAtlasRef();
    if (ref.empty()) {
        statusText_ = "No atlas selected.";
        return;
    }
    SetClipboardText(ref.c_str());
    statusText_ = "Copied atlas ref: " + ref;
}

void AtlasEditor::Update(float dt) {
    EnsureCurrentTextureLoaded();
    HandleKeyboardInput(dt);
    HandleMapKeyboardInput(dt);

    const Rectangle canvasBounds {18.0f, 76.0f, static_cast<float>(GetScreenWidth()) - 370.0f, static_cast<float>(GetScreenHeight()) - 94.0f};
    HandleMouseInput(canvasBounds);
    HandleMapMouseInput(canvasBounds);
}

void AtlasEditor::DrawTopBar() const {
    const Rectangle bar {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), 60.0f};
    DrawRectangleRec(bar, PanelColor());
    DrawUiText(uiFont_, "nganu.atlas", 18, 14, UiFont(28), RAYWHITE);
    DrawUiText(uiFont_, "F1 Atlas | F2 Map", 190, 20, UiFont(18), AccentSoft());
    DrawUiText(uiFont_, ModeLabel(mode_), 340, 20, UiFont(18), Fade(RAYWHITE, 0.8f));
}

void AtlasEditor::DrawAtlasCanvas(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.02f, 8, Color {252, 248, 241, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Fade(InkColor(), 0.14f));

    const AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        DrawUiText(uiFont_, "No atlas available for this domain.", bounds.x + 20.0f, bounds.y + 20.0f, UiFont(20), Fade(InkColor(), 0.65f));
        return;
    }

    const Rectangle drawRect {bounds.x + 18.0f + pan_.x, bounds.y + 18.0f + pan_.y,
        static_cast<float>(asset->texture.width) * zoom_, static_cast<float>(asset->texture.height) * zoom_};

    BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    DrawTexturePro(asset->texture, Rectangle {0.0f, 0.0f, static_cast<float>(asset->texture.width), static_cast<float>(asset->texture.height)},
        drawRect, Vector2 {}, 0.0f, WHITE);
    for (int x = 0; x <= asset->texture.width; x += std::max(1, gridWidth_)) {
        const float drawX = drawRect.x + static_cast<float>(x) * zoom_;
        DrawLineEx(Vector2 {drawX, drawRect.y}, Vector2 {drawX, drawRect.y + drawRect.height}, 1.0f, Fade(InkColor(), 0.16f));
    }
    for (int y = 0; y <= asset->texture.height; y += std::max(1, gridHeight_)) {
        const float drawY = drawRect.y + static_cast<float>(y) * zoom_;
        DrawLineEx(Vector2 {drawRect.x, drawY}, Vector2 {drawRect.x + drawRect.width, drawY}, 1.0f, Fade(InkColor(), 0.16f));
    }
    const Rectangle selection = SelectionRectPixels();
    const Rectangle highlight {drawRect.x + selection.x * zoom_, drawRect.y + selection.y * zoom_, selection.width * zoom_, selection.height * zoom_};
    DrawRectangleRec(highlight, Fade(AccentColor(), 0.22f));
    DrawRectangleLinesEx(highlight, 3.0f, AccentColor());
    EndScissorMode();
}

void AtlasEditor::DrawSidePanel(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.04f, 8, PanelColor());

    const AtlasAsset* asset = CurrentAsset();
    const std::string ref = BuildAtlasRef();
    const Rectangle previewBox {bounds.x + 18.0f, bounds.y + 206.0f, bounds.width - 36.0f, 170.0f};

    DrawUiText(uiFont_, "Domain", bounds.x + 18.0f, bounds.y + 18.0f, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawUiText(uiFont_, DomainLabel(activeDomain_), bounds.x + 18.0f, bounds.y + 42.0f, UiFont(30), AccentSoft());
    DrawUiText(uiFont_, "Atlas", bounds.x + 18.0f, bounds.y + 86.0f, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawUiText(uiFont_, asset ? asset->filename.c_str() : "(none)", bounds.x + 18.0f, bounds.y + 110.0f, UiFont(20), RAYWHITE);

    std::ostringstream gridText;
    gridText << gridWidth_ << " x " << gridHeight_;
    DrawUiText(uiFont_, "Grid", bounds.x + 18.0f, bounds.y + 146.0f, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawUiText(uiFont_, gridText.str(), bounds.x + 18.0f, bounds.y + 170.0f, UiFont(20), RAYWHITE);

    DrawRectangleRounded(previewBox, 0.07f, 8, Fade(WHITE, 0.06f));
    DrawUiText(uiFont_, "Selection", previewBox.x + 12.0f, previewBox.y + 10.0f, UiFont(18), Fade(RAYWHITE, 0.75f));
    if (asset && asset->loaded) {
        const Rectangle selection = SelectionRectPixels();
        const float scale = std::min((previewBox.width - 24.0f) / selection.width, (previewBox.height - 52.0f) / selection.height);
        const Rectangle dest {
            previewBox.x + (previewBox.width * 0.5f) - ((selection.width * scale) * 0.5f),
            previewBox.y + 34.0f + ((previewBox.height - 52.0f) * 0.5f) - ((selection.height * scale) * 0.5f),
            selection.width * scale, selection.height * scale};
        DrawTexturePro(asset->texture, selection, dest, Vector2 {}, 0.0f, WHITE);
        DrawRectangleLinesEx(dest, 2.0f, Fade(AccentSoft(), 0.9f));
    }

    DrawUiText(uiFont_, "Atlas Ref", bounds.x + 18.0f, bounds.y + 392.0f, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawRectangleRounded(Rectangle {bounds.x + 18.0f, bounds.y + 418.0f, bounds.width - 36.0f, 88.0f}, 0.05f, 8, Fade(WHITE, 0.06f));
    DrawUiText(uiFont_, ref, bounds.x + 18.0f, bounds.y + 430.0f, UiFont(18), AccentSoft());

    DrawUiText(uiFont_, "Controls", bounds.x + 18.0f, bounds.y + 526.0f, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawUiText(uiFont_, "Tab domain | Q/E atlas", bounds.x + 18.0f, bounds.y + 550.0f, UiFont(17), RAYWHITE);
    DrawUiText(uiFont_, "Wheel zoom | RMB/MMB drag pan", bounds.x + 18.0f, bounds.y + 572.0f, UiFont(17), RAYWHITE);
    DrawUiText(uiFont_, "WASD pan | Arrows move select", bounds.x + 18.0f, bounds.y + 594.0f, UiFont(17), RAYWHITE);
    DrawUiText(uiFont_, "Shift+Arrows resize | C copy", bounds.x + 18.0f, bounds.y + 616.0f, UiFont(17), RAYWHITE);
    DrawUiText(uiFont_, "Switch to Map mode with F2", bounds.x + 18.0f, bounds.y + 638.0f, UiFont(17), AccentSoft());
    DrawUiText(uiFont_, statusText_, bounds.x + 18.0f, bounds.y + bounds.height - 30.0f, UiFont(16), Fade(RAYWHITE, 0.62f));
}

void AtlasEditor::DrawMapCanvas(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.02f, 8, Color {252, 248, 241, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Fade(InkColor(), 0.14f));

    const Rectangle canvas = MapCanvasRect(bounds);
    BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    DrawRectangleRec(canvas, Color {221, 228, 216, 255});

    for (const MapLayerDef& layer : mapLayers_) {
        if (layer.kind == "image" && !layer.asset.empty()) {
            const AtlasRef ref = ParseAtlasRef(layer.asset);
            const AtlasAsset* atlasAsset = nullptr;
            const auto& pool = ref.domain == "character" ? characterAssets_ : mapAssets_;
            for (const AtlasAsset& candidate : pool) {
                if (candidate.filename == ref.file && candidate.loaded) {
                    atlasAsset = &candidate;
                    break;
                }
            }
            if (!atlasAsset) {
                continue;
            }
            for (int y = 0; y < mapHeight_; ++y) {
                for (int x = 0; x < mapWidth_; ++x) {
                    const Rectangle dest {
                        canvas.x + (x * mapTileSize_ * mapZoom_),
                        canvas.y + (y * mapTileSize_ * mapZoom_),
                        mapTileSize_ * mapZoom_,
                        mapTileSize_ * mapZoom_
                    };
                    DrawTexturePro(atlasAsset->texture, ref.source, dest, Vector2 {}, 0.0f, WHITE);
                }
            }
        }
        for (const MapStamp& stamp : mapStamps_) {
            if (stamp.layer != layer.name) continue;
            const AtlasRef ref = ParseAtlasRef(stamp.asset);
            const auto& pool = ref.domain == "character" ? characterAssets_ : mapAssets_;
            const AtlasAsset* atlasAsset = nullptr;
            for (const AtlasAsset& candidate : pool) {
                if (candidate.filename == ref.file && candidate.loaded) {
                    atlasAsset = &candidate;
                    break;
                }
            }
            if (!atlasAsset) continue;
            const Rectangle dest {
                canvas.x + (stamp.x * mapTileSize_ * mapZoom_),
                canvas.y + (stamp.y * mapTileSize_ * mapZoom_),
                mapTileSize_ * mapZoom_,
                mapTileSize_ * mapZoom_
            };
            DrawTexturePro(atlasAsset->texture, ref.source, dest, Vector2 {}, 0.0f, WHITE);
        }
    }

    for (int x = 0; x <= mapWidth_; ++x) {
        const float drawX = canvas.x + (x * mapTileSize_ * mapZoom_);
        DrawLineEx(Vector2 {drawX, canvas.y}, Vector2 {drawX, canvas.y + canvas.height}, 1.0f, MapGridColor());
    }
    for (int y = 0; y <= mapHeight_; ++y) {
        const float drawY = canvas.y + (y * mapTileSize_ * mapZoom_);
        DrawLineEx(Vector2 {canvas.x, drawY}, Vector2 {canvas.x + canvas.width, drawY}, 1.0f, MapGridColor());
    }

    for (const MapRect& rect : blockedAreas_) {
        Rectangle drawRect {canvas.x + rect.x * mapZoom_, canvas.y + rect.y * mapZoom_, rect.width * mapZoom_, rect.height * mapZoom_};
        DrawRectangleRec(drawRect, BlockedColor());
    }
    for (const MapRect& rect : waterAreas_) {
        Rectangle drawRect {canvas.x + rect.x * mapZoom_, canvas.y + rect.y * mapZoom_, rect.width * mapZoom_, rect.height * mapZoom_};
        DrawRectangleRec(drawRect, WaterColor());
    }

    for (int i = 0; i < static_cast<int>(mapObjects_.size()); ++i) {
        const MapObject& object = mapObjects_[static_cast<size_t>(i)];
        Rectangle drawRect {
            canvas.x + object.x * mapZoom_,
            canvas.y + object.y * mapZoom_,
            object.width * mapZoom_,
            object.height * mapZoom_
        };
        const std::string sprite = PropertyValue(object.properties, "sprite");
        bool drewSprite = false;
        const AtlasRef ref = ParseAtlasRef(sprite);
        if (ref.valid) {
            const auto& pool = ref.domain == "character" ? characterAssets_ : mapAssets_;
            for (const AtlasAsset& candidate : pool) {
                if (candidate.filename == ref.file && candidate.loaded) {
                    DrawTexturePro(candidate.texture, ref.source, drawRect, Vector2 {}, 0.0f, WHITE);
                    drewSprite = true;
                    break;
                }
            }
        }
        if (!drewSprite) {
            Color tint = AccentSoft();
            if (object.kind == "portal") tint = Color {155, 214, 255, 255};
            else if (object.kind == "trigger") tint = Color {247, 215, 107, 255};
            else if (object.kind == "npc") tint = Color {140, 235, 176, 255};
            else if (object.kind == "region") tint = Color {212, 157, 255, 255};
            DrawRectangleRec(drawRect, Fade(tint, 0.55f));
        }
        DrawRectangleLinesEx(drawRect, i == selectedMapObjectIndex_ ? 3.0f : 2.0f,
            i == selectedMapObjectIndex_ ? AccentColor() : Fade(InkColor(), 0.65f));
    }

    const Rectangle spawnRect {
        canvas.x + (mapSpawn_.x * mapZoom_) - 6.0f,
        canvas.y + (mapSpawn_.y * mapZoom_) - 6.0f,
        12.0f,
        12.0f
    };
    DrawRectangleRec(spawnRect, AccentColor());

    int hoverTileX = 0;
    int hoverTileY = 0;
    if (ScreenToMapTile(GetMousePosition(), bounds, hoverTileX, hoverTileY)) {
        int hoverCols = 1;
        int hoverRows = 1;
        if (mapTool_ == MapTool::Paint) {
            hoverCols = std::max(1, selectionCols_);
            hoverRows = std::max(1, selectionRows_);
        } else if (mapTool_ == MapTool::Object) {
            hoverCols = 1;
            hoverRows = 1;
            if (objectPlacementKind_ == "trigger") {
                hoverCols = 2;
                hoverRows = 2;
            } else if (objectPlacementKind_ == "region") {
                hoverCols = 8;
                hoverRows = 6;
            }
        }

        hoverCols = std::min(hoverCols, std::max(1, mapWidth_ - hoverTileX));
        hoverRows = std::min(hoverRows, std::max(1, mapHeight_ - hoverTileY));

        const Rectangle hover {
            canvas.x + (hoverTileX * mapTileSize_ * mapZoom_),
            canvas.y + (hoverTileY * mapTileSize_ * mapZoom_),
            hoverCols * mapTileSize_ * mapZoom_,
            hoverRows * mapTileSize_ * mapZoom_
        };
        DrawRectangleRec(hover, Fade(AccentColor(), 0.14f));
        DrawRectangleLinesEx(hover, 2.0f, Fade(AccentColor(), 0.9f));
    }

    EndScissorMode();
}

void AtlasEditor::DrawMapSidePanel(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.04f, 8, PanelColor());
    const MapLayerDef* activeLayer = ActiveMapLayer();
    float y = bounds.y + 18.0f;
    const float left = bounds.x + 18.0f;
    const float width = bounds.width - 36.0f;
    const float rowGap = 6.0f;

    DrawUiText(uiFont_, "Map", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
    y += 22.0f;
    DrawUiText(uiFont_, mapId_, left, y, UiFont(28), AccentSoft());
    y += 24.0f;
    DrawUiText(uiFont_, worldName_, left, y, UiFont(18), RAYWHITE);
    y += 28.0f;

    std::ostringstream sizeText;
    sizeText << mapWidth_ << " x " << mapHeight_ << "  tile " << mapTileSize_;
    DrawUiText(uiFont_, sizeText.str(), left, y, UiFont(18), RAYWHITE);
    y += 32.0f;

    DrawUiText(uiFont_, "Section", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
    y += 24.0f;
    const float tabWidth = (width - 12.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        Rectangle tab {left + (i * (tabWidth + 6.0f)), y, tabWidth, 28.0f};
        DrawRectangleRounded(tab, 0.18f, 6, i == static_cast<int>(activeMapSection_) ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.06f));
        DrawUiText(uiFont_, MapSectionLabel(static_cast<MapSection>(i)), tab.x + 6.0f, tab.y + 7.0f, UiFont(15), RAYWHITE);
    }
    y += 40.0f;

    DrawUiText(uiFont_, "Tool", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
    y += 22.0f;
    if (activeMapSection_ == MapSection::Tile) {
        Rectangle rowA {left, y, width, 24.0f};
        Rectangle rowB {left, y + 28.0f, width, 24.0f};
        DrawRectangleRounded(rowA, 0.15f, 6, mapTool_ == MapTool::Paint ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowB, 0.15f, 6, mapTool_ == MapTool::Erase ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawUiText(uiFont_, "Paint", rowA.x + 8.0f, rowA.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Erase", rowB.x + 8.0f, rowB.y + 5.0f, UiFont(16), RAYWHITE);
        y += 64.0f;
    } else if (activeMapSection_ == MapSection::Region) {
        Rectangle rowA {left, y, width, 24.0f};
        Rectangle rowB {left, y + 28.0f, width, 24.0f};
        Rectangle rowC {left, y + 56.0f, width, 24.0f};
        DrawRectangleRounded(rowA, 0.15f, 6, mapTool_ == MapTool::Blocked ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowB, 0.15f, 6, mapTool_ == MapTool::Water ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowC, 0.15f, 6, mapTool_ == MapTool::Spawn ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawUiText(uiFont_, "Blocked", rowA.x + 8.0f, rowA.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Water", rowB.x + 8.0f, rowB.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Spawn", rowC.x + 8.0f, rowC.y + 5.0f, UiFont(16), RAYWHITE);
        y += 88.0f;
    } else {
        Rectangle rowA {left, y, width, 24.0f};
        Rectangle rowB {left, y + 28.0f, width, 24.0f};
        Rectangle rowC {left, y + 56.0f, width, 24.0f};
        Rectangle rowD {left, y + 84.0f, width, 24.0f};
        Rectangle rowE {left, y + 112.0f, width, 24.0f};
        DrawRectangleRounded(rowA, 0.15f, 6, objectPlacementKind_ == "prop" ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowB, 0.15f, 6, objectPlacementKind_ == "npc" ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowC, 0.15f, 6, objectPlacementKind_ == "portal" ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowD, 0.15f, 6, objectPlacementKind_ == "trigger" ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawRectangleRounded(rowE, 0.15f, 6, objectPlacementKind_ == "region" ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
        DrawUiText(uiFont_, "Prop", rowA.x + 8.0f, rowA.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "NPC", rowB.x + 8.0f, rowB.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Portal", rowC.x + 8.0f, rowC.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Trigger", rowD.x + 8.0f, rowD.y + 5.0f, UiFont(16), RAYWHITE);
        DrawUiText(uiFont_, "Region", rowE.x + 8.0f, rowE.y + 5.0f, UiFont(16), RAYWHITE);
        y += 144.0f;
    }

    if (activeMapSection_ == MapSection::Tile) {
        DrawUiText(uiFont_, "Layers", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 24.0f;
        for (int i = 0; i < static_cast<int>(mapLayers_.size()) && y < bounds.y + bounds.height - 220.0f; ++i) {
            Rectangle row {left, y, width, 22.0f};
            DrawRectangleRounded(row, 0.15f, 6, i == activeMapLayerIndex_ ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
            DrawUiText(uiFont_, mapLayers_[static_cast<size_t>(i)].name, row.x + 8.0f, row.y + 5.0f, UiFont(16), RAYWHITE);
            y += 26.0f;
        }
        y += rowGap;
        DrawUiText(uiFont_, "Brush", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 24.0f;
        DrawRectangleRounded(Rectangle {left, y, width, 60.0f}, 0.05f, 8, Fade(WHITE, 0.06f));
        DrawUiText(uiFont_, CurrentBrushRef().empty() ? "(pick in Atlas mode)" : CurrentBrushRef(), left, y + 12.0f, UiFont(18), AccentSoft());
        y += 48.0f;
        DrawUiText(uiFont_, "Paint memakai atlas brush aktif.", left, y + 4.0f, UiFont(14), Fade(RAYWHITE, 0.72f));
        y += 24.0f;
    } else if (activeMapSection_ == MapSection::Region) {
        DrawUiText(uiFont_, "Region Tools", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 24.0f;
        const std::string blockedLabel = "Blocked cells: " + std::to_string(blockedAreas_.size());
        DrawUiText(uiFont_, blockedLabel, left, y, UiFont(16), RAYWHITE);
        y += 20.0f;
        const std::string waterLabel = "Water cells: " + std::to_string(waterAreas_.size());
        DrawUiText(uiFont_, waterLabel, left, y, UiFont(16), RAYWHITE);
        y += 20.0f;
        const std::string spawnLabel = "Spawn: " + std::to_string(static_cast<int>(mapSpawn_.x)) + "," + std::to_string(static_cast<int>(mapSpawn_.y));
        DrawUiText(uiFont_, spawnLabel, left, y, UiFont(16), RAYWHITE);
        y += 26.0f;
        DrawUiText(uiFont_, "Use B/V/G and click on map.", left, y, UiFont(15), AccentSoft());
        y += 24.0f;
    } else if (activeMapSection_ == MapSection::Object) {
        DrawUiText(uiFont_, "Object Kind", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 22.0f;
        DrawUiText(uiFont_, objectPlacementKind_, left, y, UiFont(18), AccentSoft());
        y += 26.0f;
        DrawUiText(uiFont_, "Brush", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 22.0f;
        DrawUiText(uiFont_, CurrentBrushRef().empty() ? "(pick in Atlas mode)" : CurrentBrushRef(), left, y, UiFont(16), AccentSoft());
        y += 28.0f;
        DrawUiText(uiFont_, "Objects", left, y, UiFont(18), Fade(RAYWHITE, 0.72f));
        y += 24.0f;
        for (int i = 0; i < static_cast<int>(mapObjects_.size()) && y < bounds.y + bounds.height - 220.0f; ++i) {
            Rectangle row {left, y, width, 20.0f};
            DrawRectangleRounded(row, 0.15f, 6, i == selectedMapObjectIndex_ ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.05f));
            const std::string label = mapObjects_[static_cast<size_t>(i)].kind + ":" + mapObjects_[static_cast<size_t>(i)].id;
            DrawUiText(uiFont_, label, row.x + 8.0f, row.y + 4.0f, UiFont(15), RAYWHITE);
            y += 24.0f;
        }
    }

    const float footerTop = bounds.y + bounds.height - 132.0f;
    DrawUiText(uiFont_, "Map File", left, footerTop, UiFont(18), Fade(RAYWHITE, 0.72f));
    DrawUiText(uiFont_, currentMapFile_.empty() ? "(unsaved)" : currentMapFile_.filename().string(), left, footerTop + 24.0f, UiFont(16), RAYWHITE);
    DrawUiText(uiFont_, "Tab section | Ctrl+S save | Ctrl+N new", left, footerTop + 48.0f, UiFont(14), RAYWHITE);
    DrawUiText(uiFont_, "PgUp/PgDn map | F1 atlas | F2 map", left, footerTop + 66.0f, UiFont(14), RAYWHITE);
    DrawUiText(uiFont_, statusText_, left, bounds.y + bounds.height - 24.0f, UiFont(16), Fade(RAYWHITE, 0.62f));
}

void AtlasEditor::Draw() const {
    ClearBackground(BackgroundColor());
    DrawTopBar();

    const Rectangle canvasBounds {18.0f, 76.0f, static_cast<float>(GetScreenWidth()) - 370.0f, static_cast<float>(GetScreenHeight()) - 94.0f};
    const Rectangle sideBounds {static_cast<float>(GetScreenWidth()) - 336.0f, 76.0f, 318.0f, static_cast<float>(GetScreenHeight()) - 94.0f};

    if (mode_ == EditorMode::Atlas) {
        DrawAtlasCanvas(canvasBounds);
        DrawSidePanel(sideBounds);
        return;
    }

    DrawMapCanvas(canvasBounds);
    DrawMapSidePanel(sideBounds);
}

const char* AtlasEditor::ModeLabel(EditorMode mode) {
    return mode == EditorMode::Atlas ? "Atlas Picker" : "Map Editor";
}

const char* AtlasEditor::DomainLabel(Domain domain) {
    return domain == Domain::Map ? "map" : "character";
}

const char* AtlasEditor::MapToolLabel(MapTool tool) {
    switch (tool) {
        case MapTool::Paint: return "paint";
        case MapTool::Erase: return "erase";
        case MapTool::Blocked: return "blocked";
        case MapTool::Water: return "water";
        case MapTool::Spawn: return "spawn";
        case MapTool::Object: return "object";
    }
    return "paint";
}

const char* AtlasEditor::MapSectionLabel(MapSection section) {
    switch (section) {
        case MapSection::Tile: return "Tile";
        case MapSection::Region: return "Region";
        case MapSection::Object: return "Object";
    }
    return "Tile";
}
