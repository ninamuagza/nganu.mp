#include "ui/UiDataStore.h"

namespace Ui {

void DataStore::Clear() {
    documents_.clear();
    windowToAssetKey_.clear();
}

void DataStore::Store(const std::string& assetKey, const std::string& raw) {
    DataDocument document;
    document.assetKey = assetKey;
    document.raw = raw;
    document.windowConfig = ParseWindowConfig(raw);
    documents_[assetKey] = document;

    if (document.windowConfig.has_value()) {
        windowToAssetKey_[document.windowConfig->windowId] = assetKey;
    }
}

const DataDocument* DataStore::FindByAssetKey(const std::string& assetKey) const {
    auto it = documents_.find(assetKey);
    if (it == documents_.end()) {
        return nullptr;
    }
    return &it->second;
}

const WindowConfig* DataStore::FindWindowConfig(const std::string& windowId) const {
    auto keyIt = windowToAssetKey_.find(windowId);
    if (keyIt == windowToAssetKey_.end()) {
        return nullptr;
    }
    const DataDocument* document = FindByAssetKey(keyIt->second);
    if (!document || !document->windowConfig.has_value()) {
        return nullptr;
    }
    return &*document->windowConfig;
}

} // namespace Ui
