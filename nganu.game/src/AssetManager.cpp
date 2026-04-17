#include "AssetManager.h"

#include "raylib.h"
#include <cstring>

AssetManager::AssetManager() {
    EnsureFallback();
}

AssetManager::~AssetManager() {
    UnloadAll();
}

void AssetManager::EnsureFallback() {
    if (fallbackReady_) return;
    /* 4×4 magenta RGBA texture as fallback */
    Image img = GenImageColor(4, 4, MAGENTA);
    fallback_ = LoadTextureFromImage(img);
    UnloadImage(img);
    fallbackReady_ = (fallback_.id > 0);
}

bool AssetManager::LoadFromBytes(const std::string& key, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return false;

    /* Load from memory using raylib extension */
    const char* ext = ".png";
    Image img = LoadImageFromMemory(ext, bytes.data(), static_cast<int>(bytes.size()));
    if (img.data == nullptr) return false;

    /* Unload previous texture with same key if exists */
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        if (it->second.id > 0) UnloadTexture(it->second);
        textures_.erase(it);
    }

    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tex.id == 0) return false;

    textures_[key] = tex;
    return true;
}

Texture2D AssetManager::GetTexture(const std::string& key) const {
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    return fallback_;
}

bool AssetManager::Has(const std::string& key) const {
    return textures_.count(key) > 0;
}

void AssetManager::Remove(const std::string& key) {
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }
    if (it->second.id > 0) {
        UnloadTexture(it->second);
    }
    textures_.erase(it);
}

void AssetManager::UnloadAll() {
    for (auto& [key, tex] : textures_) {
        if (tex.id > 0) UnloadTexture(tex);
    }
    textures_.clear();
    if (fallbackReady_ && fallback_.id > 0) {
        UnloadTexture(fallback_);
        fallback_ = {};
        fallbackReady_ = false;
    }
}
