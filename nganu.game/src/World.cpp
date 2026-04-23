#include "World.h"

#include "shared/MapFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>

namespace {
Color GrassColor() { return Color {84, 148, 92, 255}; }
Color PathColor() { return Color {182, 157, 110, 255}; }

#if defined(PLATFORM_ANDROID)
constexpr bool kFastMobileRender = true;
#else
constexpr bool kFastMobileRender = false;
#endif

// GLES drivers can leave 1-pixel cracks between independently rasterized tile quads.
constexpr float kTileSeamOverlap = 0.5f;
constexpr int kSpatialChunkTiles = 16;

struct AtlasRef {
    AssetDomain domain = AssetDomain::Map;
    std::string file;
    Rectangle source {};
    bool valid = false;
};

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
    const auto parsed = Nganu::MapFormat::ParseAtlasRef(asset);
    if (!parsed.has_value()) {
        return ref;
    }
    ref.domain = parsed->domain == Nganu::MapFormat::AssetDomain::Character ? AssetDomain::Character : AssetDomain::Map;
    ref.file = parsed->file;
    ref.source = Rectangle {parsed->source.x, parsed->source.y, parsed->source.width, parsed->source.height};
    ref.valid = true;
    return ref;
}

std::uint64_t SpatialBucketKey(int chunkX, int chunkY) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint32_t>(chunkY);
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

    const unsigned char pngHeader[8] {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() < sizeof(pngHeader) || !std::equal(std::begin(pngHeader), std::end(pngHeader), bytes.begin())) {
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
    if (texture.id <= 0 || texture.width <= 0 || texture.height <= 0) {
        std::filesystem::remove(path, ec);
        return nullptr;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);

    textureIt = textures.emplace(cacheKey, texture).first;
    return &textureIt->second;
}

Rectangle AddTileSeamOverlap(Rectangle dest) {
    if (dest.width <= 0.0f || dest.height <= 0.0f) {
        return dest;
    }
    dest.width += kTileSeamOverlap;
    dest.height += kTileSeamOverlap;
    return dest;
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
            DrawTexturePro(texture, source, AddTileSeamOverlap(dest), Vector2 {}, 0.0f, tint);
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

std::optional<std::string> PropertyValue(const std::unordered_map<std::string, std::string>& properties, const std::string& key) {
    auto it = properties.find(key);
    if (it == properties.end()) {
        return std::nullopt;
    }
    return it->second;
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

std::optional<Rectangle> PropertyCollider(const WorldObject& object) {
    const std::optional<std::string> collider = PropertyValue(object.properties, "collider");
    const std::optional<std::string> hitbox = PropertyValue(object.properties, "hitbox");
    const std::string* value = collider.has_value() ? &*collider : (hitbox.has_value() ? &*hitbox : nullptr);
    if (value == nullptr) {
        return std::nullopt;
    }

    Rectangle rect {};
    if (!ParseLocalRect(*value, rect)) {
        return std::nullopt;
    }
    return rect;
}

bool DrawSpriteRefInternal(std::unordered_map<std::string, Texture2D>& textures,
                           const std::filesystem::path& mapAssetRoot,
                           const std::filesystem::path& characterAssetRoot,
                           const std::string& spriteRef,
                           Rectangle dest,
                           Vector2 origin,
                           float rotation,
                           Color tint,
                           bool addTileSeamOverlap) {
    const AtlasRef ref = ParseAtlasRef(spriteRef);
    if (!ref.valid) {
        return false;
    }
    Texture2D* texture = EnsureTextureLoaded(textures, ref, mapAssetRoot, characterAssetRoot);
    if (!texture) {
        return false;
    }
    const Rectangle drawDest = addTileSeamOverlap ? AddTileSeamOverlap(dest) : dest;
    DrawTexturePro(*texture, ref.source, drawDest, origin, rotation, tint);
    return true;
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
    tileSize_ = 32;
    width_ = 16;
    height_ = 12;
    mapId_.clear();
    worldName_.clear();
    spawnPoint_ = Vector2 {96.0f, 96.0f};
    properties_.clear();
    layers_.clear();
    stamps_.clear();
    objects_.clear();

    atlasTileMeta_.clear();
    RebuildSpatialIndex();
}

bool World::LoadFromMapAsset(const std::string& rawAsset) {
    const Nganu::MapFormat::ParseResult parsed = Nganu::MapFormat::ParseDocument(rawAsset);
    if (!parsed.ok) {
        return false;
    }
    const Nganu::MapFormat::Document& document = parsed.document;

    std::unordered_map<std::string, std::string> nextProperties;
    std::vector<WorldLayer> nextLayers;
    std::vector<WorldStamp> nextStamps;
    std::vector<WorldObject> nextObjects;

    nextProperties = document.properties;
    nextLayers.reserve(document.layers.size());
    for (const Nganu::MapFormat::Layer& source : document.layers) {
        WorldLayer layer;
        layer.name = source.name;
        layer.kind = source.kind;
        layer.asset = source.asset;
        layer.tint = ParseHexColor(source.tint, RAYWHITE);
        layer.parallax = source.parallax;
        nextLayers.push_back(std::move(layer));
    }
    nextObjects.reserve(document.objects.size());
    for (const Nganu::MapFormat::Object& source : document.objects) {
        WorldObject object;
        object.kind = source.kind;
        object.id = source.id;
        object.bounds = Rectangle {source.bounds.x, source.bounds.y, source.bounds.width, source.bounds.height};
        object.properties = source.properties;
        nextObjects.push_back(std::move(object));
    }
    nextStamps.reserve(document.stamps.size());
    for (const Nganu::MapFormat::Stamp& source : document.stamps) {
        nextStamps.push_back(WorldStamp {source.layer, source.x, source.y, source.asset});
    }
    UnloadTextures();
    tileSize_ = document.tileSize;
    width_ = document.width;
    height_ = document.height;
    mapId_ = document.mapId;
    worldName_ = document.worldName;
    spawnPoint_ = Vector2 {document.spawn.x, document.spawn.y};
    properties_ = std::move(nextProperties);
    layers_ = std::move(nextLayers);
    stamps_ = std::move(nextStamps);
    objects_ = std::move(nextObjects);
    RebuildSpatialIndex();
    ReloadAtlasMetadata();
    return true;
}

void World::RebuildSpatialIndex() {
    stampBuckets_.clear();
    objectBuckets_.clear();
    hasRoadStamps_ = false;
    stampQueryMarks_.assign(stamps_.size(), 0);
    stampQueryToken_ = 1;
    objectQueryMarks_.assign(objects_.size(), 0);
    objectQueryToken_ = 1;

    const float chunkWorldSize = static_cast<float>(std::max(1, tileSize_) * kSpatialChunkTiles);
    for (size_t i = 0; i < stamps_.size(); ++i) {
        const WorldStamp& stamp = stamps_[i];
        hasRoadStamps_ = hasRoadStamps_ || stamp.layer == "road";
        const Rectangle bounds = stampBounds(stamp);
        if (!HasArea(bounds)) {
            continue;
        }

        const int minChunkX = static_cast<int>(std::floor(bounds.x / chunkWorldSize));
        const int minChunkY = static_cast<int>(std::floor(bounds.y / chunkWorldSize));
        const int maxChunkX = static_cast<int>(std::floor((bounds.x + bounds.width) / chunkWorldSize));
        const int maxChunkY = static_cast<int>(std::floor((bounds.y + bounds.height) / chunkWorldSize));
        for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
            for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                stampBuckets_[SpatialBucketKey(chunkX, chunkY)].push_back(i);
            }
        }
    }

    for (size_t i = 0; i < objects_.size(); ++i) {
        const Rectangle bounds = objects_[i].bounds;
        if (!HasArea(bounds)) {
            continue;
        }
        const int minChunkX = static_cast<int>(std::floor(bounds.x / chunkWorldSize));
        const int minChunkY = static_cast<int>(std::floor(bounds.y / chunkWorldSize));
        const int maxChunkX = static_cast<int>(std::floor((bounds.x + bounds.width) / chunkWorldSize));
        const int maxChunkY = static_cast<int>(std::floor((bounds.y + bounds.height) / chunkWorldSize));
        for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
            for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                objectBuckets_[SpatialBucketKey(chunkX, chunkY)].push_back(i);
            }
        }
    }
}

void World::CollectVisibleStampIndices(Rectangle visibleArea, std::vector<size_t>& out) const {
    out.clear();
    if (stampBuckets_.empty() || tileSize_ <= 0 || !HasArea(visibleArea)) {
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
    const int minChunkX = static_cast<int>(std::floor(visibleArea.x / chunkWorldSize));
    const int minChunkY = static_cast<int>(std::floor(visibleArea.y / chunkWorldSize));
    const int maxChunkX = static_cast<int>(std::floor((visibleArea.x + visibleArea.width) / chunkWorldSize));
    const int maxChunkY = static_cast<int>(std::floor((visibleArea.y + visibleArea.height) / chunkWorldSize));

    for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            auto it = stampBuckets_.find(SpatialBucketKey(chunkX, chunkY));
            if (it == stampBuckets_.end()) {
                continue;
            }
            for (size_t stampIndex : it->second) {
                if (stampIndex >= stamps_.size() || stampQueryMarks_[stampIndex] == stampQueryToken_) {
                    continue;
                }
                stampQueryMarks_[stampIndex] = stampQueryToken_;
                if (CheckCollisionRecs(stampBounds(stamps_[stampIndex]), visibleArea)) {
                    out.push_back(stampIndex);
                }
            }
        }
    }
}

void World::CollectVisibleObjects(Rectangle visibleArea, std::vector<const WorldObject*>& out) const {
    out.clear();
    if (objectBuckets_.empty() || tileSize_ <= 0 || !HasArea(visibleArea)) {
        return;
    }
    if (objectQueryMarks_.size() != objects_.size()) {
        objectQueryMarks_.assign(objects_.size(), 0);
        objectQueryToken_ = 1;
    }
    ++objectQueryToken_;
    if (objectQueryToken_ == 0) {
        std::fill(objectQueryMarks_.begin(), objectQueryMarks_.end(), 0);
        objectQueryToken_ = 1;
    }

    const float chunkWorldSize = static_cast<float>(tileSize_ * kSpatialChunkTiles);
    const int minChunkX = static_cast<int>(std::floor(visibleArea.x / chunkWorldSize));
    const int minChunkY = static_cast<int>(std::floor(visibleArea.y / chunkWorldSize));
    const int maxChunkX = static_cast<int>(std::floor((visibleArea.x + visibleArea.width) / chunkWorldSize));
    const int maxChunkY = static_cast<int>(std::floor((visibleArea.y + visibleArea.height) / chunkWorldSize));

    for (int chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY) {
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            auto it = objectBuckets_.find(SpatialBucketKey(chunkX, chunkY));
            if (it == objectBuckets_.end()) {
                continue;
            }
            for (size_t objectIndex : it->second) {
                if (objectIndex >= objects_.size() || objectQueryMarks_[objectIndex] == objectQueryToken_) {
                    continue;
                }
                objectQueryMarks_[objectIndex] = objectQueryToken_;
                const WorldObject& object = objects_[objectIndex];
                if (CheckCollisionRecs(object.bounds, visibleArea)) {
                    out.push_back(&object);
                }
            }
        }
    }
}

Rectangle World::stampBounds(const WorldStamp& stamp) const {
    const AtlasRef ref = ParseAtlasRef(stamp.asset);
    const float width = ref.valid ? std::max(1.0f, ref.source.width) : static_cast<float>(tileSize_);
    const float height = ref.valid ? std::max(1.0f, ref.source.height) : static_cast<float>(tileSize_);
    return Rectangle {
        static_cast<float>(stamp.x * tileSize_) + ((static_cast<float>(tileSize_) - width) * 0.5f),
        static_cast<float>((stamp.y + 1) * tileSize_) - height,
        width,
        height
    };
}

Rectangle World::stampCollisionRect(const WorldStamp& stamp, const WorldAtlasTileMeta& meta) const {
    const Rectangle bounds = stampBounds(stamp);
    if (!meta.hasCollider) {
        return bounds;
    }

    const AtlasRef ref = ParseAtlasRef(stamp.asset);
    const float sourceWidth = ref.valid ? std::max(1.0f, ref.source.width) : static_cast<float>(tileSize_);
    const float sourceHeight = ref.valid ? std::max(1.0f, ref.source.height) : static_cast<float>(tileSize_);
    return Rectangle {
        bounds.x + (meta.collider.x / sourceWidth) * bounds.width,
        bounds.y + (meta.collider.y / sourceHeight) * bounds.height,
        (meta.collider.width / sourceWidth) * bounds.width,
        (meta.collider.height / sourceHeight) * bounds.height
    };
}

std::optional<Rectangle> World::objectCollisionRect(const WorldObject& object) const {
    const std::optional<std::string> collision = PropertyValue(object.properties, "collision");
    if (collision.has_value() && CollisionDisablesMovementBlock(*collision)) {
        return std::nullopt;
    }

    std::optional<WorldAtlasTileMeta> spriteMeta;
    const std::optional<std::string> sprite = PropertyValue(object.properties, "sprite");
    if (sprite.has_value()) {
        spriteMeta = metaForAsset(*sprite);
    }

    const bool blocks = collision.has_value()
        ? CollisionBlocksMovement(*collision)
        : (spriteMeta.has_value() && spriteMeta->blocksMovement);
    if (!blocks || !HasArea(object.bounds)) {
        return std::nullopt;
    }

    if (const std::optional<Rectangle> localCollider = PropertyCollider(object)) {
        return Rectangle {
            object.bounds.x + localCollider->x,
            object.bounds.y + localCollider->y,
            localCollider->width,
            localCollider->height
        };
    }

    if (spriteMeta.has_value() && spriteMeta->hasCollider && sprite.has_value()) {
        const AtlasRef ref = ParseAtlasRef(*sprite);
        const float sourceWidth = ref.valid ? std::max(1.0f, ref.source.width) : std::max(1.0f, object.bounds.width);
        const float sourceHeight = ref.valid ? std::max(1.0f, ref.source.height) : std::max(1.0f, object.bounds.height);
        return Rectangle {
            object.bounds.x + (spriteMeta->collider.x / sourceWidth) * object.bounds.width,
            object.bounds.y + (spriteMeta->collider.y / sourceHeight) * object.bounds.height,
            (spriteMeta->collider.width / sourceWidth) * object.bounds.width,
            (spriteMeta->collider.height / sourceHeight) * object.bounds.height
        };
    }

    return object.bounds;
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

    std::vector<size_t> candidateStamps;
    CollectVisibleStampIndices(body, candidateStamps);
    for (size_t stampIndex : candidateStamps) {
        const WorldStamp& stamp = stamps_[stampIndex];
        const auto meta = metaForAsset(stamp.asset);
        if (!meta.has_value() || !meta->blocksMovement) {
            continue;
        }
        const Rectangle stampRect = stampCollisionRect(stamp, *meta);
        if (CheckCollisionRecs(body, stampRect)) {
            return false;
        }
    }

    std::vector<const WorldObject*> candidateObjects;
    CollectVisibleObjects(body, candidateObjects);
    for (const WorldObject* object : candidateObjects) {
        const std::optional<Rectangle> collider = objectCollisionRect(*object);
        if (collider.has_value() && CheckCollisionRecs(body, *collider)) {
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

    DrawRectangleRec(clippedView, GrassColor());
}

void World::DrawDecorations(Rectangle visibleArea) const {
    const Rectangle clippedView = ClampRectToBounds(visibleArea, Bounds());
    if (!HasArea(clippedView)) {
        return;
    }

    std::vector<size_t> visibleStamps;
    CollectVisibleStampIndices(clippedView, visibleStamps);

    auto drawStamp = [&](const WorldStamp& stamp) {
        const Rectangle dest = stampBounds(stamp);
        if (CheckCollisionRecs(dest, clippedView)) {
            DrawTileSpriteRef(stamp.asset, dest, WHITE);
        }
    };

    for (const WorldLayer& layer : layers_) {
        const bool hasLayerStamps = layer.name == "road" && hasRoadStamps_;
        if (layer.kind == "color") {
            DrawRectangleRec(clippedView, Fade(layer.tint, 0.18f));
        } else if (layer.name == "road" && !hasLayerStamps) {
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
            if (!drewTexture && !hasRoadStamps_) {
                if (HasArea(clippedHorizontal)) DrawRectangleRec(clippedHorizontal, Fade(PathColor(), 0.90f));
                if (HasArea(clippedVertical)) DrawRectangleRec(clippedVertical, Fade(PathColor(), 0.90f));
            }
        } else if (LayerHasUsableImage(layer)) {
            const AtlasRef ref = ParseAtlasRef(layer.asset);
            if (ref.valid) {
                Texture2D* texture = EnsureTextureLoaded(textures_, ref, mapAssetRoot_, characterAssetRoot_);
                if (texture != nullptr) {
                    DrawTiledTexture(*texture, ref.source, clippedView, layer.tint);
                }
            }
        }

        for (size_t stampIndex : visibleStamps) {
            const WorldStamp& stamp = stamps_[stampIndex];
            if (stamp.layer == layer.name) {
                drawStamp(stamp);
            }
        }
    }

    for (size_t stampIndex : visibleStamps) {
        const WorldStamp& stamp = stamps_[stampIndex];
        const bool knownLayer = std::any_of(layers_.begin(), layers_.end(), [&](const WorldLayer& layer) {
            return layer.name == stamp.layer;
        });
        if (knownLayer) {
            continue;
        }
        drawStamp(stamp);
    }

    if (!kFastMobileRender) {
        std::vector<const WorldObject*> visibleObjects;
        CollectVisibleObjects(clippedView, visibleObjects);
        for (const WorldObject* object : visibleObjects) {
            if (object->kind == "trigger") {
                DrawRectangleRoundedLinesEx(object->bounds, 0.12f, 8, 2.0f, Fade(Color {245, 226, 120, 255}, 0.65f));
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

    if (*value == "north" || *value == "east" || *value == "south" || *value == "west") {
        return 0.0f;
    }
    try {
        return std::stof(*value);
    } catch (...) {
        return 0.0f;
    }
}

bool World::DrawSpriteRef(const std::string& spriteRef, Rectangle dest, Vector2 origin, float rotation, Color tint) const {
    return DrawSpriteRefInternal(textures_, mapAssetRoot_, characterAssetRoot_, spriteRef, dest, origin, rotation, tint, false);
}

bool World::DrawTileSpriteRef(const std::string& spriteRef, Rectangle dest, Color tint) const {
    return DrawSpriteRefInternal(textures_, mapAssetRoot_, characterAssetRoot_, spriteRef, dest, Vector2 {}, 0.0f, tint, true);
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

void World::ReloadAtlasMetadata() {
    atlasTileMeta_.clear();
    for (const WorldLayer& layer : layers_) {
        if (layer.kind == "image") {
            LoadAtlasMetadataForRef(layer.asset);
        }
    }
    for (const WorldStamp& stamp : stamps_) {
        LoadAtlasMetadataForRef(stamp.asset);
    }
    for (const WorldObject& object : objects_) {
        const auto it = object.properties.find("sprite");
        if (it != object.properties.end()) {
            LoadAtlasMetadataForRef(it->second);
        }
    }
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

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsedMetadata = Nganu::MapFormat::ParseAtlasMetadata(text, ref.file);
    for (const auto& [key, source] : parsedMetadata) {
        WorldAtlasTileMeta meta;
        meta.collision = source.collision;
        meta.blocksMovement = source.blocksMovement;
        meta.hasCollider = source.hasCollider;
        meta.collider = Rectangle {source.collider.x, source.collider.y, source.collider.width, source.collider.height};
        meta.tag = source.tag;
        atlasTileMeta_[key] = std::move(meta);
    }
}

std::optional<WorldAtlasTileMeta> World::metaForAsset(const std::string& assetRef) const {
    const AtlasRef ref = ParseAtlasRef(assetRef);
    if (!ref.valid || ref.file.empty()) {
        return std::nullopt;
    }
    const std::string key = Nganu::MapFormat::AtlasMetaKey(ref.file,
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
