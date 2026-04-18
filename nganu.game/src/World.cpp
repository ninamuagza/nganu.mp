#include "World.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>

namespace {
Color GrassColor() { return Color {84, 148, 92, 255}; }
Color GrassShade() { return Color {67, 123, 77, 255}; }
Color PathColor() { return Color {182, 157, 110, 255}; }
Color WaterColor() { return Color {59, 120, 168, 255}; }
Color WaterEdgeColor() { return Color {94, 163, 209, 255}; }
Color CanopyColor() { return Color {64, 114, 71, 255}; }
Color CanopyShade() { return Color {88, 146, 95, 255}; }

#if defined(PLATFORM_ANDROID)
constexpr bool kFastMobileRender = true;
#else
constexpr bool kFastMobileRender = false;
#endif

struct AtlasRef {
    AssetDomain domain = AssetDomain::Map;
    std::string file;
    Rectangle source {};
    bool valid = false;
};

bool ParseRectangle(const std::string& value, Rectangle& out) {
    std::istringstream stream(value);
    std::string token;
    float parts[4] {};
    int index = 0;
    while (std::getline(stream, token, ',') && index < 4) {
        try {
            parts[index++] = std::stof(token);
        } catch (...) {
            return false;
        }
    }
    if (index != 4) {
        return false;
    }

    out = Rectangle {parts[0], parts[1], parts[2], parts[3]};
    return true;
}

bool ParseVector2(const std::string& value, Vector2& out) {
    std::istringstream stream(value);
    std::string token;
    float parts[2] {};
    int index = 0;
    while (std::getline(stream, token, ',') && index < 2) {
        try {
            parts[index++] = std::stof(token);
        } catch (...) {
            return false;
        }
    }
    if (index != 2) {
        return false;
    }

    out = Vector2 {parts[0], parts[1]};
    return true;
}

bool ParsePair(const std::string& value, float& a, float& b) {
    std::istringstream stream(value);
    std::string token;
    if (!std::getline(stream, token, '|')) {
        return false;
    }
    try {
        a = std::stof(token);
    } catch (...) {
        return false;
    }
    if (!std::getline(stream, token, '|')) {
        return false;
    }
    try {
        b = std::stof(token);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::string> SplitComma(const std::string& value) {
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
        if (ch == ',') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(current);
    return parts;
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

std::optional<std::pair<std::string, std::string>> SplitPropertyAssignment(const std::string& value, char delim) {
    std::string key;
    std::string remainder;
    bool escaping = false;
    bool foundDelim = false;
    for (char ch : value) {
        if (escaping) {
            if (foundDelim) {
                remainder.push_back(ch);
            } else {
                key.push_back(ch);
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (!foundDelim && ch == delim) {
            foundDelim = true;
            continue;
        }
        if (foundDelim) {
            remainder.push_back(ch);
        } else {
            key.push_back(ch);
        }
    }
    if (!foundDelim) {
        return std::nullopt;
    }
    return std::make_pair(key, remainder);
}

Color ParseHexColor(const std::string& value, Color fallback) {
    std::string hex = value;
    if (!hex.empty() && hex[0] == '#') {
        hex.erase(hex.begin());
    }
    if (hex.size() != 6 && hex.size() != 8) {
        return fallback;
    }

    auto parsePair = [](const std::string& text, size_t offset) -> std::optional<int> {
        try {
            return std::stoi(text.substr(offset, 2), nullptr, 16);
        } catch (...) {
            return std::nullopt;
        }
    };

    const std::optional<int> r = parsePair(hex, 0);
    const std::optional<int> g = parsePair(hex, 2);
    const std::optional<int> b = parsePair(hex, 4);
    if (!r.has_value() || !g.has_value() || !b.has_value()) {
        return fallback;
    }

    int a = 255;
    if (hex.size() == 8) {
        const std::optional<int> parsedA = parsePair(hex, 6);
        if (!parsedA.has_value()) {
            return fallback;
        }
        a = *parsedA;
    }

    return Color {
        static_cast<unsigned char>(*r),
        static_cast<unsigned char>(*g),
        static_cast<unsigned char>(*b),
        static_cast<unsigned char>(a)
    };
}

AtlasRef ParseAtlasRef(const std::string& asset) {
    AtlasRef ref;
    std::vector<std::string> parts;
    std::istringstream stream(asset);
    std::string token;
    while (std::getline(stream, token, '@')) {
        parts.push_back(token);
    }

    if (parts.size() != 5) {
        return ref;
    }

    ref.file = parts[0];
    ref.file.erase(std::remove(ref.file.begin(), ref.file.end(), '\\'), ref.file.end());
    const size_t domainSep = ref.file.find(':');
    if (domainSep != std::string::npos) {
        const std::string domain = ref.file.substr(0, domainSep);
        ref.file = ref.file.substr(domainSep + 1);
        if (domain == "character") {
            ref.domain = AssetDomain::Character;
        } else {
            ref.domain = AssetDomain::Map;
        }
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

bool HasStampsOnLayer(const std::vector<WorldStamp>& stamps, const std::string& layerName) {
    return std::any_of(stamps.begin(), stamps.end(), [&](const WorldStamp& stamp) {
        return stamp.layer == layerName;
    });
}

bool LayerHasUsableImage(const WorldLayer& layer) {
    return layer.kind == "image" && !layer.asset.empty() && ParseAtlasRef(layer.asset).valid;
}

const std::filesystem::path& RootForDomain(AssetDomain domain, const std::filesystem::path& mapRoot, const std::filesystem::path& characterRoot) {
    return domain == AssetDomain::Character ? characterRoot : mapRoot;
}

const char* DomainKey(AssetDomain domain) {
    return domain == AssetDomain::Character ? "character" : "map";
}

Texture2D LoadTextureFromDiskBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return Texture2D {};
    }

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return Texture2D {};
    }

    std::string ext = path.extension().string();
    if (ext.empty()) {
        ext = ".png";
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    Image image = LoadImageFromMemory(ext.c_str(), bytes.data(), static_cast<int>(bytes.size()));
    if (image.data == nullptr) {
        return Texture2D {};
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

Texture2D* EnsureTextureLoaded(std::unordered_map<std::string, Texture2D>& textures, const AtlasRef& ref,
                               const std::filesystem::path& mapRoot, const std::filesystem::path& characterRoot) {
    const std::string cacheKey = std::string(DomainKey(ref.domain)) + ":" + ref.file;
    auto textureIt = textures.find(cacheKey);
    if (textureIt != textures.end()) {
        if (textureIt->second.id > 0) {
            return &textureIt->second;
        }
        textures.erase(textureIt);
    }

    const std::filesystem::path path = RootForDomain(ref.domain, mapRoot, characterRoot) / ref.file;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) <= 8) {
        return nullptr;
    }

    Texture2D texture = LoadTextureFromDiskBytes(path);
    if (texture.id <= 0) {
        texture = LoadTexture(path.string().c_str());
    }
    if (texture.id <= 0 || texture.width <= 0 || texture.height <= 0) {
        return nullptr;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    textureIt = textures.emplace(cacheKey, texture).first;
    return &textureIt->second;
}

void DrawTiledTexture(const Texture2D& texture, const Rectangle& source, const Rectangle& area, Color tint) {
    const float startX = std::floor(area.x / source.width) * source.width;
    const float startY = std::floor(area.y / source.height) * source.height;
    for (float y = startY; y < area.y + area.height; y += source.height) {
        for (float x = startX; x < area.x + area.width; x += source.width) {
            const Rectangle dest {x, y, source.width, source.height};
            if (!CheckCollisionRecs(dest, area)) {
                continue;
            }
            DrawTexturePro(texture, source, dest, Vector2 {}, 0.0f, tint);
        }
    }
}

Rectangle ClampRectToBounds(Rectangle rect, Rectangle bounds) {
    const float x1 = std::max(rect.x, bounds.x);
    const float y1 = std::max(rect.y, bounds.y);
    const float x2 = std::min(rect.x + rect.width, bounds.x + bounds.width);
    const float y2 = std::min(rect.y + rect.height, bounds.y + bounds.height);
    if (x2 <= x1 || y2 <= y1) {
        return Rectangle {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return Rectangle {x1, y1, x2 - x1, y2 - y1};
}

bool HasArea(Rectangle rect) {
    return rect.width > 0.0f && rect.height > 0.0f;
}

std::string AtlasMetaKey(const std::string& file, int x, int y, int w, int h) {
    return file + "@" + std::to_string(x) + "@" + std::to_string(y) + "@" + std::to_string(w) + "@" + std::to_string(h);
}
}

World::World() {
    LoadDefaults();
}

World::~World() {
    UnloadTextures();
}

void World::LoadDefaults() {
    UnloadTextures();
    tileSize_ = 48;
    width_ = 16;
    height_ = 12;
    mapId_.clear();
    worldName_.clear();
    spawnPoint_ = Vector2 {160.0f, 160.0f};
    properties_.clear();
    layers_.clear();
    stamps_.clear();
    objects_.clear();

    blockedAreas_.clear();
    waterAreas_.clear();
    atlasTileMeta_.clear();
}

bool World::LoadFromMapAsset(const std::string& rawAsset) {
    atlasTileMeta_.clear();
    int nextTileSize = tileSize_;
    int nextWidth = width_;
    int nextHeight = height_;
    std::string nextMapId = mapId_;
    std::string nextWorldName = worldName_;
    Vector2 nextSpawn = spawnPoint_;
    std::unordered_map<std::string, std::string> nextProperties;
    std::vector<WorldLayer> nextLayers;
    std::vector<WorldStamp> nextStamps;
    std::vector<WorldObject> nextObjects;
    std::vector<Rectangle> nextBlocked;
    std::vector<Rectangle> nextWater;

    std::istringstream stream(rawAsset);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "tile") {
            try {
                nextTileSize = std::stoi(value);
            } catch (...) {
                return false;
            }
        } else if (key == "width") {
            try {
                nextWidth = std::stoi(value);
            } catch (...) {
                return false;
            }
        } else if (key == "height") {
            try {
                nextHeight = std::stoi(value);
            } catch (...) {
                return false;
            }
        } else if (key == "map_id") {
            nextMapId = value;
        } else if (key == "world_name") {
            nextWorldName = value;
        } else if (key == "spawn") {
            if (!ParseVector2(value, nextSpawn)) {
                return false;
            }
        } else if (key == "property") {
            const std::vector<std::string> parts = SplitEscaped(value, ',');
            if (parts.size() < 2) {
                return false;
            }
            nextProperties[parts[0]] = parts[1];
        } else if (key == "layer") {
            const std::vector<std::string> parts = SplitComma(value);
            if (parts.size() < 5) {
                return false;
            }
            WorldLayer layer;
            layer.name = parts[0];
            layer.kind = parts[1];
            layer.asset = parts[2];
            layer.tint = ParseHexColor(parts[3], RAYWHITE);
            try {
                layer.parallax = std::stof(parts[4]);
            } catch (...) {
                return false;
            }
            LoadAtlasMetadataForRef(layer.asset);
            nextLayers.push_back(std::move(layer));
        } else if (key == "object") {
            const std::vector<std::string> parts = SplitComma(value);
            if (parts.size() < 6) {
                return false;
            }
            WorldObject object;
            object.kind = parts[0];
            object.id = parts[1];
            try {
                object.bounds.x = std::stof(parts[2]);
                object.bounds.y = std::stof(parts[3]);
                object.bounds.width = std::stof(parts[4]);
                object.bounds.height = std::stof(parts[5]);
            } catch (...) {
                return false;
            }
            for (size_t i = 6; i < parts.size(); ++i) {
                const auto prop = SplitPropertyAssignment(parts[i], ':');
                if (!prop.has_value()) {
                    continue;
                }
                object.properties[prop->first] = prop->second;
            }
            nextObjects.push_back(std::move(object));
        } else if (key == "stamp") {
            const std::vector<std::string> parts = SplitComma(value);
            if (parts.size() < 4) {
                return false;
            }
            WorldStamp stamp;
            stamp.layer = parts[0];
            try {
                stamp.x = std::stoi(parts[1]);
                stamp.y = std::stoi(parts[2]);
            } catch (...) {
                return false;
            }
            stamp.asset = parts[3];
            LoadAtlasMetadataForRef(stamp.asset);
            nextStamps.push_back(std::move(stamp));
        } else if (key == "blocked") {
            Rectangle area {};
            if (!ParseRectangle(value, area)) {
                return false;
            }
            nextBlocked.push_back(area);
        } else if (key == "water") {
            Rectangle area {};
            if (!ParseRectangle(value, area)) {
                return false;
            }
            nextWater.push_back(area);
        }
    }

    if (nextTileSize < 16 || nextTileSize > 128 || nextWidth < 8 || nextHeight < 8) {
        return false;
    }

    UnloadTextures();
    tileSize_ = nextTileSize;
    width_ = nextWidth;
    height_ = nextHeight;
    mapId_ = nextMapId;
    worldName_ = nextWorldName;
    spawnPoint_ = nextSpawn;
    properties_ = std::move(nextProperties);
    layers_ = std::move(nextLayers);
    stamps_ = std::move(nextStamps);
    objects_ = std::move(nextObjects);
    blockedAreas_ = std::move(nextBlocked);
    waterAreas_ = std::move(nextWater);
    return true;
}

bool World::IsWalkable(Vector2 worldPosition, float radius) const {
    const Rectangle body {
        worldPosition.x - radius,
        worldPosition.y - radius,
        radius * 2.0f,
        radius * 2.0f
    };

    const Rectangle bounds = Bounds();
    if (body.x < bounds.x || body.y < bounds.y) {
        return false;
    }
    if (body.x + body.width > bounds.width || body.y + body.height > bounds.height) {
        return false;
    }

    for (const Rectangle& area : blockedAreas_) {
        if (CheckCollisionRecs(body, area)) {
            return false;
        }
    }

    for (const Rectangle& area : waterAreas_) {
        if (CheckCollisionRecs(body, area)) {
            return false;
        }
    }

    for (const WorldStamp& stamp : stamps_) {
        const auto meta = metaForAsset(stamp.asset);
        if (!meta.has_value() || !meta->blocksMovement) {
            continue;
        }
        const Rectangle stampRect {
            static_cast<float>(stamp.x * tileSize_),
            static_cast<float>(stamp.y * tileSize_),
            static_cast<float>(tileSize_),
            static_cast<float>(tileSize_)
        };
        if (CheckCollisionRecs(body, stampRect)) {
            return false;
        }
    }

    return true;
}

Rectangle World::Bounds() const {
    return Rectangle {0.0f, 0.0f, static_cast<float>(width_ * tileSize_), static_cast<float>(height_ * tileSize_)};
}

void World::DrawGround(Rectangle visibleArea) const {
    const Rectangle bounds = Bounds();
    const Rectangle clippedView = ClampRectToBounds(visibleArea, bounds);
    if (!HasArea(clippedView)) {
        return;
    }

    const bool hasGroundStamps = HasStampsOnLayer(stamps_, "ground");
    DrawRectangleRec(clippedView, GrassColor());

    for (const WorldLayer& layer : layers_) {
        if (layer.name == "ground" && layer.kind == "color") {
            DrawRectangleRec(clippedView, Fade(layer.tint, 0.18f));
        } else if (layer.name == "ground" && LayerHasUsableImage(layer)) {
            const AtlasRef ref = ParseAtlasRef(layer.asset);
            if (ref.valid) {
                Texture2D* texture = EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_);
                if (texture != nullptr) {
                    DrawTiledTexture(*texture, ref.source, clippedView, layer.tint);
                }
            }
        }
    }

    for (const WorldStamp& stamp : stamps_) {
        if (stamp.layer != "ground") {
            continue;
        }
        const Rectangle dest {
            static_cast<float>(stamp.x * tileSize_),
            static_cast<float>(stamp.y * tileSize_),
            static_cast<float>(tileSize_),
            static_cast<float>(tileSize_)
        };
        if (!CheckCollisionRecs(dest, clippedView)) {
            continue;
        }
        DrawSpriteRef(stamp.asset, dest, Vector2 {}, 0.0f, WHITE);
    }

    const int minTileX = std::max(0, static_cast<int>(std::floor(clippedView.x / tileSize_)));
    const int minTileY = std::max(0, static_cast<int>(std::floor(clippedView.y / tileSize_)));
    const int maxTileX = std::min(width_, static_cast<int>(std::ceil((clippedView.x + clippedView.width) / tileSize_)));
    const int maxTileY = std::min(height_, static_cast<int>(std::ceil((clippedView.y + clippedView.height) / tileSize_)));
    if (!kFastMobileRender && !hasGroundStamps) {
        for (int y = minTileY; y < maxTileY; ++y) {
            for (int x = minTileX; x < maxTileX; ++x) {
                if ((x + y) % 2 == 0) {
                    DrawRectangle(
                        x * tileSize_,
                        y * tileSize_,
                        tileSize_,
                        tileSize_,
                        Fade(GrassShade(), 0.22f)
                    );
                }
            }
        }
    }

    for (const Rectangle& area : waterAreas_) {
        if (!CheckCollisionRecs(area, clippedView)) {
            continue;
        }
        bool drewTexture = false;
        for (const WorldLayer& layer : layers_) {
            if (layer.name != "water" || layer.kind != "image") {
                continue;
            }
            const AtlasRef ref = ParseAtlasRef(layer.asset);
            if (!ref.valid) {
                continue;
            }
            Texture2D* texture = EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_);
            if (texture != nullptr) {
                const Rectangle clippedArea = ClampRectToBounds(area, clippedView);
                if (HasArea(clippedArea)) {
                    DrawTiledTexture(*texture, ref.source, clippedArea, layer.tint);
                }
                drewTexture = true;
            }
        }
        if (!drewTexture) {
            DrawRectangleRounded(area, 0.18f, 10, WaterColor());
        }
        if (!kFastMobileRender) {
            DrawRectangleRoundedLinesEx(area, 0.18f, 10, 4.0f, WaterEdgeColor());
        }
    }
}

void World::DrawDecorations(Rectangle visibleArea) const {
    const Rectangle clippedView = ClampRectToBounds(visibleArea, Bounds());
    if (!HasArea(clippedView)) {
        return;
    }
    for (const WorldLayer& layer : layers_) {
        if (layer.name == "road") {
            const Rectangle bounds = Bounds();
            const float pathY = std::clamp(bounds.height * 0.44f, 96.0f, bounds.height - 120.0f);
            const float pathX = std::clamp(bounds.width * 0.43f, 96.0f, bounds.width - 120.0f);
            const Rectangle horizontalRoad {0.0f, pathY, bounds.width, static_cast<float>(tileSize_ * 2)};
            const Rectangle verticalRoad {pathX, 0.0f, static_cast<float>(tileSize_ * 2), bounds.height};
            const Rectangle clippedHorizontal = ClampRectToBounds(horizontalRoad, clippedView);
            const Rectangle clippedVertical = ClampRectToBounds(verticalRoad, clippedView);
            bool drewTexture = false;
            if (LayerHasUsableImage(layer)) {
                const AtlasRef ref = ParseAtlasRef(layer.asset);
                if (ref.valid) {
                    Texture2D* texture = EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_);
                    if (texture != nullptr) {
                        if (HasArea(clippedHorizontal)) DrawTiledTexture(*texture, ref.source, clippedHorizontal, layer.tint);
                        if (HasArea(clippedVertical)) DrawTiledTexture(*texture, ref.source, clippedVertical, layer.tint);
                        drewTexture = true;
                    }
                }
            }
            if (!drewTexture && !HasStampsOnLayer(stamps_, "road")) {
                if (HasArea(clippedHorizontal)) DrawRectangleRec(clippedHorizontal, Fade(PathColor(), 0.90f));
                if (HasArea(clippedVertical)) DrawRectangleRec(clippedVertical, Fade(PathColor(), 0.90f));
            }
            for (const WorldStamp& stamp : stamps_) {
                if (stamp.layer != "road") {
                    continue;
                }
                const Rectangle dest {
                    static_cast<float>(stamp.x * tileSize_),
                    static_cast<float>(stamp.y * tileSize_),
                    static_cast<float>(tileSize_),
                    static_cast<float>(tileSize_)
                };
                if (!CheckCollisionRecs(dest, clippedView)) {
                    continue;
                }
                DrawSpriteRef(stamp.asset, dest, Vector2 {}, 0.0f, WHITE);
            }
        } else if (layer.asset.find("trees") != std::string::npos) {
            const int decorationCount = kFastMobileRender
                ? std::max(8, (width_ * height_) / 140)
                : std::max(18, (width_ * height_) / 60);
            const int maxX = std::max(160, width_ * tileSize_ - 160);
            const int maxY = std::max(180, height_ * tileSize_ - 180);
            for (int i = 0; i < decorationCount; ++i) {
                const float x = 80.0f + static_cast<float>((i * 173) % maxX);
                const float y = 90.0f + static_cast<float>((i * 127) % maxY);
                if (x < clippedView.x - 64.0f || x > clippedView.x + clippedView.width + 64.0f ||
                    y < clippedView.y - 64.0f || y > clippedView.y + clippedView.height + 64.0f) {
                    continue;
                }
                DrawCircleV(Vector2 {x, y}, 28.0f, Fade(CanopyColor(), layer.tint.a / 255.0f));
                DrawCircleV(Vector2 {x - 12.0f, y + 10.0f}, 22.0f, Fade(CanopyShade(), layer.tint.a / 255.0f));
                DrawRectangleRounded(Rectangle {x - 6.0f, y + 22.0f, 12.0f, 24.0f}, 0.4f, 4, Color {95, 70, 45, 255});
            }
        }
    }

    for (const WorldStamp& stamp : stamps_) {
        if (stamp.layer == "ground" || stamp.layer == "road") {
            continue;
        }
        const Rectangle dest {
            static_cast<float>(stamp.x * tileSize_),
            static_cast<float>(stamp.y * tileSize_),
            static_cast<float>(tileSize_),
            static_cast<float>(tileSize_)
        };
        if (!CheckCollisionRecs(dest, clippedView)) {
            continue;
        }
        DrawSpriteRef(stamp.asset, dest, Vector2 {}, 0.0f, WHITE);
    }

    if (!kFastMobileRender) {
        for (const Rectangle& area : blockedAreas_) {
            if (!CheckCollisionRecs(area, clippedView)) {
                continue;
            }
            DrawRectangleRounded(area, 0.12f, 6, Color {111, 83, 61, 255});
            DrawRectangleRounded(
                Rectangle {area.x + 10.0f, area.y + 10.0f, area.width - 20.0f, area.height - 20.0f},
                0.1f,
                6,
                Color {155, 122, 81, 255}
            );
        }
    }
    if (!kFastMobileRender) {
        for (const WorldObject& object : objects_) {
            if (object.kind == "trigger") {
                if (!CheckCollisionRecs(object.bounds, clippedView)) {
                    continue;
                }
                DrawRectangleRoundedLinesEx(object.bounds, 0.12f, 8, 2.0f, Fade(Color {245, 226, 120, 255}, 0.65f));
            }
        }
    }
}

std::optional<std::string> World::property(const std::string& key) const {
    auto it = properties_.find(key);
    if (it == properties_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> World::objectProperty(const WorldObject& object, const std::string& key) const {
    auto it = object.properties.find(key);
    if (it == object.properties.end()) {
        return std::nullopt;
    }
    return it->second;
}

const WorldObject* World::findObjectById(const std::string& id) const {
    for (const WorldObject& object : objects_) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

const WorldObject* World::regionAt(Vector2 worldPosition) const {
    for (const WorldObject& object : objects_) {
        if (object.kind != "region") {
            continue;
        }
        if (worldPosition.x >= object.bounds.x &&
            worldPosition.y >= object.bounds.y &&
            worldPosition.x <= object.bounds.x + object.bounds.width &&
            worldPosition.y <= object.bounds.y + object.bounds.height) {
            return &object;
        }
    }
    return nullptr;
}

Vector2 World::objectCenter(const WorldObject& object) const {
    return Vector2 {
        object.bounds.x + (object.bounds.width * 0.5f),
        object.bounds.y + (object.bounds.height * 0.5f)
    };
}

int World::objectZLayer(const WorldObject& object) const {
    const std::optional<std::string> value = objectProperty(object, "z");
    if (!value.has_value()) {
        return 0;
    }
    try {
        return std::stoi(*value);
    } catch (...) {
        return 0;
    }
}

Vector2 World::objectPivot(const WorldObject& object) const {
    const std::optional<std::string> value = objectProperty(object, "pivot");
    if (!value.has_value()) {
        return Vector2 {0.0f, 0.0f};
    }
    float x = 0.0f;
    float y = 0.0f;
    if (!ParsePair(*value, x, y)) {
        return Vector2 {0.0f, 0.0f};
    }
    return Vector2 {x * object.bounds.width, y * object.bounds.height};
}

float World::objectFacing(const WorldObject& object) const {
    const std::optional<std::string> value = objectProperty(object, "facing");
    if (!value.has_value()) {
        return 0.0f;
    }

    if (*value == "north") return 0.0f;
    if (*value == "east") return 90.0f;
    if (*value == "south") return 180.0f;
    if (*value == "west") return 270.0f;
    try {
        return std::stof(*value);
    } catch (...) {
        return 0.0f;
    }
}

bool World::DrawSpriteRef(const std::string& spriteRef, Rectangle dest, Vector2 origin, float rotation, Color tint) const {
    const AtlasRef ref = ParseAtlasRef(spriteRef);
    if (!ref.valid) {
        return false;
    }
    Texture2D* texture = EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_);
    if (!texture) {
        return false;
    }
    DrawTexturePro(*texture, ref.source, dest, origin, rotation, tint);
    return true;
}

bool World::DrawObjectSprite(const WorldObject& object, Color tint) const {
    const std::optional<std::string> sprite = objectProperty(object, "sprite");
    if (!sprite.has_value() || sprite->empty()) {
        return false;
    }
    return DrawSpriteRef(*sprite, object.bounds, objectPivot(object), objectFacing(object), tint);
}

bool World::PreloadReferencedTextures() const {
    bool ready = true;
    auto preloadRef = [&](const std::string& spriteRef) {
        if (spriteRef.empty()) {
            return;
        }
        const AtlasRef ref = ParseAtlasRef(spriteRef);
        if (!ref.valid) {
            return;
        }
        if (EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_) == nullptr) {
            ready = false;
        }
    };

    for (const WorldLayer& layer : layers_) {
        if (layer.kind == "image") {
            preloadRef(layer.asset);
        }
    }
    for (const WorldStamp& stamp : stamps_) {
        preloadRef(stamp.asset);
    }
    for (const WorldObject& object : objects_) {
        const auto it = object.properties.find("sprite");
        if (it != object.properties.end()) {
            preloadRef(it->second);
        }
    }
    for (const auto& [key, value] : properties_) {
        if (key.rfind("player_sprite_", 0) == 0) {
            preloadRef(value);
        }
    }
    return ready;
}

std::vector<std::string> World::referencedMapImageFiles() const {
    std::vector<std::string> files;
    auto addRef = [&](const std::string& ref) {
        if (ref.empty()) return;
        const AtlasRef atlasRef = ParseAtlasRef(ref);
        if (atlasRef.valid && atlasRef.domain != AssetDomain::Map) {
            return;
        }
        const std::string file = atlasRef.valid ? atlasRef.file : ref;
        if (file.empty()) return;
        if (std::find(files.begin(), files.end(), file) == files.end()) {
            files.push_back(file);
        }
    };

    for (const WorldLayer& layer : layers_) {
        if (layer.kind == "image") {
            addRef(layer.asset);
        }
    }
    for (const WorldObject& object : objects_) {
        auto it = object.properties.find("sprite");
        if (it != object.properties.end()) {
            addRef(it->second);
        }
    }
    for (const WorldStamp& stamp : stamps_) {
        addRef(stamp.asset);
    }
    return files;
}

std::vector<std::string> World::referencedCharacterImageFiles() const {
    std::vector<std::string> files;
    auto addRef = [&](const std::string& ref) {
        if (ref.empty()) return;
        const AtlasRef atlasRef = ParseAtlasRef(ref);
        if (atlasRef.valid && atlasRef.domain != AssetDomain::Character) {
            return;
        }
        const std::string file = atlasRef.valid ? atlasRef.file : ref;
        if (file.empty()) return;
        if (std::find(files.begin(), files.end(), file) == files.end()) {
            files.push_back(file);
        }
    };

    for (const auto& [key, value] : properties_) {
        if (key.rfind("player_sprite_", 0) == 0) {
            addRef(value);
        }
    }
    for (const WorldLayer& layer : layers_) {
        if (layer.kind == "image") {
            addRef(layer.asset);
        }
    }
    for (const WorldObject& object : objects_) {
        auto it = object.properties.find("sprite");
        if (it != object.properties.end()) {
            addRef(it->second);
        }
    }
    for (const WorldStamp& stamp : stamps_) {
        addRef(stamp.asset);
    }

    return files;
}

void World::LoadAtlasMetadataForRef(const std::string& assetRef) {
    const AtlasRef ref = ParseAtlasRef(assetRef);
    if (!ref.valid || ref.domain != AssetDomain::Map || ref.file.empty()) {
        return;
    }

    const std::filesystem::path metaPath = mapAssetRoot_ / (std::filesystem::path(ref.file).stem().string() + ".atlas");
    if (!std::filesystem::exists(metaPath)) {
        return;
    }

    std::ifstream in(metaPath);
    if (!in.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const size_t sep = line.find('=');
        if (sep == std::string::npos) continue;
        if (line.substr(0, sep) != "tile") continue;

        const std::vector<std::string> parts = SplitEscaped(line.substr(sep + 1), ',');
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

        WorldAtlasTileMeta meta;
        for (size_t i = 4; i < parts.size(); ++i) {
            const auto prop = SplitPropertyAssignment(parts[i], ':');
            if (!prop.has_value()) continue;
            if (prop->first == "collision" && prop->second == "block") {
                meta.blocksMovement = true;
            } else if (prop->first == "tag") {
                meta.tag = prop->second;
            }
        }
        atlasTileMeta_[AtlasMetaKey(ref.file, x, y, w, h)] = std::move(meta);
    }
}

std::optional<WorldAtlasTileMeta> World::metaForAsset(const std::string& assetRef) const {
    const AtlasRef ref = ParseAtlasRef(assetRef);
    if (!ref.valid || ref.file.empty()) {
        return std::nullopt;
    }
    const std::string key = AtlasMetaKey(ref.file,
                                         static_cast<int>(ref.source.x),
                                         static_cast<int>(ref.source.y),
                                         static_cast<int>(ref.source.width),
                                         static_cast<int>(ref.source.height));
    auto it = atlasTileMeta_.find(key);
    if (it == atlasTileMeta_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void World::UnloadTextures() {
    for (auto& [name, texture] : textures_) {
        if (texture.id > 0) {
            UnloadTexture(texture);
        }
        (void)name;
    }
    textures_.clear();
}
