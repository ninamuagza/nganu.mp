#include "EditorApp.h"

#include "EditorUi.h"
#include "shared/MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace {

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

namespace Ui = EditorUi;

EditorApp::EditorApp() {
    projectRoot_ = std::filesystem::path(NGANU_REPO_ROOT).lexically_normal();
    mapAssetRoot_ = projectRoot_ / "nganu.mp" / "assets" / "map_images";
    characterAssetRoot_ = projectRoot_ / "nganu.mp" / "assets" / "characters";
    mapsRoot_ = projectRoot_ / "nganu.mp" / "assets" / "maps";
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

EditorApp::~EditorApp() {
    if (ownsUiFont_ && uiFont_.texture.id > 0) {
        UnloadFont(uiFont_);
    }
    UnloadAssets();
}

void EditorApp::LoadUiFont() {
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

void EditorApp::ScanAssets() {
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

void EditorApp::ScanMaps() {
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

std::vector<EditorApp::AtlasAsset>& EditorApp::ActiveAssets() {
    return activeDomain_ == Domain::Map ? mapAssets_ : characterAssets_;
}

const std::vector<EditorApp::AtlasAsset>& EditorApp::ActiveAssets() const {
    return activeDomain_ == Domain::Map ? mapAssets_ : characterAssets_;
}

EditorApp::AtlasAsset* EditorApp::CurrentAsset() {
    auto& assets = ActiveAssets();
    if (assets.empty() || activeIndex_ < 0 || activeIndex_ >= static_cast<int>(assets.size())) {
        return nullptr;
    }
    return &assets[activeIndex_];
}

const EditorApp::AtlasAsset* EditorApp::CurrentAsset() const {
    const auto& assets = ActiveAssets();
    if (assets.empty() || activeIndex_ < 0 || activeIndex_ >= static_cast<int>(assets.size())) {
        return nullptr;
    }
    return &assets[activeIndex_];
}

void EditorApp::EnsureCurrentTextureLoaded() {
    AtlasAsset* asset = CurrentAsset();
    if (!asset || asset->loaded) {
        return;
    }
    asset->texture = LoadTexture(asset->path.string().c_str());
    asset->loaded = asset->texture.id > 0;
    ClampSelectionToTexture();
}

void EditorApp::UnloadAssets() {
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

void EditorApp::ChangeDomain() {
    activeDomain_ = activeDomain_ == Domain::Map ? Domain::Character : Domain::Map;
    if (ActiveAssets().empty()) {
        activeDomain_ = activeDomain_ == Domain::Map ? Domain::Character : Domain::Map;
    }
    activeIndex_ = std::clamp(activeIndex_, 0, std::max(0, static_cast<int>(ActiveAssets().size()) - 1));
    ClampSelectionToTexture();
}

void EditorApp::StepAsset(int delta) {
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

void EditorApp::NewMapDocument() {
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

bool EditorApp::LoadMapFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        statusText_ = "Failed to open map: " + path.filename().string();
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const Nganu::MapFormat::ParseResult parsed = Nganu::MapFormat::ParseDocument(buffer.str());
    if (!parsed.ok) {
        statusText_ = parsed.line > 0
            ? "Map parse issue at line " + std::to_string(parsed.line) + ": " + parsed.error
            : "Map parse issue: " + parsed.error;
        return false;
    }
    const Nganu::MapFormat::Document& document = parsed.document;

    NewMapDocument();
    currentMapFile_ = path;
    mapId_ = document.mapId;
    worldName_ = document.worldName;
    mapTileSize_ = document.tileSize;
    mapWidth_ = document.width;
    mapHeight_ = document.height;
    mapSpawn_ = Vector2 {document.spawn.x, document.spawn.y};
    mapProperties_.clear();
    mapLayers_.clear();

    for (const auto& [key, value] : document.properties) {
        mapProperties_.push_back({key, value});
    }
    for (const Nganu::MapFormat::Layer& source : document.layers) {
        mapLayers_.push_back(MapLayerDef {source.name, source.kind, source.asset, source.tint, source.parallax});
    }
    for (const Nganu::MapFormat::Stamp& source : document.stamps) {
        mapStamps_.push_back(MapStamp {source.layer, source.x, source.y, source.asset});
    }
    for (const Nganu::MapFormat::Object& source : document.objects) {
        MapObject object;
        object.kind = source.kind;
        object.id = source.id;
        object.x = static_cast<int>(std::round(source.bounds.x));
        object.y = static_cast<int>(std::round(source.bounds.y));
        object.width = static_cast<int>(std::round(source.bounds.width));
        object.height = static_cast<int>(std::round(source.bounds.height));
        for (const auto& [key, value] : source.properties) {
            object.properties.push_back({key, value});
        }
        mapObjects_.push_back(std::move(object));
    }
    for (const Nganu::MapFormat::Rect& source : document.blockedAreas) {
        blockedAreas_.push_back(MapRect {
            static_cast<int>(std::round(source.x)),
            static_cast<int>(std::round(source.y)),
            static_cast<int>(std::round(source.width)),
            static_cast<int>(std::round(source.height))
        });
    }
    for (const Nganu::MapFormat::Rect& source : document.waterAreas) {
        waterAreas_.push_back(MapRect {
            static_cast<int>(std::round(source.x)),
            static_cast<int>(std::round(source.y)),
            static_cast<int>(std::round(source.width)),
            static_cast<int>(std::round(source.height))
        });
    }

    if (mapLayers_.empty()) {
        mapLayers_.push_back({"ground", "image", "map:terrain_atlas.png@0@0@32@32", "#FFFFFFFF", 1.0f});
    }
    activeMapLayerIndex_ = std::clamp(activeMapLayerIndex_, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    statusText_ = "Loaded map " + path.filename().string();
    return true;
}

bool EditorApp::SaveCurrentMap() const {
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

        out << "map_format=2\n";
        out << "map_id=" << mapId_ << "\n";
        out << "world_name=" << worldName_ << "\n";
        out << "tile_size=" << mapTileSize_ << "\n";
        out << "size=" << mapWidth_ << "," << mapHeight_ << "\n";
        out << "spawn=" << static_cast<int>(mapSpawn_.x) << "," << static_cast<int>(mapSpawn_.y) << "\n";
        for (const MapProperty& property : mapProperties_) {
            out << "property=" << Nganu::MapFormat::EscapeValue(property.key) << "," << Nganu::MapFormat::EscapeValue(property.value) << "\n";
        }
        for (const MapLayerDef& layer : mapLayers_) {
            out << "layer=" << Nganu::MapFormat::EscapeValue(layer.name) << ",tilemap";
            if (!layer.asset.empty()) {
                out << ",asset:" << Nganu::MapFormat::EscapeValue(layer.asset);
            }
            out << ",tint:" << layer.tint << ",parallax:" << layer.parallax << "\n";
        }
        std::map<std::pair<std::string, std::string>, std::set<std::pair<int, int>>> stampGroups;
        for (const MapStamp& stamp : mapStamps_) {
            stampGroups[{stamp.layer, stamp.asset}].insert({stamp.x, stamp.y});
        }
        for (auto& [group, points] : stampGroups) {
            const std::string& layer = group.first;
            const std::string& asset = group.second;
            while (!points.empty()) {
                const auto start = *points.begin();
                int runX = start.first;
                while (points.find({runX + 1, start.second}) != points.end()) {
                    ++runX;
                }
                int runY = start.second;
                while (points.find({start.first, runY + 1}) != points.end()) {
                    ++runY;
                }
                const int horizontalLen = runX - start.first + 1;
                const int verticalLen = runY - start.second + 1;
                if (horizontalLen >= 3 || verticalLen >= 3) {
                    if (horizontalLen >= verticalLen) {
                        out << "line=" << Nganu::MapFormat::EscapeValue(layer) << ","
                            << start.first << "," << start.second << ","
                            << runX << "," << start.second << ","
                            << Nganu::MapFormat::EscapeValue(asset) << "\n";
                        for (int x = start.first; x <= runX; ++x) {
                            points.erase({x, start.second});
                        }
                    } else {
                        out << "line=" << Nganu::MapFormat::EscapeValue(layer) << ","
                            << start.first << "," << start.second << ","
                            << start.first << "," << runY << ","
                            << Nganu::MapFormat::EscapeValue(asset) << "\n";
                        for (int y = start.second; y <= runY; ++y) {
                            points.erase({start.first, y});
                        }
                    }
                    continue;
                }

                std::vector<std::pair<int, int>> batch;
                batch.reserve(32);
                while (!points.empty() && batch.size() < 32) {
                    batch.push_back(*points.begin());
                    points.erase(points.begin());
                }
                if (batch.size() == 1) {
                    out << "tile=" << Nganu::MapFormat::EscapeValue(layer) << ","
                        << batch[0].first << "," << batch[0].second << ","
                        << Nganu::MapFormat::EscapeValue(asset) << "\n";
                } else {
                    out << "tiles=" << Nganu::MapFormat::EscapeValue(layer) << ","
                        << Nganu::MapFormat::EscapeValue(asset);
                    for (const auto& [x, y] : batch) {
                        out << "," << x << ":" << y;
                    }
                    out << "\n";
                }
            }
        }
        for (const MapObject& object : mapObjects_) {
            out << "entity=" << Nganu::MapFormat::EscapeValue(object.kind) << "," << Nganu::MapFormat::EscapeValue(object.id) << ","
                << object.x << "," << object.y << "," << object.width << "," << object.height << "\n";
            for (const MapProperty& property : object.properties) {
                out << "prop=" << Nganu::MapFormat::EscapeValue(object.id) << ","
                    << Nganu::MapFormat::EscapeValue(property.key) << ","
                    << Nganu::MapFormat::EscapeValue(property.value) << "\n";
            }
        }
        for (const MapRect& rect : blockedAreas_) {
            out << "area=block," << rect.x << "," << rect.y << "," << rect.width << "," << rect.height << "\n";
        }
        for (const MapRect& rect : waterAreas_) {
            out << "area=water," << rect.x << "," << rect.y << "," << rect.width << "," << rect.height << "\n";
        }
        return out.good();
    } catch (...) {
        return false;
    }
}

void EditorApp::StepMapFile(int delta) {
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

EditorApp::MapLayerDef* EditorApp::ActiveMapLayer() {
    if (mapLayers_.empty() || activeMapLayerIndex_ < 0 || activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        return nullptr;
    }
    return &mapLayers_[activeMapLayerIndex_];
}

const EditorApp::MapLayerDef* EditorApp::ActiveMapLayer() const {
    if (mapLayers_.empty() || activeMapLayerIndex_ < 0 || activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        return nullptr;
    }
    return &mapLayers_[activeMapLayerIndex_];
}

std::string EditorApp::CurrentBrushRef() const {
    const AtlasAsset* asset = CurrentAsset();
    if (!asset) {
        return {};
    }
    if (asset->domain != "map") {
        return {};
    }
    return BuildAtlasRef();
}

std::vector<EditorApp::BrushStamp> EditorApp::CurrentBrushPattern() const {
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

void EditorApp::HandleKeyboardInput(float dt) {
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

void EditorApp::HandleMouseInput(const Rectangle& canvasBounds) {
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

Rectangle EditorApp::MapCanvasRect(const Rectangle& bounds) const {
    return Rectangle {
        bounds.x + mapPan_.x,
        bounds.y + mapPan_.y,
        static_cast<float>(mapWidth_ * mapTileSize_) * mapZoom_,
        static_cast<float>(mapHeight_ * mapTileSize_) * mapZoom_
    };
}

bool EditorApp::ScreenToMapTile(Vector2 screen, const Rectangle& bounds, int& outTileX, int& outTileY) const {
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

bool EditorApp::ScreenToMapPixel(Vector2 screen, const Rectangle& bounds, int& outX, int& outY) const {
    const Rectangle canvas = MapCanvasRect(bounds);
    if (!CheckCollisionPointRec(screen, canvas)) {
        return false;
    }
    outX = std::clamp(static_cast<int>((screen.x - canvas.x) / mapZoom_), 0, std::max(0, mapWidth_ * mapTileSize_ - 1));
    outY = std::clamp(static_cast<int>((screen.y - canvas.y) / mapZoom_), 0, std::max(0, mapHeight_ * mapTileSize_ - 1));
    return true;
}

void EditorApp::PaintMapTile(int tileX, int tileY) {
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

void EditorApp::EraseMapTile(int tileX, int tileY) {
    MapLayerDef* layer = ActiveMapLayer();
    if (!layer) {
        return;
    }
    mapStamps_.erase(std::remove_if(mapStamps_.begin(), mapStamps_.end(), [&](const MapStamp& stamp) {
        return stamp.layer == layer->name && stamp.x == tileX && stamp.y == tileY;
    }), mapStamps_.end());
}

bool EditorApp::HasMapRect(const std::vector<MapRect>& areas, int tileX, int tileY) const {
    const int px = tileX * mapTileSize_;
    const int py = tileY * mapTileSize_;
    for (const MapRect& rect : areas) {
        if (px >= rect.x && py >= rect.y && px < rect.x + rect.width && py < rect.y + rect.height) {
            return true;
        }
    }
    return false;
}

void EditorApp::ToggleMapRect(std::vector<MapRect>& areas, int tileX, int tileY) {
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

int EditorApp::ObjectIndexAtPixel(int px, int py) const {
    for (int i = static_cast<int>(mapObjects_.size()) - 1; i >= 0; --i) {
        const MapObject& object = mapObjects_[static_cast<size_t>(i)];
        if (px >= object.x && py >= object.y && px < object.x + object.width && py < object.y + object.height) {
            return i;
        }
    }
    return -1;
}

void EditorApp::PlaceOrSelectObject(int px, int py) {
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

void EditorApp::DeleteSelectedObject() {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        return;
    }
    const std::string removedId = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)].id;
    mapObjects_.erase(mapObjects_.begin() + selectedMapObjectIndex_);
    selectedMapObjectIndex_ = -1;
    statusText_ = "Deleted object " + removedId;
}

void EditorApp::HandleMapKeyboardInput(float dt) {
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

void EditorApp::HandleMapMouseInput(const Rectangle& canvasBounds) {
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

void EditorApp::ClampSelectionToTexture() {
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

Rectangle EditorApp::SelectionRectPixels() const {
    return Rectangle {
        static_cast<float>(selectionX_ * gridWidth_),
        static_cast<float>(selectionY_ * gridHeight_),
        static_cast<float>(selectionCols_ * gridWidth_),
        static_cast<float>(selectionRows_ * gridHeight_)
    };
}

std::string EditorApp::BuildAtlasRef() const {
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

void EditorApp::CopyCurrentRef() {
    const std::string ref = BuildAtlasRef();
    if (ref.empty()) {
        statusText_ = "No atlas selected.";
        return;
    }
    SetClipboardText(ref.c_str());
    statusText_ = "Copied atlas ref: " + ref;
}

void EditorApp::Update(float dt) {
    EnsureCurrentTextureLoaded();
    HandleKeyboardInput(dt);
    HandleMapKeyboardInput(dt);

    const Ui::Layout layout = Ui::BuildLayout(GetScreenWidth(), GetScreenHeight());
    HandleMouseInput(layout.canvas);
    HandleMapMouseInput(layout.canvas);
}

void EditorApp::DrawAtlasCanvas(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.02f, 8, Color {252, 248, 241, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Fade(Ui::InkColor(), 0.14f));

    const AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        Ui::DrawTextClipped(uiFont_, "No atlas available for this domain.",
            Rectangle {bounds.x + 20.0f, bounds.y + 20.0f, bounds.width - 40.0f, 28.0f},
            Ui::FontSize(20), Fade(Ui::InkColor(), 0.65f));
        return;
    }

    const Rectangle drawRect {bounds.x + 18.0f + pan_.x, bounds.y + 18.0f + pan_.y,
        static_cast<float>(asset->texture.width) * zoom_, static_cast<float>(asset->texture.height) * zoom_};

    BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    DrawTexturePro(asset->texture, Rectangle {0.0f, 0.0f, static_cast<float>(asset->texture.width), static_cast<float>(asset->texture.height)},
        drawRect, Vector2 {}, 0.0f, WHITE);
    for (int x = 0; x <= asset->texture.width; x += std::max(1, gridWidth_)) {
        const float drawX = drawRect.x + static_cast<float>(x) * zoom_;
        DrawLineEx(Vector2 {drawX, drawRect.y}, Vector2 {drawX, drawRect.y + drawRect.height}, 1.0f, Fade(Ui::InkColor(), 0.16f));
    }
    for (int y = 0; y <= asset->texture.height; y += std::max(1, gridHeight_)) {
        const float drawY = drawRect.y + static_cast<float>(y) * zoom_;
        DrawLineEx(Vector2 {drawRect.x, drawY}, Vector2 {drawRect.x + drawRect.width, drawY}, 1.0f, Fade(Ui::InkColor(), 0.16f));
    }
    const Rectangle selection = SelectionRectPixels();
    const Rectangle highlight {drawRect.x + selection.x * zoom_, drawRect.y + selection.y * zoom_, selection.width * zoom_, selection.height * zoom_};
    DrawRectangleRec(highlight, Fade(Ui::AccentColor(), 0.22f));
    DrawRectangleLinesEx(highlight, 3.0f, Ui::AccentColor());
    EndScissorMode();
}

void EditorApp::DrawSidePanel(Rectangle bounds) const {
    const AtlasAsset* asset = CurrentAsset();
    std::ostringstream gridText;
    gridText << gridWidth_ << " x " << gridHeight_;

    Ui::AtlasPanelState state;
    state.domain = DomainLabel(activeDomain_);
    state.atlasName = asset ? asset->filename : "";
    state.grid = gridText.str();
    state.ref = BuildAtlasRef();
    state.status = statusText_;
    state.hasSelection = asset && asset->loaded;
    if (state.hasSelection) {
        state.texture = asset->texture;
        state.source = SelectionRectPixels();
    }
    Ui::DrawAtlasPanel(uiFont_, bounds, state);
}

void EditorApp::DrawMapCanvas(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.02f, 8, Color {252, 248, 241, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Fade(Ui::InkColor(), 0.14f));

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
        DrawLineEx(Vector2 {drawX, canvas.y}, Vector2 {drawX, canvas.y + canvas.height}, 1.0f, Ui::MapGridColor());
    }
    for (int y = 0; y <= mapHeight_; ++y) {
        const float drawY = canvas.y + (y * mapTileSize_ * mapZoom_);
        DrawLineEx(Vector2 {canvas.x, drawY}, Vector2 {canvas.x + canvas.width, drawY}, 1.0f, Ui::MapGridColor());
    }

    for (const MapRect& rect : blockedAreas_) {
        Rectangle drawRect {canvas.x + rect.x * mapZoom_, canvas.y + rect.y * mapZoom_, rect.width * mapZoom_, rect.height * mapZoom_};
        DrawRectangleRec(drawRect, Ui::BlockedColor());
    }
    for (const MapRect& rect : waterAreas_) {
        Rectangle drawRect {canvas.x + rect.x * mapZoom_, canvas.y + rect.y * mapZoom_, rect.width * mapZoom_, rect.height * mapZoom_};
        DrawRectangleRec(drawRect, Ui::WaterColor());
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
            Color tint = Ui::AccentSoft();
            if (object.kind == "portal") tint = Color {155, 214, 255, 255};
            else if (object.kind == "trigger") tint = Color {247, 215, 107, 255};
            else if (object.kind == "npc") tint = Color {140, 235, 176, 255};
            else if (object.kind == "region") tint = Color {212, 157, 255, 255};
            DrawRectangleRec(drawRect, Fade(tint, 0.55f));
        }
        DrawRectangleLinesEx(drawRect, i == selectedMapObjectIndex_ ? 3.0f : 2.0f,
            i == selectedMapObjectIndex_ ? Ui::AccentColor() : Fade(Ui::InkColor(), 0.65f));
    }

    const Rectangle spawnRect {
        canvas.x + (mapSpawn_.x * mapZoom_) - 6.0f,
        canvas.y + (mapSpawn_.y * mapZoom_) - 6.0f,
        12.0f,
        12.0f
    };
    DrawRectangleRec(spawnRect, Ui::AccentColor());

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
        DrawRectangleRec(hover, Fade(Ui::AccentColor(), 0.14f));
        DrawRectangleLinesEx(hover, 2.0f, Fade(Ui::AccentColor(), 0.9f));
    }

    EndScissorMode();
}

void EditorApp::DrawMapSidePanel(Rectangle bounds) const {
    Ui::MapPanelState state;
    state.mapId = mapId_;
    state.worldName = worldName_;

    std::ostringstream sizeText;
    sizeText << mapWidth_ << " x " << mapHeight_ << "  tile " << mapTileSize_;
    state.size = sizeText.str();
    state.section = static_cast<Ui::MapSection>(activeMapSection_);
    state.tool = static_cast<Ui::MapTool>(mapTool_);
    state.activeLayer = activeMapLayerIndex_;
    state.brushRef = CurrentBrushRef();
    state.blockedCount = static_cast<int>(blockedAreas_.size());
    state.waterCount = static_cast<int>(waterAreas_.size());
    state.spawn = std::to_string(static_cast<int>(mapSpawn_.x)) + "," + std::to_string(static_cast<int>(mapSpawn_.y));
    state.objectKind = objectPlacementKind_;
    state.selectedObject = selectedMapObjectIndex_;
    state.mapFile = currentMapFile_.empty() ? "(unsaved)" : currentMapFile_.filename().string();
    state.status = statusText_;

    state.layers.reserve(mapLayers_.size());
    for (const MapLayerDef& layer : mapLayers_) {
        state.layers.push_back(layer.name);
    }

    state.objects.reserve(mapObjects_.size());
    for (const MapObject& object : mapObjects_) {
        state.objects.push_back(object.kind + ":" + object.id);
    }

    Ui::DrawMapPanel(uiFont_, bounds, state);
}

void EditorApp::Draw() const {
    ClearBackground(Ui::BackgroundColor());
    const Ui::Layout layout = Ui::BuildLayout(GetScreenWidth(), GetScreenHeight());
    Ui::DrawTopBar(uiFont_, layout, mode_ == EditorMode::Atlas ? Ui::Mode::Atlas : Ui::Mode::Map);

    if (mode_ == EditorMode::Atlas) {
        DrawAtlasCanvas(layout.canvas);
        DrawSidePanel(layout.sidePanel);
        return;
    }

    DrawMapCanvas(layout.canvas);
    DrawMapSidePanel(layout.sidePanel);
}

const char* EditorApp::DomainLabel(Domain domain) {
    return domain == Domain::Map ? "map" : "character";
}
