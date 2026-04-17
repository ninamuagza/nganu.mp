#pragma once

/* ------------------------------------------------------------------ */
/* AssetManager.h — texture registry, never loads files directly.     */
/* Caller provides raw PNG bytes (from cached ASSET_BLOB data).       */
/* ------------------------------------------------------------------ */

#include "raylib.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /* Load texture from raw PNG bytes (already decoded from server blob).
     * key is the asset key, e.g. "ui:icons/potion_health.png" */
    bool LoadFromBytes(const std::string& key, const std::vector<uint8_t>& bytes);

    /* Returns the texture for the given key.
     * If not found, returns the 1×1 fallback (magenta). */
    Texture2D GetTexture(const std::string& key) const;

    /* True if a texture is registered for this key */
    bool Has(const std::string& key) const;

    /* Remove a single texture entry if loaded */
    void Remove(const std::string& key);

    /* Unload all textures (call before closing window) */
    void UnloadAll();

    /* Number of loaded textures */
    size_t Count() const { return textures_.size(); }

private:
    std::unordered_map<std::string, Texture2D> textures_;
    Texture2D fallback_ {};
    bool fallbackReady_ = false;

    void EnsureFallback();
};
