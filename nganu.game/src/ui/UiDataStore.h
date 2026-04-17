#pragma once

#include "ui/UiWindowConfig.h"
#include <optional>
#include <string>
#include <unordered_map>

namespace Ui {

struct DataDocument {
    std::string assetKey;
    std::string raw;
    std::optional<WindowConfig> windowConfig;
};

class DataStore {
public:
    void Clear();
    void Store(const std::string& assetKey, const std::string& raw);
    const DataDocument* FindByAssetKey(const std::string& assetKey) const;
    const WindowConfig* FindWindowConfig(const std::string& windowId) const;

private:
    std::unordered_map<std::string, DataDocument> documents_;
    std::unordered_map<std::string, std::string> windowToAssetKey_;
};

} // namespace Ui
