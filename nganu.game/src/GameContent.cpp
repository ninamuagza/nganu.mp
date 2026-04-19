#include "Game.h"
#include "shared/ContentIntegrity.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

namespace {
int HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

bool HasPngHeader(const unsigned char* data, size_t size) {
    if (data == nullptr || size < 8) {
        return false;
    }
    const unsigned char pngHeader[8] {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return std::equal(pngHeader, pngHeader + 8, data);
}
}

void Game::ApplyManifest(const std::string& rawManifest) {
    ContentManifest next;
    std::istringstream stream(rawManifest);
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
        if (key == "server_name") {
            next.serverName = value;
        } else if (key == "content_revision") {
            next.revision = value;
        } else if (key == "world_name") {
            next.worldName = value;
        } else if (key == "map_id") {
            next.mapId = value;
        } else if (key == "asset") {
            next.assets.push_back(value);
        }
    }

    next.valid = !next.serverName.empty() || !next.revision.empty() || !next.worldName.empty();
    manifest_ = std::move(next);

    if (manifest_.valid) {
        const std::string serverName = manifest_.serverName.empty() ? "server" : manifest_.serverName;
        AddChatLine("[System] Content manifest ready from " + serverName);
        if (!manifest_.revision.empty()) {
            AddChatLine("[System] Content revision " + manifest_.revision);
        }
        AddChatLine("[System] Assets listed: " + std::to_string(manifest_.assets.size()));
    }
}

std::optional<AssetBlob> Game::ParseAssetBlob(const std::string& rawBlob) const {
    AssetBlob blob;
    std::istringstream stream(rawBlob);
    std::string line;
    bool inContent = false;
    while (std::getline(stream, line)) {
        if (!inContent) {
            if (line == "---") {
                inContent = true;
                continue;
            }

            const size_t sep = line.find('=');
            if (sep == std::string::npos) {
                continue;
            }

            const std::string key = line.substr(0, sep);
            const std::string value = line.substr(sep + 1);
            if (key == "key") {
                blob.key = value;
            } else if (key == "kind") {
                blob.kind = value;
            } else if (key == "revision") {
                blob.revision = value;
            } else if (key == "encoding") {
                blob.encoding = value;
            } else if (key == "checksum") {
                blob.checksum = value;
            } else if (key == "content_size") {
                try {
                    blob.contentSize = static_cast<size_t>(std::stoull(value));
                } catch (...) {
                    return std::nullopt;
                }
            } else if (key == "chunk_index") {
                try {
                    blob.chunkIndex = static_cast<size_t>(std::stoull(value));
                } catch (...) {
                    return std::nullopt;
                }
            } else if (key == "chunk_total") {
                try {
                    blob.chunkTotal = static_cast<size_t>(std::stoull(value));
                } catch (...) {
                    return std::nullopt;
                }
            }
        } else {
            if (!blob.content.empty()) {
                blob.content.push_back('\n');
            }
            blob.content += line;
        }
    }

    if (blob.key.empty() || blob.kind.empty() || blob.checksum.empty() ||
        blob.chunkTotal == 0 || blob.chunkTotal > Protocol::kMaxAssetChunks || blob.chunkIndex >= blob.chunkTotal) {
        return std::nullopt;
    }

    return blob;
}

std::optional<AssetBlob> Game::AccumulateAssetBlobChunk(const AssetBlob& chunk) {
    const std::string assemblyKey = chunk.key + "|" + chunk.revision;
    AssetBlobAssembly& assembly = pendingAssetAssemblies_[assemblyKey];
    const bool init = assembly.chunks.empty();
    if (init) {
        assembly.key = chunk.key;
        assembly.kind = chunk.kind;
        assembly.revision = chunk.revision;
        assembly.encoding = chunk.encoding;
        assembly.checksum = chunk.checksum;
        assembly.contentSize = chunk.contentSize;
        assembly.chunks.assign(chunk.chunkTotal, std::string {});
        assembly.received.assign(chunk.chunkTotal, false);
        assembly.receivedCount = 0;
    } else if (assembly.kind != chunk.kind ||
               assembly.revision != chunk.revision ||
               assembly.encoding != chunk.encoding ||
               assembly.checksum != chunk.checksum ||
               assembly.contentSize != chunk.contentSize ||
               assembly.chunks.size() != chunk.chunkTotal) {
        pendingAssetAssemblies_.erase(assemblyKey);
        return std::nullopt;
    }

    if (!assembly.received[chunk.chunkIndex]) {
        assembly.received[chunk.chunkIndex] = true;
        assembly.chunks[chunk.chunkIndex] = chunk.content;
        ++assembly.receivedCount;
    }
    if (assembly.receivedCount < assembly.chunks.size()) {
        return std::nullopt;
    }

    std::string merged;
    merged.reserve(assembly.contentSize);
    for (const std::string& part : assembly.chunks) {
        merged += part;
    }
    const bool sizeMatches = merged.size() == assembly.contentSize;
    const bool checksumMatches = Nganu::ContentIntegrity::Fnv1a64Hex(merged) == assembly.checksum;
    if (!sizeMatches || !checksumMatches) {
        pendingAssetAssemblies_.erase(assemblyKey);
        BeginRetryWait("Asset integrity check failed. Retrying in 10 seconds.");
        return std::nullopt;
    }

    AssetBlob full {};
    full.key = assembly.key;
    full.kind = assembly.kind;
    full.revision = assembly.revision;
    full.encoding = assembly.encoding;
    full.checksum = assembly.checksum;
    full.contentSize = assembly.contentSize;
    full.chunkIndex = 0;
    full.chunkTotal = 1;
    full.content = std::move(merged);
    pendingAssetAssemblies_.erase(assemblyKey);
    return full;
}

std::filesystem::path Game::CacheDirectory() const {
#if defined(PLATFORM_ANDROID)
    char* cacheDir = GetCacheDir();
    if (cacheDir != nullptr) {
        const std::filesystem::path path(cacheDir);
        MemFree(cacheDir);
        return path / "nganu";
    }
#endif
    return std::filesystem::current_path() / "cache";
}

std::filesystem::path Game::CachePathForAsset(const std::string& assetKey, const std::string& revision) const {
    std::string safe = assetKey;
    for (char& ch : safe) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }

    std::string safeRevision = revision.empty() ? "unknown" : revision;
    for (char& ch : safeRevision) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }

    return CacheDirectory() / (safeRevision + "_" + safe + ".txt");
}

std::filesystem::path Game::ImageCachePathForAsset(const std::string& assetKey, const std::string& revision) const {
    std::string filename = assetKey;
    std::string bucket = "misc";
    if (filename.rfind("map_image:", 0) == 0) {
        filename = filename.substr(10);
        bucket = "map";
    } else if (filename.rfind("map_meta:", 0) == 0) {
        filename = filename.substr(9);
        bucket = "map";
    } else if (filename.rfind("character_image:", 0) == 0) {
        filename = filename.substr(16);
        bucket = "character";
    } else if (filename.rfind("ui_image:", 0) == 0) {
        filename = filename.substr(9);
        bucket = "ui";
    } else if (filename.rfind("ui_meta:", 0) == 0) {
        filename = filename.substr(8);
        bucket = "ui";
    }
    return CacheDirectory() / "assets" / bucket / (revision.empty() ? "unknown" : revision) / filename;
}

bool Game::SaveAssetToCache(const AssetBlob& asset) const {
    try {
        if (asset.kind == "image") {
            if (asset.encoding != "hex" || (asset.content.size() % 2) != 0) {
                return false;
            }
            std::filesystem::path outPath = ImageCachePathForAsset(asset.key, asset.revision);
            const std::filesystem::path tmpPath = outPath.string() + ".tmp";
            std::filesystem::create_directories(outPath.parent_path());
            std::vector<unsigned char> decoded;
            decoded.reserve(asset.content.size() / 2);
            for (size_t i = 0; i < asset.content.size(); i += 2) {
                const int hi = HexNibble(asset.content[i]);
                const int lo = HexNibble(asset.content[i + 1]);
                if (hi < 0 || lo < 0) {
                    return false;
                }
                decoded.push_back(static_cast<unsigned char>((hi << 4) | lo));
            }
            if (!HasPngHeader(decoded.data(), decoded.size())) {
                std::filesystem::remove(tmpPath);
                std::filesystem::remove(outPath);
                return false;
            }
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return false;
            }
            out.write(reinterpret_cast<const char*>(decoded.data()), static_cast<std::streamsize>(decoded.size()));
            out.close();
            if (!out.good()) {
                std::filesystem::remove(tmpPath);
                return false;
            }
            std::filesystem::remove(outPath);
            std::filesystem::rename(tmpPath, outPath);
            return true;
        } else if (asset.kind == "meta") {
            std::filesystem::path outPath = ImageCachePathForAsset(asset.key, asset.revision);
            std::filesystem::create_directories(outPath.parent_path());
            std::ofstream out(outPath, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
            out.write(asset.content.data(), static_cast<std::streamsize>(asset.content.size()));
            return out.good();
        }

        std::filesystem::create_directories(CacheDirectory());
        std::ofstream out(CachePathForAsset(asset.key, asset.revision), std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        out.write(asset.content.data(), static_cast<std::streamsize>(asset.content.size()));
        return out.good();
    } catch (...) {
        return false;
    }
}

bool Game::LoadCachedAsset(const std::string& assetKey, const std::string& revision, std::string& outContent) const {
    try {
        std::ifstream in(CachePathForAsset(assetKey, revision), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        outContent = buffer.str();
        return true;
    } catch (...) {
        return false;
    }
}

bool Game::HasCachedImageAsset(const std::string& assetKey, const std::string& revision) const {
    try {
        const std::filesystem::path path = ImageCachePathForAsset(assetKey, revision);
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) <= 8) {
            return false;
        }
        if (assetKey.rfind("map_meta:", 0) == 0 || assetKey.rfind("ui_meta:", 0) == 0) {
            return true;
        }
        std::ifstream in(path, std::ios::binary);
        unsigned char header[8] {};
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        const bool validPng = HasPngHeader(header, sizeof(header));
        if (!validPng) {
            std::filesystem::remove(path);
        }
        return validPng;
    } catch (...) {
        return false;
    }
}

void Game::BeginMapBootstrap() {
    if (manifest_.mapId.empty()) {
        BeginRetryWait("Manifest has no map asset. Retrying in 10 seconds.");
        return;
    }
    BeginMapBootstrapForAsset("map:" + manifest_.mapId, "Manifest ready. Downloading map asset...");
}

void Game::BeginMapBootstrapForAsset(const std::string& assetKey, const std::string& statusText) {
    mapReady_ = false;
    pendingMapAssetKey_.clear();
    pendingMapAssetKey_ = assetKey;

    std::string cached;
    if (LoadCachedAsset(assetKey, manifest_.revision, cached)) {
        AssetBlob cachedBlob;
        cachedBlob.key = assetKey;
        cachedBlob.kind = "map";
        cachedBlob.revision = manifest_.revision;
        cachedBlob.content = cached;
        lastMapAssetSource_ = "cache";
        ApplyMapAsset(cachedBlob);
        AddChatLine("[System] Loaded map asset from cache");
        return;
    }

    if (!network_.RequestAsset(assetKey)) {
        BeginRetryWait("Map request failed. Retrying in 10 seconds.");
        return;
    }
    lastMapAssetSource_ = "server";
    loginStatus_ = statusText;
    AddChatLine("[System] Requesting map asset " + assetKey);
}

void Game::ApplyMapAsset(const AssetBlob& asset) {
    if (asset.kind != "map") {
        return;
    }
    const std::string revision = asset.revision.empty() ? "unknown" : asset.revision;
    world_.SetMapAssetRoot(CacheDirectory() / "assets" / "map" / revision);
    world_.SetCharacterAssetRoot(CacheDirectory() / "assets" / "character" / revision);
    if (!world_.LoadFromMapAsset(asset.content)) {
        BeginRetryWait("Map asset failed to load. Retrying in 10 seconds.");
        return;
    }

    if (hasPendingSpawnPosition_) {
        player_.position = pendingSpawnPosition_;
        hasAuthoritativePosition_ = true;
    } else if (!hasAuthoritativePosition_) {
        player_.position = world_.spawnPoint();
    }
    lastSentPosition_ = player_.position;
    localCorrectionRemaining_ = Vector2 {};
    camera_.target = player_.position;
    mapReady_ = false;
    pendingMapAssetKey_.clear();
    pendingMapId_ = world_.mapId();
    hasPendingSpawnPosition_ = false;
    lastAppliedMapAssetKey_ = asset.key;
    manifest_.mapId = world_.mapId();
    manifest_.worldName = world_.worldName();
    AddChatLine("[System] Map asset applied: " + asset.key);
    EnsureReferencedImagesRequested();
    RefreshMapAssetReadiness();
}

void Game::EnsureReferencedImagesRequested() {
    pendingMapImageAssetKeys_.clear();
    auto addPending = [&](const std::string& assetKey) {
        if (std::find(pendingMapImageAssetKeys_.begin(), pendingMapImageAssetKeys_.end(), assetKey) == pendingMapImageAssetKeys_.end()) {
            pendingMapImageAssetKeys_.push_back(assetKey);
        }
    };

    for (const std::string& file : world_.referencedMapImageFiles()) {
        const std::string assetKey = "map_image:" + file;
        if (!HasCachedImageAsset(assetKey, manifest_.revision)) {
            addPending(assetKey);
        }
        if (network_.IsConnected() && !HasCachedImageAsset(assetKey, manifest_.revision)) {
            network_.RequestAsset(assetKey);
        }
        const std::string metaKey = "map_meta:" + std::filesystem::path(file).stem().string() + ".atlas";
        const bool metaListed = std::find(manifest_.assets.begin(), manifest_.assets.end(), metaKey) != manifest_.assets.end();
        if (metaListed && !HasCachedImageAsset(metaKey, manifest_.revision)) {
            addPending(metaKey);
        }
        if (metaListed && network_.IsConnected() && !HasCachedImageAsset(metaKey, manifest_.revision)) {
            network_.RequestAsset(metaKey);
        }
    }
    for (const std::string& file : world_.referencedCharacterImageFiles()) {
        const std::string assetKey = "character_image:" + file;
        if (!HasCachedImageAsset(assetKey, manifest_.revision)) {
            addPending(assetKey);
        }
        if (network_.IsConnected() && !HasCachedImageAsset(assetKey, manifest_.revision)) {
            network_.RequestAsset(assetKey);
        }
    }

    if (!pendingMapImageAssetKeys_.empty()) {
        loginStatus_ = "Downloading map textures: " + std::to_string(pendingMapImageAssetKeys_.size()) + " remaining";
        AddChatLine("[System] Waiting for map textures: " + std::to_string(pendingMapImageAssetKeys_.size()));
    }
}

void Game::RefreshMapAssetReadiness() {
    if (lastAppliedMapAssetKey_.empty()) {
        return;
    }

    pendingMapImageAssetKeys_.erase(
        std::remove_if(pendingMapImageAssetKeys_.begin(),
                       pendingMapImageAssetKeys_.end(),
                       [&](const std::string& assetKey) {
                           return HasCachedImageAsset(assetKey, manifest_.revision);
                       }),
        pendingMapImageAssetKeys_.end());

    if (!pendingMapImageAssetKeys_.empty()) {
        mapReady_ = false;
        loginStatus_ = "Downloading map textures: " + std::to_string(pendingMapImageAssetKeys_.size()) + " remaining";
        return;
    }

    if (!mapReady_) {
        world_.ReloadAtlasMetadata();
        mapReady_ = true;
        loginStatus_ = "Server ready. Press Enter to log in.";
        AddChatLine("[System] Map textures ready");
        if (uiMode_ == UiMode::Boot) {
            uiMode_ = UiMode::MainMenu;
        } else if (uiMode_ == UiMode::World) {
            AddChatLine("[System] World textures ready: " + world_.mapId());
        }
    }
}

void Game::EnsureUiDataAssetsRequested() {
    if (!network_.IsConnected() || manifest_.revision.empty()) {
        return;
    }

    for (const std::string& assetKey : manifest_.assets) {
        if (assetKey.rfind("data:", 0) != 0) {
            continue;
        }

        std::string cached;
        if (LoadCachedAsset(assetKey, manifest_.revision, cached)) {
            AssetBlob cachedBlob;
            cachedBlob.key = assetKey;
            cachedBlob.kind = "data";
            cachedBlob.revision = manifest_.revision;
            cachedBlob.content = cached;
            ApplyDataAsset(cachedBlob);
            continue;
        }

        network_.RequestAsset(assetKey);
    }
}

void Game::EnsureUiThemeAssetsRequested() {
    if (!network_.IsConnected()) {
        return;
    }

    for (const std::string& textureKey : uiTheme_.ReferencedTextureKeys()) {
        if (textureKey.rfind("ui:", 0) != 0) {
            continue;
        }
        const std::string assetKey = "ui_image:" + textureKey.substr(3);
        if (HasCachedImageAsset(assetKey, manifest_.revision)) {
            LoadUiTextureFromCache(assetKey);
            continue;
        }
        network_.RequestAsset(assetKey);
    }
}

void Game::ApplyDataAsset(const AssetBlob& asset) {
    if (asset.kind != "data") {
        return;
    }

    if (asset.key == "data:item_defs.json") {
        itemDefs_.LoadFromJson(asset.content);
        AddChatLine("[System] Item definitions ready: " + std::to_string(itemDefs_.Count()));
        return;
    }

    if (asset.key.rfind("data:ui/", 0) == 0) {
        uiData_.Store(asset.key, asset.content);
        if (asset.key == "data:ui/theme_default.json") {
            if (uiTheme_.LoadFromJson(asset.content)) {
                EnsureUiThemeAssetsRequested();
                AddChatLine("[System] UI theme ready: theme_default");
            }
        }
        const Ui::DataDocument* document = uiData_.FindByAssetKey(asset.key);
        if (document && document->windowConfig.has_value()) {
            Ui::Widget* widget = uiSystem_.Find(document->windowConfig->windowId);
            if (widget != nullptr) {
                widget->ApplyWindowConfig(*document->windowConfig);
            }
            if (document->windowConfig->windowId == "inventory_main") {
                inventoryLayoutReady_ = true;
            }
        }
        AddChatLine("[System] UI document loaded: " + asset.key);
    }
}

void Game::LoadUiTextureFromCache(const std::string& assetKey) {
    if (assetKey.rfind("ui_image:", 0) != 0) {
        return;
    }
    const std::filesystem::path path = ImageCachePathForAsset(assetKey, manifest_.revision);
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return;
    }
    uiAssets_.LoadFromBytes("ui:" + assetKey.substr(9), bytes);
}
