#pragma once

#include "ui/UiTypes.h"
#include <optional>
#include <string>

namespace Ui {

struct WindowConfig {
    std::string windowId;
    std::string title;
    float width = 440.0f;
    float height = 340.0f;
    float minWidth = 220.0f;
    float minHeight = 180.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int padding = 12;
    int slotSize = 48;
    int columns = 5;
    int rows = 4;
    int slotSpacing = 4;
    bool showUseButton = true;
    bool showDropButton = true;
    Anchor anchor = Anchor::Center;
};

std::optional<WindowConfig> ParseWindowConfig(const std::string& json);
Rect ResolveAnchoredRect(const WindowConfig& config, float screenWidth, float screenHeight);

} // namespace Ui
