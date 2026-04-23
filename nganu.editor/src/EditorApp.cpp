#include "EditorApp.h"

#include "EditorUi.h"
#include "shared/MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <map>
#include <set>

namespace {

constexpr size_t kUndoHistoryLimit = 80;

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

template <typename PropertyCollection>
void RemovePropertyValue(PropertyCollection& properties, const std::string& key) {
    properties.erase(std::remove_if(properties.begin(), properties.end(), [&](const auto& property) {
        return property.key == key;
    }), properties.end());
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool CollisionBlocksMovement(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "block" ||
           lowered == "solid" ||
           lowered == "wall";
}

bool CollisionDisablesMovementBlock(const std::string& collision) {
    const std::string lowered = ToLower(collision);
    return lowered == "none" ||
           lowered == "pass" ||
           lowered == "passable" ||
           lowered == "false" ||
           lowered == "off";
}

bool ParseLocalRect(const std::string& value, Rectangle& out) {
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

std::string FormatRect(Rectangle rect) {
    std::ostringstream out;
    out << static_cast<int>(std::round(rect.x)) << "|"
        << static_cast<int>(std::round(rect.y)) << "|"
        << static_cast<int>(std::round(rect.width)) << "|"
        << static_cast<int>(std::round(rect.height));
    return out.str();
}

Rectangle TrunkColliderFor(Rectangle source) {
    const float width = std::max(4.0f, source.width * 0.34f);
    const float height = std::max(4.0f, source.height * 0.28f);
    return Rectangle {
        std::max(0.0f, (source.width - width) * 0.5f),
        std::max(0.0f, source.height - height),
        width,
        height
    };
}

Rectangle ClampLocalRect(Rectangle rect, float maxWidth, float maxHeight) {
    maxWidth = std::max(1.0f, maxWidth);
    maxHeight = std::max(1.0f, maxHeight);
    rect.width = std::clamp(rect.width, 1.0f, maxWidth);
    rect.height = std::clamp(rect.height, 1.0f, maxHeight);
    rect.x = std::clamp(rect.x, 0.0f, std::max(0.0f, maxWidth - rect.width));
    rect.y = std::clamp(rect.y, 0.0f, std::max(0.0f, maxHeight - rect.height));
    return rect;
}

template <typename LayerCollection>
std::string UniqueLayerName(const LayerCollection& layers) {
    for (int suffix = static_cast<int>(layers.size()) + 1; suffix < 1000; ++suffix) {
        const std::string name = suffix == 1 ? "base" : "layer_" + std::to_string(suffix);
        const bool exists = std::any_of(layers.begin(), layers.end(), [&](const auto& layer) {
            return layer.name == name;
        });
        if (!exists) {
            return name;
        }
    }
    return "layer_new";
}

struct ColliderInputRects {
    Rectangle x {};
    Rectangle y {};
    Rectangle width {};
    Rectangle height {};
};

float AtlasColliderInputRowY(Rectangle bounds) {
    Rectangle content {bounds.x + 18.0f, bounds.y + 18.0f, bounds.width - 36.0f, bounds.height - 36.0f};
    float y = content.y;
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(28) + 10);
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(18) + 10);
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(18) + 10);
    const float previewHeight = std::clamp(bounds.height * 0.23f, 120.0f, 170.0f);
    y = (y + 8.0f) + previewHeight + 18.0f;
    y += 22.0f;
    y += 82.0f;
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(18) + 10);
    y += 22.0f;
    return y;
}

float ObjectColliderInputRowY(Rectangle bounds, int selectedObject, bool hasSelectedCollider) {
    Rectangle content {bounds.x + 18.0f, bounds.y + 18.0f, bounds.width - 36.0f, bounds.height - 36.0f};
    float y = content.y;
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(27) + 10);
    y += static_cast<float>(EditorUi::FontSize(17) + 10);
    y += static_cast<float>(EditorUi::FontSize(17) + 10);
    y += 22.0f;
    y += 40.0f;
    y += 22.0f;
    y += 5.0f * 28.0f;
    y += 8.0f;
    y += 22.0f;
    y += static_cast<float>(EditorUi::FontSize(18) + 10);
    y += 48.0f;
    if (selectedObject >= 0) {
        y += static_cast<float>(EditorUi::FontSize(15) + 10);
        if (hasSelectedCollider) {
            y += static_cast<float>(EditorUi::FontSize(15) + 10);
        }
    }
    y += 22.0f;
    return y;
}

ColliderInputRects BuildColliderInputRects(Rectangle bounds, float rowY) {
    const float left = bounds.x + 18.0f;
    const float width = bounds.width - 36.0f;
    const float gap = 6.0f;
    const float fieldWidth = (width - gap * 3.0f) / 4.0f;
    return ColliderInputRects {
        Rectangle {left, rowY, fieldWidth, 24.0f},
        Rectangle {left + (fieldWidth + gap), rowY, fieldWidth, 24.0f},
        Rectangle {left + (fieldWidth + gap) * 2.0f, rowY, fieldWidth, 24.0f},
        Rectangle {left + (fieldWidth + gap) * 3.0f, rowY, fieldWidth, 24.0f}
    };
}

std::string FormatNumber(float value) {
    return std::to_string(static_cast<int>(std::round(value)));
}

void DrawColliderInputField(const Font& font, Rectangle bounds, const char* label, const std::string& value, bool active) {
    DrawRectangleRounded(bounds, 0.18f, 6, active ? Fade(EditorUi::AccentColor(), 0.30f) : Fade(WHITE, 0.08f));
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, active ? EditorUi::AccentSoft() : Fade(RAYWHITE, 0.22f));
    EditorUi::DrawTextClipped(font, std::string(label) + ":" + value,
        Rectangle {bounds.x + 6.0f, bounds.y + 5.0f, bounds.width - 12.0f, bounds.height - 8.0f},
        EditorUi::FontSize(14), RAYWHITE);
}

void DrawColliderHandles(Rectangle rect, Color color) {
    constexpr float handle = 6.0f;
    const Vector2 points[] {
        Vector2 {rect.x, rect.y},
        Vector2 {rect.x + rect.width * 0.5f, rect.y},
        Vector2 {rect.x + rect.width, rect.y},
        Vector2 {rect.x, rect.y + rect.height * 0.5f},
        Vector2 {rect.x + rect.width, rect.y + rect.height * 0.5f},
        Vector2 {rect.x, rect.y + rect.height},
        Vector2 {rect.x + rect.width * 0.5f, rect.y + rect.height},
        Vector2 {rect.x + rect.width, rect.y + rect.height}
    };
    for (const Vector2 point : points) {
        const Rectangle handleRect {point.x - handle * 0.5f, point.y - handle * 0.5f, handle, handle};
        DrawRectangleRec(handleRect, color);
        DrawRectangleLinesEx(handleRect, 1.0f, Fade(BLACK, 0.42f));
    }
}

int HitColliderDragModeCode(Vector2 point, Rectangle rect) {
    const float handle = 8.0f;
    const Rectangle expanded {rect.x - handle, rect.y - handle, rect.width + handle * 2.0f, rect.height + handle * 2.0f};
    if (!CheckCollisionPointRec(point, expanded)) {
        return 0;
    }

    const bool left = std::fabs(point.x - rect.x) <= handle;
    const bool right = std::fabs(point.x - (rect.x + rect.width)) <= handle;
    const bool top = std::fabs(point.y - rect.y) <= handle;
    const bool bottom = std::fabs(point.y - (rect.y + rect.height)) <= handle;
    if (left && top) return 6;
    if (right && top) return 7;
    if (left && bottom) return 8;
    if (right && bottom) return 9;
    if (left) return 2;
    if (right) return 3;
    if (top) return 4;
    if (bottom) return 5;
    return CheckCollisionPointRec(point, rect) ? 1 : 0;
}

Rectangle ApplyColliderDrag(Rectangle start, int mode, Vector2 delta) {
    float left = start.x;
    float top = start.y;
    float right = start.x + start.width;
    float bottom = start.y + start.height;

    if (mode == 1) {
        left += delta.x;
        right += delta.x;
        top += delta.y;
        bottom += delta.y;
    } else {
        if (mode == 2 || mode == 6 || mode == 8) left += delta.x;
        if (mode == 3 || mode == 7 || mode == 9) right += delta.x;
        if (mode == 4 || mode == 6 || mode == 7) top += delta.y;
        if (mode == 5 || mode == 8 || mode == 9) bottom += delta.y;
    }

    if (right - left < 1.0f) {
        if (mode == 2 || mode == 6 || mode == 8) left = right - 1.0f;
        else right = left + 1.0f;
    }
    if (bottom - top < 1.0f) {
        if (mode == 4 || mode == 6 || mode == 7) top = bottom - 1.0f;
        else bottom = top + 1.0f;
    }

    return Rectangle {left, top, right - left, bottom - top};
}

bool AtlasTileFromScreen(Vector2 mouse, Rectangle drawRect, int gridWidth, int gridHeight, int textureWidth, int textureHeight, int& outX, int& outY) {
    if (drawRect.width <= 0.0f || drawRect.height <= 0.0f || gridWidth <= 0 || gridHeight <= 0 || textureWidth <= 0 || textureHeight <= 0) {
        return false;
    }

    const float localX = ((mouse.x - drawRect.x) / drawRect.width) * static_cast<float>(textureWidth);
    const float localY = ((mouse.y - drawRect.y) / drawRect.height) * static_cast<float>(textureHeight);
    const int maxCols = std::max(1, textureWidth / gridWidth);
    const int maxRows = std::max(1, textureHeight / gridHeight);
    outX = std::clamp(static_cast<int>(std::floor(localX / static_cast<float>(gridWidth))), 0, maxCols - 1);
    outY = std::clamp(static_cast<int>(std::floor(localY / static_cast<float>(gridHeight))), 0, maxRows - 1);
    return true;
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
    atlasMetadata_.clear();
    loadedAtlasMetadata_.clear();

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
    for (const AtlasAsset& asset : mapAssets_) {
        LoadAtlasMetadataForAsset(asset);
    }
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

void EditorApp::EnsureAssetTextureLoaded(AtlasAsset& asset) {
    if (asset.loaded) {
        return;
    }
    asset.texture = LoadTexture(asset.path.string().c_str());
    asset.loaded = asset.texture.id > 0;
    if (asset.loaded) {
        SetTextureFilter(asset.texture, TEXTURE_FILTER_POINT);
        SetTextureWrap(asset.texture, TEXTURE_WRAP_CLAMP);
        LoadAtlasMetadataForAsset(asset);
    }
}

void EditorApp::EnsureCurrentTextureLoaded() {
    AtlasAsset* asset = CurrentAsset();
    if (!asset) {
        return;
    }
    EnsureAssetTextureLoaded(*asset);
    ClampSelectionToTexture();
}

void EditorApp::EnsureMapRenderTexturesLoaded() {
    if (mode_ != EditorMode::Map) {
        return;
    }

    auto ensureRef = [&](const std::string& assetRef) {
        const AtlasRef ref = ParseAtlasRef(assetRef);
        if (!ref.valid) {
            return;
        }
        std::vector<AtlasAsset>& pool = ref.domain == "character" ? characterAssets_ : mapAssets_;
        for (AtlasAsset& asset : pool) {
            if (asset.filename == ref.file) {
                EnsureAssetTextureLoaded(asset);
                return;
            }
        }
    };

    for (const MapLayerDef& layer : mapLayers_) {
        ensureRef(layer.asset);
    }
    for (const MapStamp& stamp : mapStamps_) {
        ensureRef(stamp.asset);
    }
    for (const MapObject& object : mapObjects_) {
        ensureRef(PropertyValue(object.properties, "sprite"));
    }
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

void EditorApp::LoadAtlasMetadataForAsset(const AtlasAsset& asset) {
    if (asset.domain != "map" || loadedAtlasMetadata_[asset.filename]) {
        return;
    }
    loadedAtlasMetadata_[asset.filename] = true;

    const std::filesystem::path metaPath = asset.path.parent_path() / (asset.path.stem().string() + ".atlas");
    if (!std::filesystem::exists(metaPath)) {
        return;
    }

    std::ifstream in(metaPath);
    if (!in.is_open()) {
        return;
    }

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsedMetadata = Nganu::MapFormat::ParseAtlasMetadata(text, asset.filename);
    for (const auto& [key, source] : parsedMetadata) {
        AtlasTileMeta meta;
        meta.collision = source.collision;
        meta.tag = source.tag;
        meta.hasCollider = source.hasCollider;
        meta.collider = Rectangle {source.collider.x, source.collider.y, source.collider.width, source.collider.height};
        atlasMetadata_[key] = std::move(meta);
    }
}

bool EditorApp::SaveAtlasMetadataForAsset(const AtlasAsset& asset) const {
    if (asset.domain != "map") {
        return false;
    }

    const std::string prefix = asset.filename + "@";
    std::vector<std::pair<std::string, AtlasTileMeta>> entries;
    for (const auto& [key, meta] : atlasMetadata_) {
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }
        if (meta.collision.empty() && meta.tag.empty() && !meta.hasCollider) {
            continue;
        }
        entries.push_back({key, meta});
    }

    const std::filesystem::path metaPath = asset.path.parent_path() / (asset.path.stem().string() + ".atlas");
    if (entries.empty()) {
        std::error_code ec;
        std::filesystem::remove(metaPath, ec);
        return !ec;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        const std::vector<std::string> left = Nganu::MapFormat::SplitEscaped(a.first, '@');
        const std::vector<std::string> right = Nganu::MapFormat::SplitEscaped(b.first, '@');
        if (left.size() != 5 || right.size() != 5) {
            return a.first < b.first;
        }
        int leftX = 0;
        int leftY = 0;
        int rightX = 0;
        int rightY = 0;
        Nganu::MapFormat::ParseIntStrict(left[1], leftX);
        Nganu::MapFormat::ParseIntStrict(left[2], leftY);
        Nganu::MapFormat::ParseIntStrict(right[1], rightX);
        Nganu::MapFormat::ParseIntStrict(right[2], rightY);
        return leftY == rightY ? leftX < rightX : leftY < rightY;
    });

    std::ofstream out(metaPath, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    for (const auto& [key, meta] : entries) {
        const std::vector<std::string> parts = Nganu::MapFormat::SplitEscaped(key, '@');
        if (parts.size() != 5) {
            continue;
        }
        out << "tile=" << parts[1] << "," << parts[2] << "," << parts[3] << "," << parts[4];
        if (!meta.collision.empty()) {
            out << ",collision:" << Nganu::MapFormat::EscapeValue(meta.collision);
        }
        if (!meta.tag.empty()) {
            out << ",tag:" << Nganu::MapFormat::EscapeValue(meta.tag);
        }
        if (meta.hasCollider) {
            out << ",collider:" << FormatRect(meta.collider);
        }
        out << "\n";
    }

    return out.good();
}

void EditorApp::SaveAllAtlasMetadata() const {
    for (const AtlasAsset& asset : mapAssets_) {
        SaveAtlasMetadataForAsset(asset);
    }
}

EditorApp::EditorSnapshot EditorApp::CaptureSnapshot() const {
    EditorSnapshot snapshot;
    snapshot.mode = mode_;
    snapshot.activeDomain = activeDomain_;
    snapshot.activeIndex = activeIndex_;
    snapshot.gridWidth = gridWidth_;
    snapshot.gridHeight = gridHeight_;
    snapshot.selectionCols = selectionCols_;
    snapshot.selectionRows = selectionRows_;
    snapshot.selectionX = selectionX_;
    snapshot.selectionY = selectionY_;
    snapshot.activeMapFileIndex = activeMapFileIndex_;
    snapshot.currentMapFile = currentMapFile_;
    snapshot.mapId = mapId_;
    snapshot.worldName = worldName_;
    snapshot.mapTileSize = mapTileSize_;
    snapshot.mapWidth = mapWidth_;
    snapshot.mapHeight = mapHeight_;
    snapshot.mapSpawn = mapSpawn_;
    snapshot.mapProperties = mapProperties_;
    snapshot.mapLayers = mapLayers_;
    snapshot.mapStamps = mapStamps_;
    snapshot.mapObjects = mapObjects_;
    snapshot.activeMapLayerIndex = activeMapLayerIndex_;
    snapshot.mapTool = mapTool_;
    snapshot.activeMapSection = activeMapSection_;
    snapshot.selectedMapObjectIndex = selectedMapObjectIndex_;
    snapshot.objectPlacementKind = objectPlacementKind_;
    snapshot.atlasMetadata = atlasMetadata_;
    return snapshot;
}

void EditorApp::RestoreSnapshot(const EditorSnapshot& snapshot) {
    restoringSnapshot_ = true;
    mode_ = snapshot.mode;
    activeDomain_ = snapshot.activeDomain;
    activeIndex_ = snapshot.activeIndex;
    gridWidth_ = snapshot.gridWidth;
    gridHeight_ = snapshot.gridHeight;
    selectionCols_ = snapshot.selectionCols;
    selectionRows_ = snapshot.selectionRows;
    selectionX_ = snapshot.selectionX;
    selectionY_ = snapshot.selectionY;
    activeMapFileIndex_ = snapshot.activeMapFileIndex;
    currentMapFile_ = snapshot.currentMapFile;
    mapId_ = snapshot.mapId;
    worldName_ = snapshot.worldName;
    mapTileSize_ = snapshot.mapTileSize;
    mapWidth_ = snapshot.mapWidth;
    mapHeight_ = snapshot.mapHeight;
    mapSpawn_ = snapshot.mapSpawn;
    mapProperties_ = snapshot.mapProperties;
    mapLayers_ = snapshot.mapLayers;
    mapStamps_ = snapshot.mapStamps;
    mapObjects_ = snapshot.mapObjects;
    activeMapLayerIndex_ = snapshot.activeMapLayerIndex;
    mapTool_ = snapshot.mapTool;
    activeMapSection_ = snapshot.activeMapSection;
    selectedMapObjectIndex_ = snapshot.selectedMapObjectIndex;
    objectPlacementKind_ = snapshot.objectPlacementKind;
    atlasMetadata_ = snapshot.atlasMetadata;
    colliderEditTarget_ = ColliderEditTarget::None;
    colliderEditBuffer_.clear();
    atlasColliderDragMode_ = ColliderDragMode::None;
    objectColliderDragMode_ = ColliderDragMode::None;
    atlasColliderDirtyDuringDrag_ = false;
    atlasSelectionDragging_ = false;

    activeIndex_ = std::clamp(activeIndex_, 0, std::max(0, static_cast<int>(ActiveAssets().size()) - 1));
    activeMapFileIndex_ = std::clamp(activeMapFileIndex_, 0, std::max(0, static_cast<int>(mapFiles_.size()) - 1));
    activeMapLayerIndex_ = std::clamp(activeMapLayerIndex_, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        selectedMapObjectIndex_ = -1;
    }
    ClampSelectionToTexture();
    EnsureCurrentTextureLoaded();
    EnsureMapRenderTexturesLoaded();
    SaveAllAtlasMetadata();
    restoringSnapshot_ = false;
}

void EditorApp::PushUndoSnapshot() {
    if (restoringSnapshot_) {
        return;
    }
    undoStack_.push_back(CaptureSnapshot());
    if (undoStack_.size() > kUndoHistoryLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void EditorApp::Undo() {
    if (undoStack_.empty()) {
        statusText_ = "Nothing to undo.";
        return;
    }
    redoStack_.push_back(CaptureSnapshot());
    const EditorSnapshot snapshot = undoStack_.back();
    undoStack_.pop_back();
    RestoreSnapshot(snapshot);
    statusText_ = "Undo.";
}

void EditorApp::Redo() {
    if (redoStack_.empty()) {
        statusText_ = "Nothing to redo.";
        return;
    }
    undoStack_.push_back(CaptureSnapshot());
    if (undoStack_.size() > kUndoHistoryLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    const EditorSnapshot snapshot = redoStack_.back();
    redoStack_.pop_back();
    RestoreSnapshot(snapshot);
    statusText_ = "Redo.";
}

void EditorApp::HandleUndoRedoInput() {
    if (colliderEditTarget_ != ColliderEditTarget::None) {
        return;
    }
    const bool ctrlMode = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shiftMode = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!ctrlMode) {
        return;
    }
    if (IsKeyPressed(KEY_Z)) {
        if (shiftMode) {
            Redo();
        } else {
            Undo();
        }
    } else if (IsKeyPressed(KEY_Y)) {
        Redo();
    }
}

std::string EditorApp::CurrentAtlasMetaKey() const {
    const AtlasAsset* asset = CurrentAsset();
    if (!asset || asset->domain != "map") {
        return {};
    }
    const Rectangle selection = SelectionRectPixels();
    return Nganu::MapFormat::AtlasMetaKey(asset->filename,
                                          static_cast<int>(selection.x),
                                          static_cast<int>(selection.y),
                                          static_cast<int>(selection.width),
                                          static_cast<int>(selection.height));
}

const EditorApp::AtlasTileMeta* EditorApp::CurrentAtlasMeta() const {
    const std::string key = CurrentAtlasMetaKey();
    if (key.empty()) {
        return nullptr;
    }
    auto it = atlasMetadata_.find(key);
    if (it == atlasMetadata_.end()) {
        return nullptr;
    }
    return &it->second;
}

const EditorApp::AtlasTileMeta* EditorApp::MetaForAtlasRef(const std::string& assetRef) const {
    const AtlasRef ref = ParseAtlasRef(assetRef);
    if (!ref.valid || ref.domain != "map") {
        return nullptr;
    }
    const std::string key = Nganu::MapFormat::AtlasMetaKey(ref.file,
                                                           static_cast<int>(ref.source.x),
                                                           static_cast<int>(ref.source.y),
                                                           static_cast<int>(ref.source.width),
                                                           static_cast<int>(ref.source.height));
    auto it = atlasMetadata_.find(key);
    if (it == atlasMetadata_.end()) {
        return nullptr;
    }
    return &it->second;
}

void EditorApp::SetCurrentAtlasCollision(const std::string& collision) {
    AtlasAsset* asset = CurrentAsset();
    const std::string key = CurrentAtlasMetaKey();
    if (!asset || asset->domain != "map" || key.empty()) {
        statusText_ = "Collision metadata only applies to map atlases.";
        return;
    }

    PushUndoSnapshot();
    AtlasTileMeta& meta = atlasMetadata_[key];
    meta.collision = ToLower(collision);
    meta.tag.clear();
    if (CollisionBlocksMovement(meta.collision) && !meta.hasCollider) {
        const Rectangle source = SelectionRectPixels();
        meta.hasCollider = true;
        meta.collider = Rectangle {0.0f, 0.0f, source.width, source.height};
    } else if (CollisionDisablesMovementBlock(meta.collision)) {
        meta.hasCollider = false;
        meta.collider = Rectangle {};
    }
    if (SaveAtlasMetadataForAsset(*asset)) {
        statusText_ = "Atlas collision set to " + meta.collision + ".";
    } else {
        statusText_ = "Failed to save atlas metadata.";
    }
}

void EditorApp::SetCurrentAtlasTrunkCollider() {
    AtlasAsset* asset = CurrentAsset();
    const std::string key = CurrentAtlasMetaKey();
    if (!asset || asset->domain != "map" || key.empty()) {
        statusText_ = "Trunk collider only applies to map atlases.";
        return;
    }

    PushUndoSnapshot();
    AtlasTileMeta& meta = atlasMetadata_[key];
    meta.collision = "block";
    meta.hasCollider = true;
    meta.collider = TrunkColliderFor(SelectionRectPixels());
    if (SaveAtlasMetadataForAsset(*asset)) {
        statusText_ = "Atlas trunk collider saved.";
    } else {
        statusText_ = "Failed to save atlas metadata.";
    }
}

void EditorApp::AdjustCurrentAtlasCollider(int moveX, int moveY, int growWidth, int growHeight) {
    AtlasAsset* asset = CurrentAsset();
    const std::string key = CurrentAtlasMetaKey();
    if (!asset || asset->domain != "map" || key.empty()) {
        statusText_ = "Collider edit only applies to map atlases.";
        return;
    }

    PushUndoSnapshot();
    const Rectangle source = SelectionRectPixels();
    AtlasTileMeta& meta = atlasMetadata_[key];
    meta.collision = "block";
    if (!meta.hasCollider) {
        meta.collider = Rectangle {0.0f, 0.0f, source.width, source.height};
        meta.hasCollider = true;
    }
    meta.collider.x += static_cast<float>(moveX);
    meta.collider.y += static_cast<float>(moveY);
    meta.collider.width += static_cast<float>(growWidth);
    meta.collider.height += static_cast<float>(growHeight);
    meta.collider = ClampLocalRect(meta.collider, source.width, source.height);

    if (SaveAtlasMetadataForAsset(*asset)) {
        statusText_ = "Atlas collider " + FormatRect(meta.collider) + ".";
    } else {
        statusText_ = "Failed to save atlas metadata.";
    }
}

Rectangle EditorApp::CurrentAtlasColliderOrDefault() const {
    const Rectangle source = SelectionRectPixels();
    Rectangle collider {0.0f, 0.0f, source.width, source.height};
    if (const AtlasTileMeta* meta = CurrentAtlasMeta()) {
        if (meta->hasCollider) {
            collider = meta->collider;
        }
    }
    return ClampLocalRect(collider, source.width, source.height);
}

void EditorApp::SetCurrentAtlasColliderRect(Rectangle rect, bool saveNow) {
    AtlasAsset* asset = CurrentAsset();
    const std::string key = CurrentAtlasMetaKey();
    if (!asset || asset->domain != "map" || key.empty()) {
        return;
    }

    const Rectangle source = SelectionRectPixels();
    AtlasTileMeta& meta = atlasMetadata_[key];
    meta.collision = "block";
    meta.hasCollider = true;
    meta.collider = ClampLocalRect(rect, source.width, source.height);
    statusText_ = "Atlas collider " + FormatRect(meta.collider) + ".";
    if (saveNow && !SaveAtlasMetadataForAsset(*asset)) {
        statusText_ = "Failed to save atlas metadata.";
    }
}

void EditorApp::ClearCurrentAtlasCollision() {
    AtlasAsset* asset = CurrentAsset();
    const std::string key = CurrentAtlasMetaKey();
    if (!asset || asset->domain != "map" || key.empty()) {
        statusText_ = "Collision metadata only applies to map atlases.";
        return;
    }

    PushUndoSnapshot();
    atlasMetadata_.erase(key);
    if (SaveAtlasMetadataForAsset(*asset)) {
        statusText_ = "Atlas collision cleared.";
    } else {
        statusText_ = "Failed to save atlas metadata.";
    }
}

void EditorApp::SetSelectedObjectCollision(const std::string& collision) {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        statusText_ = "Select an object first.";
        return;
    }

    PushUndoSnapshot();
    MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    if (CollisionDisablesMovementBlock(collision)) {
        SetPropertyValue(object.properties, "collision", "none");
        RemovePropertyValue(object.properties, "collider");
        statusText_ = "Object collision cleared for " + object.id + ".";
        return;
    }

    SetPropertyValue(object.properties, "collision", ToLower(collision));
    statusText_ = "Object collision set to " + ToLower(collision) + " for " + object.id + ".";
}

void EditorApp::SetSelectedObjectTrunkCollider() {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        statusText_ = "Select an object first.";
        return;
    }

    PushUndoSnapshot();
    MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    SetPropertyValue(object.properties, "collision", "block");
    const Rectangle localBounds {0.0f, 0.0f, static_cast<float>(object.width), static_cast<float>(object.height)};
    SetPropertyValue(object.properties, "collider", FormatRect(TrunkColliderFor(localBounds)));
    statusText_ = "Object trunk collider set for " + object.id + ".";
}

void EditorApp::AdjustSelectedObjectCollider(int moveX, int moveY, int growWidth, int growHeight) {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        statusText_ = "Select an object first.";
        return;
    }

    PushUndoSnapshot();
    MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    Rectangle collider {0.0f, 0.0f, static_cast<float>(std::max(1, object.width)), static_cast<float>(std::max(1, object.height))};
    const std::string current = PropertyValue(object.properties, "collider");
    if (!current.empty()) {
        Rectangle parsed {};
        if (ParseLocalRect(current, parsed)) {
            collider = parsed;
        }
    }

    collider.x += static_cast<float>(moveX);
    collider.y += static_cast<float>(moveY);
    collider.width += static_cast<float>(growWidth);
    collider.height += static_cast<float>(growHeight);
    collider = ClampLocalRect(collider, static_cast<float>(std::max(1, object.width)), static_cast<float>(std::max(1, object.height)));

    SetPropertyValue(object.properties, "collision", "block");
    SetPropertyValue(object.properties, "collider", FormatRect(collider));
    statusText_ = "Object collider " + object.id + " " + FormatRect(collider) + ".";
}

Rectangle EditorApp::SelectedObjectColliderOrDefault() const {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        return Rectangle {};
    }
    const MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    Rectangle collider {0.0f, 0.0f, static_cast<float>(std::max(1, object.width)), static_cast<float>(std::max(1, object.height))};
    const std::string current = PropertyValue(object.properties, "collider");
    if (!current.empty()) {
        Rectangle parsed {};
        if (ParseLocalRect(current, parsed)) {
            collider = parsed;
        }
    } else {
        const std::string sprite = PropertyValue(object.properties, "sprite");
        const AtlasTileMeta* spriteMeta = MetaForAtlasRef(sprite);
        if (spriteMeta != nullptr && spriteMeta->hasCollider) {
            const AtlasRef spriteRef = ParseAtlasRef(sprite);
            const float sourceWidth = spriteRef.valid ? std::max(1.0f, spriteRef.source.width) : static_cast<float>(std::max(1, object.width));
            const float sourceHeight = spriteRef.valid ? std::max(1.0f, spriteRef.source.height) : static_cast<float>(std::max(1, object.height));
            collider = Rectangle {
                (spriteMeta->collider.x / sourceWidth) * static_cast<float>(std::max(1, object.width)),
                (spriteMeta->collider.y / sourceHeight) * static_cast<float>(std::max(1, object.height)),
                (spriteMeta->collider.width / sourceWidth) * static_cast<float>(std::max(1, object.width)),
                (spriteMeta->collider.height / sourceHeight) * static_cast<float>(std::max(1, object.height))
            };
        }
    }
    return ClampLocalRect(collider, static_cast<float>(std::max(1, object.width)), static_cast<float>(std::max(1, object.height)));
}

void EditorApp::SetSelectedObjectColliderRect(Rectangle rect) {
    if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        return;
    }
    MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    rect = ClampLocalRect(rect, static_cast<float>(std::max(1, object.width)), static_cast<float>(std::max(1, object.height)));
    SetPropertyValue(object.properties, "collision", "block");
    SetPropertyValue(object.properties, "collider", FormatRect(rect));
    statusText_ = "Object collider " + object.id + " " + FormatRect(rect) + ".";
}

void EditorApp::BeginColliderTextEdit(ColliderEditTarget target) {
    Rectangle rect {};
    switch (target) {
        case ColliderEditTarget::AtlasX:
        case ColliderEditTarget::AtlasY:
        case ColliderEditTarget::AtlasWidth:
        case ColliderEditTarget::AtlasHeight:
            rect = CurrentAtlasColliderOrDefault();
            break;
        case ColliderEditTarget::ObjectX:
        case ColliderEditTarget::ObjectY:
        case ColliderEditTarget::ObjectWidth:
        case ColliderEditTarget::ObjectHeight:
            if (selectedMapObjectIndex_ < 0 || selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
                return;
            }
            rect = SelectedObjectColliderOrDefault();
            break;
        case ColliderEditTarget::None:
            return;
    }

    colliderEditTarget_ = target;
    switch (target) {
        case ColliderEditTarget::AtlasX:
        case ColliderEditTarget::ObjectX:
            colliderEditBuffer_ = FormatNumber(rect.x);
            break;
        case ColliderEditTarget::AtlasY:
        case ColliderEditTarget::ObjectY:
            colliderEditBuffer_ = FormatNumber(rect.y);
            break;
        case ColliderEditTarget::AtlasWidth:
        case ColliderEditTarget::ObjectWidth:
            colliderEditBuffer_ = FormatNumber(rect.width);
            break;
        case ColliderEditTarget::AtlasHeight:
        case ColliderEditTarget::ObjectHeight:
            colliderEditBuffer_ = FormatNumber(rect.height);
            break;
        case ColliderEditTarget::None:
            break;
    }
}

void EditorApp::CommitColliderTextEdit() {
    if (colliderEditTarget_ == ColliderEditTarget::None) {
        return;
    }

    float value = 0.0f;
    if (!Nganu::MapFormat::ParseFloatStrict(colliderEditBuffer_, value)) {
        statusText_ = "Invalid collider value.";
        return;
    }

    const ColliderEditTarget target = colliderEditTarget_;
    Rectangle rect {};
    switch (target) {
        case ColliderEditTarget::AtlasX:
        case ColliderEditTarget::AtlasY:
        case ColliderEditTarget::AtlasWidth:
        case ColliderEditTarget::AtlasHeight:
            rect = CurrentAtlasColliderOrDefault();
            if (target == ColliderEditTarget::AtlasX) rect.x = value;
            else if (target == ColliderEditTarget::AtlasY) rect.y = value;
            else if (target == ColliderEditTarget::AtlasWidth) rect.width = value;
            else rect.height = value;
            PushUndoSnapshot();
            SetCurrentAtlasColliderRect(rect, true);
            break;
        case ColliderEditTarget::ObjectX:
        case ColliderEditTarget::ObjectY:
        case ColliderEditTarget::ObjectWidth:
        case ColliderEditTarget::ObjectHeight:
            rect = SelectedObjectColliderOrDefault();
            if (target == ColliderEditTarget::ObjectX) rect.x = value;
            else if (target == ColliderEditTarget::ObjectY) rect.y = value;
            else if (target == ColliderEditTarget::ObjectWidth) rect.width = value;
            else rect.height = value;
            PushUndoSnapshot();
            SetSelectedObjectColliderRect(rect);
            break;
        case ColliderEditTarget::None:
            break;
    }

    colliderEditTarget_ = ColliderEditTarget::None;
    colliderEditBuffer_.clear();
}

void EditorApp::CancelColliderTextEdit() {
    colliderEditTarget_ = ColliderEditTarget::None;
    colliderEditBuffer_.clear();
}

void EditorApp::HandleColliderTextInput() {
    if (colliderEditTarget_ == ColliderEditTarget::None) {
        return;
    }

    int ch = GetCharPressed();
    while (ch > 0) {
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
            colliderEditBuffer_.push_back(static_cast<char>(ch));
        }
        ch = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !colliderEditBuffer_.empty()) {
        colliderEditBuffer_.pop_back();
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        CommitColliderTextEdit();
    } else if (IsKeyPressed(KEY_ESCAPE)) {
        CancelColliderTextEdit();
    }
}

void EditorApp::AddMapLayer() {
    const std::string name = UniqueLayerName(mapLayers_);
    PushUndoSnapshot();
    mapLayers_.push_back(MapLayerDef {name, "tilemap", "", "#FFFFFFFF", 1.0f});
    activeMapLayerIndex_ = static_cast<int>(mapLayers_.size()) - 1;
    statusText_ = "Added layer " + name + ".";
}

void EditorApp::RemoveActiveMapLayer() {
    if (mapLayers_.size() <= 1 ||
        activeMapLayerIndex_ < 0 ||
        activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        statusText_ = "Cannot remove the last layer.";
        return;
    }

    PushUndoSnapshot();
    const std::string removedName = mapLayers_[static_cast<size_t>(activeMapLayerIndex_)].name;
    mapLayers_.erase(mapLayers_.begin() + activeMapLayerIndex_);
    mapStamps_.erase(std::remove_if(mapStamps_.begin(), mapStamps_.end(), [&](const MapStamp& stamp) {
        return stamp.layer == removedName;
    }), mapStamps_.end());
    activeMapLayerIndex_ = std::clamp(activeMapLayerIndex_, 0, static_cast<int>(mapLayers_.size()) - 1);
    statusText_ = "Removed layer " + removedName + ".";
}

void EditorApp::MoveActiveMapLayer(int delta) {
    if (delta == 0 ||
        activeMapLayerIndex_ < 0 ||
        activeMapLayerIndex_ >= static_cast<int>(mapLayers_.size())) {
        return;
    }

    const int target = activeMapLayerIndex_ + delta;
    if (target < 0 || target >= static_cast<int>(mapLayers_.size())) {
        return;
    }

    PushUndoSnapshot();
    std::swap(mapLayers_[static_cast<size_t>(activeMapLayerIndex_)], mapLayers_[static_cast<size_t>(target)]);
    activeMapLayerIndex_ = target;
    statusText_ = "Layer order updated.";
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
    mapTileSize_ = 32;
    mapWidth_ = 24;
    mapHeight_ = 18;
    mapSpawn_ = Vector2 {static_cast<float>(mapTileSize_ * 2), static_cast<float>(mapTileSize_ * 2)};
    mapProperties_.clear();
    mapProperties_.push_back({"music", "prototype_day"});
    mapProperties_.push_back({"climate", "temperate"});
    mapLayers_.clear();
    mapLayers_.push_back({"base", "tilemap", "", "#FFFFFFFF", 1.0f});
    mapStamps_.clear();
    mapObjects_.clear();
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
    if (mapLayers_.empty()) {
        mapLayers_.push_back({"ground", "image", "map:terrain_atlas.png@0@0@32@32", "#FFFFFFFF", 1.0f});
    }
    activeMapLayerIndex_ = std::clamp(activeMapLayerIndex_, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    statusText_ = "Loaded map " + path.filename().string();
    undoStack_.clear();
    redoStack_.clear();
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
            const std::string kind = layer.kind.empty() ? "tilemap" : layer.kind;
            out << "layer=" << Nganu::MapFormat::EscapeValue(layer.name) << "," << Nganu::MapFormat::EscapeValue(kind);
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
    const std::string fullSelectionRef = BuildAtlasRef();
    if ((cols > 1 || rows > 1) && MetaForAtlasRef(fullSelectionRef) != nullptr) {
        pattern.push_back(BrushStamp {0, 0, fullSelectionRef});
        return pattern;
    }

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
    if (colliderEditTarget_ != ColliderEditTarget::None) {
        return;
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
    if (IsKeyPressed(KEY_J)) SetCurrentAtlasCollision("none");
    if (IsKeyPressed(KEY_K)) SetCurrentAtlasCollision("block");
    if (IsKeyPressed(KEY_T)) SetCurrentAtlasTrunkCollider();
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) ClearCurrentAtlasCollision();
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

    const bool shiftMode = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool moveColliderMode = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool resizeColliderMode = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const int colliderStep = shiftMode ? 4 : 1;
    if (moveColliderMode || resizeColliderMode) {
        if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), leftRepeatTimer_, dt)) {
            if (moveColliderMode) AdjustCurrentAtlasCollider(-colliderStep, 0, 0, 0);
            else AdjustCurrentAtlasCollider(0, 0, -colliderStep, 0);
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), rightRepeatTimer_, dt)) {
            if (moveColliderMode) AdjustCurrentAtlasCollider(colliderStep, 0, 0, 0);
            else AdjustCurrentAtlasCollider(0, 0, colliderStep, 0);
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), upRepeatTimer_, dt)) {
            if (moveColliderMode) AdjustCurrentAtlasCollider(0, -colliderStep, 0, 0);
            else AdjustCurrentAtlasCollider(0, 0, 0, -colliderStep);
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), downRepeatTimer_, dt)) {
            if (moveColliderMode) AdjustCurrentAtlasCollider(0, colliderStep, 0, 0);
            else AdjustCurrentAtlasCollider(0, 0, 0, colliderStep);
        }
    } else {
        if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), leftRepeatTimer_, dt)) {
            selectionX_ = std::max(0, selectionX_ - 1);
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), rightRepeatTimer_, dt)) {
            selectionX_ += 1;
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), upRepeatTimer_, dt)) {
            selectionY_ = std::max(0, selectionY_ - 1);
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), downRepeatTimer_, dt)) {
            selectionY_ += 1;
        }
    }

    ClampSelectionToTexture();
}

bool EditorApp::HandleAtlasColliderPanelInput(const Rectangle& panelBounds) {
    if (mode_ != EditorMode::Atlas || activeDomain_ != Domain::Map || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return false;
    }
    const AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        return false;
    }

    const ColliderInputRects rects = BuildColliderInputRects(panelBounds, AtlasColliderInputRowY(panelBounds));
    const Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, rects.x)) {
        BeginColliderTextEdit(ColliderEditTarget::AtlasX);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.y)) {
        BeginColliderTextEdit(ColliderEditTarget::AtlasY);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.width)) {
        BeginColliderTextEdit(ColliderEditTarget::AtlasWidth);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.height)) {
        BeginColliderTextEdit(ColliderEditTarget::AtlasHeight);
        return true;
    }
    return false;
}

bool EditorApp::HandleObjectColliderPanelInput(const Rectangle& panelBounds) {
    if (mode_ != EditorMode::Map ||
        activeMapSection_ != MapSection::Object ||
        selectedMapObjectIndex_ < 0 ||
        selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size()) ||
        !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return false;
    }

    const std::string collider = PropertyValue(mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)].properties, "collider");
    const ColliderInputRects rects = BuildColliderInputRects(panelBounds, ObjectColliderInputRowY(panelBounds, selectedMapObjectIndex_, !collider.empty()));
    const Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, rects.x)) {
        BeginColliderTextEdit(ColliderEditTarget::ObjectX);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.y)) {
        BeginColliderTextEdit(ColliderEditTarget::ObjectY);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.width)) {
        BeginColliderTextEdit(ColliderEditTarget::ObjectWidth);
        return true;
    }
    if (CheckCollisionPointRec(mouse, rects.height)) {
        BeginColliderTextEdit(ColliderEditTarget::ObjectHeight);
        return true;
    }
    return false;
}

bool EditorApp::HandleAtlasColliderMouseInput(const Rectangle& canvasBounds) {
    if (mode_ != EditorMode::Atlas || activeDomain_ != Domain::Map) {
        return false;
    }

    AtlasAsset* asset = CurrentAsset();
    if (!asset || !asset->loaded) {
        atlasColliderDragMode_ = ColliderDragMode::None;
        atlasColliderDirtyDuringDrag_ = false;
        return false;
    }

    if (atlasColliderDragMode_ != ColliderDragMode::None) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 delta {
                (GetMousePosition().x - colliderDragStartMouse_.x) / zoom_,
                (GetMousePosition().y - colliderDragStartMouse_.y) / zoom_
            };
            const Rectangle rect = ApplyColliderDrag(colliderDragStartRect_, static_cast<int>(atlasColliderDragMode_), delta);
            SetCurrentAtlasColliderRect(rect, false);
            atlasColliderDirtyDuringDrag_ = true;
            return true;
        }

        if (atlasColliderDirtyDuringDrag_) {
            SaveAtlasMetadataForAsset(*asset);
        }
        atlasColliderDragMode_ = ColliderDragMode::None;
        atlasColliderDirtyDuringDrag_ = false;
        return true;
    }

    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        return false;
    }

    const AtlasTileMeta* meta = CurrentAtlasMeta();
    if (!meta || !meta->hasCollider || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !CheckCollisionPointRec(GetMousePosition(), canvasBounds)) {
        return false;
    }

    const Rectangle selection = SelectionRectPixels();
    const Rectangle sourceDraw {
        canvasBounds.x + 18.0f + pan_.x,
        canvasBounds.y + 18.0f + pan_.y,
        static_cast<float>(asset->texture.width) * zoom_,
        static_cast<float>(asset->texture.height) * zoom_
    };
    const Rectangle collider = CurrentAtlasColliderOrDefault();
    const Rectangle screenCollider {
        sourceDraw.x + (selection.x + collider.x) * zoom_,
        sourceDraw.y + (selection.y + collider.y) * zoom_,
        collider.width * zoom_,
        collider.height * zoom_
    };
    const int mode = HitColliderDragModeCode(GetMousePosition(), screenCollider);
    if (mode == 0) {
        return false;
    }

    PushUndoSnapshot();
    atlasColliderDragMode_ = static_cast<ColliderDragMode>(mode);
    colliderDragStartMouse_ = GetMousePosition();
    colliderDragStartRect_ = collider;
    atlasColliderDirtyDuringDrag_ = false;
    return true;
}

bool EditorApp::HandleSelectedObjectColliderMouseInput(const Rectangle& canvasBounds) {
    if (mode_ != EditorMode::Map ||
        activeMapSection_ != MapSection::Object ||
        selectedMapObjectIndex_ < 0 ||
        selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        objectColliderDragMode_ = ColliderDragMode::None;
        return false;
    }

    if (objectColliderDragMode_ != ColliderDragMode::None) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 delta {
                (GetMousePosition().x - colliderDragStartMouse_.x) / mapZoom_,
                (GetMousePosition().y - colliderDragStartMouse_.y) / mapZoom_
            };
            SetSelectedObjectColliderRect(ApplyColliderDrag(colliderDragStartRect_, static_cast<int>(objectColliderDragMode_), delta));
            return true;
        }

        objectColliderDragMode_ = ColliderDragMode::None;
        return true;
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !CheckCollisionPointRec(GetMousePosition(), canvasBounds)) {
        return false;
    }

    const Rectangle canvas = MapCanvasRect(canvasBounds);
    const MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    const Rectangle collider = SelectedObjectColliderOrDefault();
    const Rectangle screenCollider {
        canvas.x + (object.x + collider.x) * mapZoom_,
        canvas.y + (object.y + collider.y) * mapZoom_,
        collider.width * mapZoom_,
        collider.height * mapZoom_
    };
    const int mode = HitColliderDragModeCode(GetMousePosition(), screenCollider);
    if (mode == 0) {
        return false;
    }

    PushUndoSnapshot();
    objectColliderDragMode_ = static_cast<ColliderDragMode>(mode);
    colliderDragStartMouse_ = GetMousePosition();
    colliderDragStartRect_ = collider;
    return true;
}

void EditorApp::HandleMouseInput(const Rectangle& canvasBounds, const Rectangle& sideBounds) {
    if (mode_ != EditorMode::Atlas) {
        return;
    }

    if (atlasColliderDragMode_ != ColliderDragMode::None && HandleAtlasColliderMouseInput(canvasBounds)) {
        return;
    }

    if (CheckCollisionPointRec(GetMousePosition(), sideBounds)) {
        if (HandleAtlasColliderPanelInput(sideBounds)) {
            return;
        }
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
        atlasSelectionDragging_ = false;
        const Vector2 delta = GetMouseDelta();
        pan_.x += delta.x;
        pan_.y += delta.y;
        panning_ = true;
    } else if (panning_ && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        panning_ = false;
    }

    if (HandleAtlasColliderMouseInput(canvasBounds)) {
        return;
    }

    const float drawWidth = static_cast<float>(asset->texture.width) * zoom_;
    const float drawHeight = static_cast<float>(asset->texture.height) * zoom_;
    const Rectangle drawRect {canvasBounds.x + 18.0f + pan_.x, canvasBounds.y + 18.0f + pan_.y, drawWidth, drawHeight};
    const Vector2 mouse = GetMousePosition();

    if (atlasSelectionDragging_) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            int dragX = 0;
            int dragY = 0;
            if (AtlasTileFromScreen(mouse, drawRect, gridWidth_, gridHeight_, asset->texture.width, asset->texture.height, dragX, dragY)) {
                selectionX_ = std::min(atlasSelectionAnchorX_, dragX);
                selectionY_ = std::min(atlasSelectionAnchorY_, dragY);
                const int cols = dragX >= atlasSelectionAnchorX_ ? dragX - atlasSelectionAnchorX_ : atlasSelectionAnchorX_ - dragX;
                const int rows = dragY >= atlasSelectionAnchorY_ ? dragY - atlasSelectionAnchorY_ : atlasSelectionAnchorY_ - dragY;
                selectionCols_ = cols + 1;
                selectionRows_ = rows + 1;
                ClampSelectionToTexture();
            }
            return;
        }
        atlasSelectionDragging_ = false;
        return;
    }

    const bool leftPick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsKeyDown(KEY_SPACE) &&
        !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (!CheckCollisionPointRec(GetMousePosition(), canvasBounds) || !leftPick) {
        return;
    }

    if (!CheckCollisionPointRec(mouse, drawRect)) {
        return;
    }

    int tileX = 0;
    int tileY = 0;
    if (!AtlasTileFromScreen(mouse, drawRect, gridWidth_, gridHeight_, asset->texture.width, asset->texture.height, tileX, tileY)) {
        return;
    }

    const double now = GetTime();
    const bool doubleClick = (now - lastAtlasClickTime_) <= 0.35 &&
        tileX == lastAtlasClickTileX_ &&
        tileY == lastAtlasClickTileY_;
    const bool resizeWithMouse = doubleClick || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    lastAtlasClickTime_ = now;
    lastAtlasClickTileX_ = tileX;
    lastAtlasClickTileY_ = tileY;

    selectionX_ = tileX;
    selectionY_ = tileY;
    selectionCols_ = 1;
    selectionRows_ = 1;
    if (resizeWithMouse) {
        atlasSelectionDragging_ = true;
        atlasSelectionAnchorX_ = tileX;
        atlasSelectionAnchorY_ = tileY;
        statusText_ = "Drag cursor to resize atlas selection.";
    } else {
        statusText_ = "Atlas tile selected.";
    }
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

    PushUndoSnapshot();
    MapObject object;
    object.kind = objectPlacementKind_;
    object.id = objectPlacementKind_ + "_" + std::to_string(static_cast<int>(mapObjects_.size()) + 1);
    const int tileX = (px / mapTileSize_) * mapTileSize_;
    const int tileY = (py / mapTileSize_) * mapTileSize_;
    object.x = tileX;
    object.y = tileY;
    object.width = mapTileSize_;
    object.height = mapTileSize_;
    const std::string brush = CurrentBrushRef();
    if (!brush.empty()) {
        SetPropertyValue(object.properties, "sprite", brush);
        const AtlasRef ref = ParseAtlasRef(brush);
        if (ref.valid) {
            object.width = std::max(1, static_cast<int>(std::round(ref.source.width)));
            object.height = std::max(1, static_cast<int>(std::round(ref.source.height)));
            object.x = tileX + ((mapTileSize_ - object.width) / 2);
            object.y = tileY + mapTileSize_ - object.height;
        }
    }
    if (object.kind == "portal") {
        SetPropertyValue(object.properties, "title", "Portal");
        SetPropertyValue(object.properties, "target_x", std::to_string(object.x));
        SetPropertyValue(object.properties, "target_y", std::to_string(object.y));
    } else if (object.kind == "npc") {
        SetPropertyValue(object.properties, "title", "NPC");
        SetPropertyValue(object.properties, "name", "Guide");
    } else if (object.kind == "region") {
        object.x = tileX;
        object.y = tileY;
        object.width = mapTileSize_ * 8;
        object.height = mapTileSize_ * 6;
        SetPropertyValue(object.properties, "title", "Region");
        SetPropertyValue(object.properties, "climate", "temperate");
    } else if (object.kind == "trigger") {
        object.x = tileX;
        object.y = tileY;
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
    PushUndoSnapshot();
    const std::string removedId = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)].id;
    mapObjects_.erase(mapObjects_.begin() + selectedMapObjectIndex_);
    selectedMapObjectIndex_ = -1;
    statusText_ = "Deleted object " + removedId;
}

void EditorApp::HandleMapKeyboardInput(float dt) {
    if (mode_ != EditorMode::Map) {
        return;
    }
    if (colliderEditTarget_ != ColliderEditTarget::None) {
        return;
    }

    const float panStep = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? 20.0f : 10.0f;
    if (IsKeyPressed(KEY_N) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        PushUndoSnapshot();
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
        if (activeMapSection_ == MapSection::Tile) activeMapSection_ = MapSection::Spawn;
        else if (activeMapSection_ == MapSection::Spawn) activeMapSection_ = MapSection::Object;
        else activeMapSection_ = MapSection::Tile;
    }
    if (IsKeyPressed(KEY_ONE)) activeMapLayerIndex_ = std::min(0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_TWO)) activeMapLayerIndex_ = std::clamp(1, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_THREE)) activeMapLayerIndex_ = std::clamp(2, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_FOUR)) activeMapLayerIndex_ = std::clamp(3, 0, std::max(0, static_cast<int>(mapLayers_.size()) - 1));
    if (IsKeyPressed(KEY_P)) { mapTool_ = MapTool::Paint; activeMapSection_ = MapSection::Tile; }
    if (IsKeyPressed(KEY_X)) { mapTool_ = MapTool::Erase; activeMapSection_ = MapSection::Tile; }
    if (IsKeyPressed(KEY_G)) { mapTool_ = MapTool::Spawn; activeMapSection_ = MapSection::Spawn; }
    if (IsKeyPressed(KEY_O)) { mapTool_ = MapTool::Object; activeMapSection_ = MapSection::Object; }
    if (IsKeyPressed(KEY_NINE)) objectPlacementKind_ = "npc";
    if (IsKeyPressed(KEY_ZERO)) objectPlacementKind_ = "portal";
    if (IsKeyPressed(KEY_EIGHT)) objectPlacementKind_ = "trigger";
    if (IsKeyPressed(KEY_SEVEN)) objectPlacementKind_ = "prop";
    if (IsKeyPressed(KEY_SIX)) objectPlacementKind_ = "region";
    if (activeMapSection_ == MapSection::Tile) {
        if (IsKeyPressed(KEY_L)) AddMapLayer();
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) RemoveActiveMapLayer();
    } else if (activeMapSection_ == MapSection::Object && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))) {
        DeleteSelectedObject();
    }
    if (activeMapSection_ == MapSection::Object) {
        if (IsKeyPressed(KEY_J)) SetSelectedObjectCollision("none");
        if (IsKeyPressed(KEY_K)) SetSelectedObjectCollision("block");
        if (IsKeyPressed(KEY_T)) SetSelectedObjectTrunkCollider();
    }
    if (IsKeyPressed(KEY_R)) {
        mapZoom_ = 1.5f;
        mapPan_ = Vector2 {0.0f, 0.0f};
    }
    if (IsKeyDown(KEY_A) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL)) mapPan_.x += panStep;
    if (IsKeyDown(KEY_D) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL)) mapPan_.x -= panStep;
    if (IsKeyDown(KEY_W)) mapPan_.y += panStep;
    if (IsKeyDown(KEY_S) && !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) mapPan_.y -= panStep;

    bool consumedArrow = false;
    const bool ctrlMode = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool altMode = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool shiftMode = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const int editStep = shiftMode ? 4 : 1;
    if (activeMapSection_ == MapSection::Tile && ctrlMode) {
        if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), mapUpRepeatTimer_, dt)) {
            MoveActiveMapLayer(-1);
            consumedArrow = true;
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), mapDownRepeatTimer_, dt)) {
            MoveActiveMapLayer(1);
            consumedArrow = true;
        }
    } else if (activeMapSection_ == MapSection::Object && (ctrlMode || altMode)) {
        if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), mapLeftRepeatTimer_, dt)) {
            if (altMode) AdjustSelectedObjectCollider(-editStep, 0, 0, 0);
            else AdjustSelectedObjectCollider(0, 0, -editStep, 0);
            consumedArrow = true;
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), mapRightRepeatTimer_, dt)) {
            if (altMode) AdjustSelectedObjectCollider(editStep, 0, 0, 0);
            else AdjustSelectedObjectCollider(0, 0, editStep, 0);
            consumedArrow = true;
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), mapUpRepeatTimer_, dt)) {
            if (altMode) AdjustSelectedObjectCollider(0, -editStep, 0, 0);
            else AdjustSelectedObjectCollider(0, 0, 0, -editStep);
            consumedArrow = true;
        }
        if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), mapDownRepeatTimer_, dt)) {
            if (altMode) AdjustSelectedObjectCollider(0, editStep, 0, 0);
            else AdjustSelectedObjectCollider(0, 0, 0, editStep);
            consumedArrow = true;
        }
    }

    if (!consumedArrow) {
        if (ConsumeRepeat(IsKeyPressed(KEY_LEFT), IsKeyDown(KEY_LEFT), mapLeftRepeatTimer_, dt)) mapPan_.x += panStep;
        if (ConsumeRepeat(IsKeyPressed(KEY_RIGHT), IsKeyDown(KEY_RIGHT), mapRightRepeatTimer_, dt)) mapPan_.x -= panStep;
        if (ConsumeRepeat(IsKeyPressed(KEY_UP), IsKeyDown(KEY_UP), mapUpRepeatTimer_, dt)) mapPan_.y += panStep;
        if (ConsumeRepeat(IsKeyPressed(KEY_DOWN), IsKeyDown(KEY_DOWN), mapDownRepeatTimer_, dt)) mapPan_.y -= panStep;
    }
}

void EditorApp::HandleMapMouseInput(const Rectangle& canvasBounds, const Rectangle& sideBounds) {
    if (mode_ != EditorMode::Map) {
        return;
    }

    if (objectColliderDragMode_ != ColliderDragMode::None && HandleSelectedObjectColliderMouseInput(canvasBounds)) {
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

    const bool mouseInSidePanel = CheckCollisionPointRec(GetMousePosition(), sideBounds);
    if (!mouseInSidePanel && HandleSelectedObjectColliderMouseInput(canvasBounds)) {
        return;
    }

    int tileX = 0;
    int tileY = 0;
    if (!ScreenToMapTile(GetMousePosition(), canvasBounds, tileX, tileY) && !mouseInSidePanel) {
        return;
    }

    if (mouseInSidePanel) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (HandleObjectColliderPanelInput(sideBounds)) {
                return;
            }

            const float left = sideBounds.x + 18.0f;
            const float width = sideBounds.width - 36.0f;
            const float tabWidth = (width - 12.0f) / 3.0f;
            float layoutY = sideBounds.y + 18.0f;
            layoutY += 22.0f;
            layoutY += static_cast<float>(Ui::FontSize(27) + 10);
            layoutY += static_cast<float>(Ui::FontSize(17) + 10);
            layoutY += static_cast<float>(Ui::FontSize(17) + 10);
            layoutY += 22.0f;
            const float tabY = layoutY;

            for (int i = 0; i < 3; ++i) {
                const Rectangle tab {left + (i * (tabWidth + 6.0f)), tabY, tabWidth, 28.0f};
                if (CheckCollisionPointRec(GetMousePosition(), tab)) {
                    activeMapSection_ = static_cast<MapSection>(i);
                    if (activeMapSection_ == MapSection::Tile && mapTool_ != MapTool::Paint && mapTool_ != MapTool::Erase) mapTool_ = MapTool::Paint;
                    if (activeMapSection_ == MapSection::Spawn) mapTool_ = MapTool::Spawn;
                    if (activeMapSection_ == MapSection::Object) mapTool_ = MapTool::Object;
                    return;
                }
            }

            float y = tabY + 40.0f + 22.0f;
            const Rectangle toolA {left, y, width, 24.0f};
            const Rectangle toolB {left, y + 28.0f, width, 24.0f};
            const Rectangle toolC {left, y + 56.0f, width, 24.0f};
            if (activeMapSection_ == MapSection::Tile) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Paint; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolB)) { mapTool_ = MapTool::Erase; return; }
                y += 56.0f;
                y += 8.0f;
                y += 22.0f;
                for (int i = 0; i < static_cast<int>(mapLayers_.size()); ++i) {
                    const Rectangle row {left, y + (i * 26.0f), width, 22.0f};
                    if (CheckCollisionPointRec(GetMousePosition(), row)) {
                        activeMapLayerIndex_ = i;
                        const Rectangle upButton {row.x + row.width - 44.0f, row.y, 20.0f, row.height};
                        const Rectangle downButton {row.x + row.width - 22.0f, row.y, 20.0f, row.height};
                        if (CheckCollisionPointRec(GetMousePosition(), upButton)) {
                            MoveActiveMapLayer(-1);
                        } else if (CheckCollisionPointRec(GetMousePosition(), downButton)) {
                            MoveActiveMapLayer(1);
                        }
                        return;
                    }
                }
                const Rectangle addLayerRow {left, y + (static_cast<int>(mapLayers_.size()) * 26.0f) + 4.0f, width, 22.0f};
                if (CheckCollisionPointRec(GetMousePosition(), addLayerRow)) {
                    AddMapLayer();
                    return;
                }
            } else if (activeMapSection_ == MapSection::Spawn) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Spawn; return; }
            } else if (activeMapSection_ == MapSection::Object) {
                if (CheckCollisionPointRec(GetMousePosition(), toolA)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "prop"; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolB)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "npc"; return; }
                if (CheckCollisionPointRec(GetMousePosition(), toolC)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "portal"; return; }
                const Rectangle toolD {left, y + 84.0f, width, 24.0f};
                if (CheckCollisionPointRec(GetMousePosition(), toolD)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "trigger"; return; }
                const Rectangle toolE {left, y + 112.0f, width, 24.0f};
                if (CheckCollisionPointRec(GetMousePosition(), toolE)) { mapTool_ = MapTool::Object; objectPlacementKind_ = "region"; return; }
                y += 140.0f;
                y += 8.0f;
                y += 22.0f;
                y += static_cast<float>(Ui::FontSize(18) + 10);
                y += 48.0f;
                if (selectedMapObjectIndex_ >= 0 && selectedMapObjectIndex_ < static_cast<int>(mapObjects_.size())) {
                    y += static_cast<float>(Ui::FontSize(15) + 10);
                    const std::string collider = PropertyValue(mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)].properties, "collider");
                    if (!collider.empty()) {
                        y += static_cast<float>(Ui::FontSize(15) + 10);
                    }
                    y += 22.0f;
                    y += 32.0f;
                    y += 34.0f;
                }
                y += 22.0f;
                y += 54.0f;
                y += 22.0f;
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

    int px = 0;
    int py = 0;
    if (!ScreenToMapPixel(GetMousePosition(), canvasBounds, px, py)) {
        return;
    }

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        return;
    }

    if (mapTool_ == MapTool::Paint) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            PushUndoSnapshot();
        }
        PaintMapTile(tileX, tileY);
    } else if (mapTool_ == MapTool::Erase) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            PushUndoSnapshot();
        }
        EraseMapTile(tileX, tileY);
    } else if (mapTool_ == MapTool::Spawn && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        PushUndoSnapshot();
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
    EnsureMapRenderTexturesLoaded();
    HandleColliderTextInput();
    HandleUndoRedoInput();
    HandleKeyboardInput(dt);
    HandleMapKeyboardInput(dt);

    const Ui::Layout layout = Ui::BuildLayout(GetScreenWidth(), GetScreenHeight());
    HandleMouseInput(layout.canvas, layout.sidePanel);
    HandleMapMouseInput(layout.canvas, layout.sidePanel);
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
    if (const AtlasTileMeta* meta = CurrentAtlasMeta()) {
        const bool blocksMovement = CollisionBlocksMovement(meta->collision);
        const Color collisionColor = blocksMovement ? Ui::BlockedColor() : Ui::AccentColor();
        DrawRectangleRec(highlight, Fade(collisionColor, blocksMovement ? 0.34f : 0.16f));
        if (meta->hasCollider) {
            const Rectangle collider {
                highlight.x + meta->collider.x * zoom_,
                highlight.y + meta->collider.y * zoom_,
                meta->collider.width * zoom_,
                meta->collider.height * zoom_
            };
            DrawRectangleRec(collider, Fade(collisionColor, 0.55f));
            DrawRectangleLinesEx(collider, 2.0f, collisionColor);
            DrawColliderHandles(collider, collisionColor);
        }
    } else {
        DrawRectangleRec(highlight, Fade(Ui::AccentColor(), 0.22f));
    }
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
    if (const AtlasTileMeta* meta = CurrentAtlasMeta()) {
        state.collision = meta->collision.empty() ? "pass" : meta->collision;
        if (meta->hasCollider) {
            state.collider = FormatRect(meta->collider);
        }
    } else {
        state.collision = "pass";
    }
    state.status = statusText_;
    state.hasSelection = asset && asset->loaded;
    if (state.hasSelection) {
        state.texture = asset->texture;
        state.source = SelectionRectPixels();
    }
    Ui::DrawAtlasPanel(uiFont_, bounds, state);
    DrawAtlasColliderInputs(bounds);
}

void EditorApp::DrawAtlasColliderInputs(Rectangle bounds) const {
    const AtlasAsset* asset = CurrentAsset();
    if (mode_ != EditorMode::Atlas || activeDomain_ != Domain::Map || !asset || !asset->loaded) {
        return;
    }

    const Rectangle rect = CurrentAtlasColliderOrDefault();
    const ColliderInputRects fields = BuildColliderInputRects(bounds, AtlasColliderInputRowY(bounds));
    const bool atlasTarget =
        colliderEditTarget_ == ColliderEditTarget::AtlasX ||
        colliderEditTarget_ == ColliderEditTarget::AtlasY ||
        colliderEditTarget_ == ColliderEditTarget::AtlasWidth ||
        colliderEditTarget_ == ColliderEditTarget::AtlasHeight;

    DrawColliderInputField(uiFont_, fields.x, "x",
        colliderEditTarget_ == ColliderEditTarget::AtlasX ? colliderEditBuffer_ : FormatNumber(rect.x),
        atlasTarget && colliderEditTarget_ == ColliderEditTarget::AtlasX);
    DrawColliderInputField(uiFont_, fields.y, "y",
        colliderEditTarget_ == ColliderEditTarget::AtlasY ? colliderEditBuffer_ : FormatNumber(rect.y),
        atlasTarget && colliderEditTarget_ == ColliderEditTarget::AtlasY);
    DrawColliderInputField(uiFont_, fields.width, "w",
        colliderEditTarget_ == ColliderEditTarget::AtlasWidth ? colliderEditBuffer_ : FormatNumber(rect.width),
        atlasTarget && colliderEditTarget_ == ColliderEditTarget::AtlasWidth);
    DrawColliderInputField(uiFont_, fields.height, "h",
        colliderEditTarget_ == ColliderEditTarget::AtlasHeight ? colliderEditBuffer_ : FormatNumber(rect.height),
        atlasTarget && colliderEditTarget_ == ColliderEditTarget::AtlasHeight);
}

void EditorApp::DrawMapCanvas(Rectangle bounds) const {
    DrawRectangleRounded(bounds, 0.02f, 8, Color {252, 248, 241, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Fade(Ui::InkColor(), 0.14f));

    const Rectangle canvas = MapCanvasRect(bounds);
    BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    DrawRectangleRec(canvas, Color {221, 228, 216, 255});

    auto stampDrawRect = [&](const MapStamp& stamp) {
        const AtlasRef ref = ParseAtlasRef(stamp.asset);
        const float sourceWidth = ref.valid ? std::max(1.0f, ref.source.width) : static_cast<float>(mapTileSize_);
        const float sourceHeight = ref.valid ? std::max(1.0f, ref.source.height) : static_cast<float>(mapTileSize_);
        return Rectangle {
            canvas.x + ((stamp.x * mapTileSize_) + ((static_cast<float>(mapTileSize_) - sourceWidth) * 0.5f)) * mapZoom_,
            canvas.y + (((stamp.y + 1) * mapTileSize_) - sourceHeight) * mapZoom_,
            sourceWidth * mapZoom_,
            sourceHeight * mapZoom_
        };
    };

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
            const Rectangle dest = stampDrawRect(stamp);
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

    for (const MapStamp& stamp : mapStamps_) {
        const AtlasTileMeta* meta = MetaForAtlasRef(stamp.asset);
        if (!meta || !CollisionBlocksMovement(meta->collision)) {
            continue;
        }

        Rectangle drawRect = stampDrawRect(stamp);
        if (meta->hasCollider) {
            const AtlasRef ref = ParseAtlasRef(stamp.asset);
            const float sourceWidth = ref.valid ? std::max(1.0f, ref.source.width) : static_cast<float>(mapTileSize_);
            const float sourceHeight = ref.valid ? std::max(1.0f, ref.source.height) : static_cast<float>(mapTileSize_);
            drawRect = Rectangle {
                drawRect.x + (meta->collider.x / sourceWidth) * drawRect.width,
                drawRect.y + (meta->collider.y / sourceHeight) * drawRect.height,
                (meta->collider.width / sourceWidth) * drawRect.width,
                (meta->collider.height / sourceHeight) * drawRect.height
            };
        }
        const Color color = Ui::BlockedColor();
        DrawRectangleRec(drawRect, Fade(color, 0.42f));
        DrawRectangleLinesEx(drawRect, 1.5f, Fade(color, 0.9f));
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

        const std::string collision = PropertyValue(object.properties, "collision");
        const AtlasTileMeta* spriteMeta = MetaForAtlasRef(sprite);
        const bool hasExplicitCollision = !collision.empty();
        const bool blocksMovement = hasExplicitCollision
            ? !CollisionDisablesMovementBlock(collision) && CollisionBlocksMovement(collision)
            : (spriteMeta != nullptr && CollisionBlocksMovement(spriteMeta->collision));
        const bool selectedObject = i == selectedMapObjectIndex_ && activeMapSection_ == MapSection::Object;
        if (blocksMovement || selectedObject) {
            Rectangle colliderRect = drawRect;
            Rectangle localCollider {};
            const std::string collider = PropertyValue(object.properties, "collider");
            const std::string hitbox = PropertyValue(object.properties, "hitbox");
            if (!collider.empty() && ParseLocalRect(collider, localCollider)) {
                colliderRect = Rectangle {
                    canvas.x + (object.x + localCollider.x) * mapZoom_,
                    canvas.y + (object.y + localCollider.y) * mapZoom_,
                    localCollider.width * mapZoom_,
                    localCollider.height * mapZoom_
                };
            } else if (!hitbox.empty() && ParseLocalRect(hitbox, localCollider)) {
                colliderRect = Rectangle {
                    canvas.x + (object.x + localCollider.x) * mapZoom_,
                    canvas.y + (object.y + localCollider.y) * mapZoom_,
                    localCollider.width * mapZoom_,
                    localCollider.height * mapZoom_
                };
            } else if (spriteMeta != nullptr && spriteMeta->hasCollider) {
                const AtlasRef spriteRef = ParseAtlasRef(sprite);
                const float sourceWidth = spriteRef.valid ? std::max(1.0f, spriteRef.source.width) : static_cast<float>(std::max(1, object.width));
                const float sourceHeight = spriteRef.valid ? std::max(1.0f, spriteRef.source.height) : static_cast<float>(std::max(1, object.height));
                colliderRect = Rectangle {
                    drawRect.x + (spriteMeta->collider.x / sourceWidth) * drawRect.width,
                    drawRect.y + (spriteMeta->collider.y / sourceHeight) * drawRect.height,
                    (spriteMeta->collider.width / sourceWidth) * drawRect.width,
                    (spriteMeta->collider.height / sourceHeight) * drawRect.height
                };
            }
            const Color color = blocksMovement ? Ui::BlockedColor() : Ui::AccentColor();
            DrawRectangleRec(colliderRect, Fade(color, blocksMovement ? 0.42f : 0.24f));
            DrawRectangleLinesEx(colliderRect, 2.0f, Fade(color, 0.95f));
            if (selectedObject) {
                DrawColliderHandles(colliderRect, Fade(color, 0.96f));
            }
        }
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
        Rectangle hover {
            canvas.x + (hoverTileX * mapTileSize_ * mapZoom_),
            canvas.y + (hoverTileY * mapTileSize_ * mapZoom_),
            mapTileSize_ * mapZoom_,
            mapTileSize_ * mapZoom_
        };
        if (mapTool_ == MapTool::Paint) {
            const std::vector<BrushStamp> brush = CurrentBrushPattern();
            if (brush.size() == 1) {
                const AtlasRef ref = ParseAtlasRef(brush.front().asset);
                if (ref.valid && (ref.source.width > static_cast<float>(mapTileSize_) || ref.source.height > static_cast<float>(mapTileSize_))) {
                    const float width = std::max(1.0f, ref.source.width);
                    const float height = std::max(1.0f, ref.source.height);
                    hover.x = canvas.x + ((hoverTileX * mapTileSize_) + ((mapTileSize_ - width) * 0.5f)) * mapZoom_;
                    hover.y = canvas.y + (((hoverTileY + 1) * mapTileSize_) - height) * mapZoom_;
                    hover.width = width * mapZoom_;
                    hover.height = height * mapZoom_;
                } else {
                    hover.width = mapTileSize_ * mapZoom_;
                    hover.height = mapTileSize_ * mapZoom_;
                }
            } else {
                hoverCols = std::max(1, selectionCols_);
                hoverRows = std::max(1, selectionRows_);
                hover.width = hoverCols * mapTileSize_ * mapZoom_;
                hover.height = hoverRows * mapTileSize_ * mapZoom_;
            }
        } else if (mapTool_ == MapTool::Object) {
            hoverCols = 1;
            hoverRows = 1;
            if (objectPlacementKind_ == "trigger") {
                hoverCols = 2;
                hoverRows = 2;
                hover.width = hoverCols * mapTileSize_ * mapZoom_;
                hover.height = hoverRows * mapTileSize_ * mapZoom_;
            } else if (objectPlacementKind_ == "region") {
                hoverCols = 8;
                hoverRows = 6;
                hover.width = hoverCols * mapTileSize_ * mapZoom_;
                hover.height = hoverRows * mapTileSize_ * mapZoom_;
            } else {
                const AtlasRef ref = ParseAtlasRef(CurrentBrushRef());
                if (ref.valid) {
                    const float width = std::max(1.0f, ref.source.width);
                    const float height = std::max(1.0f, ref.source.height);
                    hover.x = canvas.x + ((hoverTileX * mapTileSize_) + ((mapTileSize_ - width) * 0.5f)) * mapZoom_;
                    hover.y = canvas.y + (((hoverTileY + 1) * mapTileSize_) - height) * mapZoom_;
                    hover.width = width * mapZoom_;
                    hover.height = height * mapZoom_;
                }
            }
        }

        if ((mapTool_ != MapTool::Object || objectPlacementKind_ == "trigger" || objectPlacementKind_ == "region") &&
            !(mapTool_ == MapTool::Paint && CurrentBrushPattern().size() == 1)) {
            hoverCols = std::min(hoverCols, std::max(1, mapWidth_ - hoverTileX));
            hoverRows = std::min(hoverRows, std::max(1, mapHeight_ - hoverTileY));
            hover.width = hoverCols * mapTileSize_ * mapZoom_;
            hover.height = hoverRows * mapTileSize_ * mapZoom_;
        }
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
    state.layerCount = static_cast<int>(mapLayers_.size());
    state.brushRef = CurrentBrushRef();
    state.tileCollisionCount = 0;
    for (const MapStamp& stamp : mapStamps_) {
        const AtlasTileMeta* meta = MetaForAtlasRef(stamp.asset);
        if (meta != nullptr && CollisionBlocksMovement(meta->collision)) {
            ++state.tileCollisionCount;
        }
    }
    state.objectCollisionCount = 0;
    for (const MapObject& object : mapObjects_) {
        const std::string collision = PropertyValue(object.properties, "collision");
        const std::string sprite = PropertyValue(object.properties, "sprite");
        const AtlasTileMeta* meta = MetaForAtlasRef(sprite);
        const bool blocksMovement = !collision.empty()
            ? !CollisionDisablesMovementBlock(collision) && CollisionBlocksMovement(collision)
            : (meta != nullptr && CollisionBlocksMovement(meta->collision));
        if (blocksMovement) {
            ++state.objectCollisionCount;
        }
    }
    state.spawn = std::to_string(static_cast<int>(mapSpawn_.x)) + "," + std::to_string(static_cast<int>(mapSpawn_.y));
    state.objectKind = objectPlacementKind_;
    state.selectedObject = selectedMapObjectIndex_;
    if (selectedMapObjectIndex_ >= 0 && selectedMapObjectIndex_ < static_cast<int>(mapObjects_.size())) {
        const MapObject& selected = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
        state.selectedCollision = PropertyValue(selected.properties, "collision");
        if (state.selectedCollision.empty()) {
            state.selectedCollision = "sprite/pass";
        }
        state.selectedCollider = PropertyValue(selected.properties, "collider");
    }
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
    DrawObjectColliderInputs(bounds);
}

void EditorApp::DrawObjectColliderInputs(Rectangle bounds) const {
    if (mode_ != EditorMode::Map ||
        activeMapSection_ != MapSection::Object ||
        selectedMapObjectIndex_ < 0 ||
        selectedMapObjectIndex_ >= static_cast<int>(mapObjects_.size())) {
        return;
    }

    const MapObject& object = mapObjects_[static_cast<size_t>(selectedMapObjectIndex_)];
    const Rectangle rect = SelectedObjectColliderOrDefault();
    const std::string collider = PropertyValue(object.properties, "collider");
    const ColliderInputRects fields = BuildColliderInputRects(bounds, ObjectColliderInputRowY(bounds, selectedMapObjectIndex_, !collider.empty()));
    const bool objectTarget =
        colliderEditTarget_ == ColliderEditTarget::ObjectX ||
        colliderEditTarget_ == ColliderEditTarget::ObjectY ||
        colliderEditTarget_ == ColliderEditTarget::ObjectWidth ||
        colliderEditTarget_ == ColliderEditTarget::ObjectHeight;

    DrawColliderInputField(uiFont_, fields.x, "x",
        colliderEditTarget_ == ColliderEditTarget::ObjectX ? colliderEditBuffer_ : FormatNumber(rect.x),
        objectTarget && colliderEditTarget_ == ColliderEditTarget::ObjectX);
    DrawColliderInputField(uiFont_, fields.y, "y",
        colliderEditTarget_ == ColliderEditTarget::ObjectY ? colliderEditBuffer_ : FormatNumber(rect.y),
        objectTarget && colliderEditTarget_ == ColliderEditTarget::ObjectY);
    DrawColliderInputField(uiFont_, fields.width, "w",
        colliderEditTarget_ == ColliderEditTarget::ObjectWidth ? colliderEditBuffer_ : FormatNumber(rect.width),
        objectTarget && colliderEditTarget_ == ColliderEditTarget::ObjectWidth);
    DrawColliderInputField(uiFont_, fields.height, "h",
        colliderEditTarget_ == ColliderEditTarget::ObjectHeight ? colliderEditBuffer_ : FormatNumber(rect.height),
        objectTarget && colliderEditTarget_ == ColliderEditTarget::ObjectHeight);
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
