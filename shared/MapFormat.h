#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nganu {
namespace MapFormat {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Layer {
    std::string name;
    std::string kind;
    std::string asset;
    std::string tint {"#FFFFFFFF"};
    float parallax = 1.0f;
};

struct Stamp {
    std::string layer;
    int x = 0;
    int y = 0;
    std::string asset;
};

struct Object {
    std::string kind;
    std::string id;
    Rect bounds;
    std::unordered_map<std::string, std::string> properties;
};

struct Document {
    std::string mapId;
    std::string worldName;
    int tileSize = 32;
    int width = 0;
    int height = 0;
    Vec2 spawn {96.0f, 96.0f};
    std::unordered_map<std::string, std::string> properties;
    std::vector<Layer> layers;
    std::vector<Stamp> stamps;
    std::vector<Object> objects;
};

enum class AssetDomain {
    Map,
    Character
};

struct AtlasRef {
    AssetDomain domain = AssetDomain::Map;
    std::string file;
    Rect source;
    bool valid = false;
};

struct AtlasTileMeta {
    std::string collision;
    std::string tag;
    bool blocksMovement = false;
    bool hasCollider = false;
    Rect collider;
};

struct ParseResult {
    Document document;
    bool ok = false;
    int line = 0;
    std::string error;
};

struct TmxWriteOptions {
    std::filesystem::path mapImageRoot;
    std::filesystem::path characterImageRoot;
    std::string mapImageSourcePrefix {"../map_images/"};
    std::string characterImageSourcePrefix {"../characters/"};
};

std::vector<std::string> SplitEscaped(const std::string& value, char delim);
std::optional<std::pair<std::string, std::string>> SplitPropertyAssignment(const std::string& value, char delim);
std::string EscapeValue(const std::string& value);

bool ParseFloatStrict(const std::string& value, float& out);
bool ParseIntStrict(const std::string& value, int& out);
bool ParseRectStrict(const std::string& value, Rect& out);
bool ParseVec2Strict(const std::string& value, Vec2& out);

ParseResult ParseDocument(const std::string& text);
std::string SerializeDocumentAsTmx(const Document& document, const TmxWriteOptions& options = {});
std::optional<AtlasRef> ParseAtlasRef(const std::string& asset);
std::string AssetFileName(const std::string& ref);
AssetDomain AssetDomainForRef(const std::string& ref, AssetDomain fallbackDomain);
std::string AtlasMetaKey(const std::string& file, int x, int y, int w, int h);
std::unordered_map<std::string, AtlasTileMeta> ParseAtlasMetadata(const std::string& text, const std::string& fileName);

void AddUniqueAssetRef(std::vector<std::string>& refs, const std::string& file);
void CollectReferencedAssets(const Document& document,
                             std::vector<std::string>& mapImageRefs,
                             std::vector<std::string>& characterImageRefs);

}
}
