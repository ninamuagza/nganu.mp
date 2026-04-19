#include "ui/UiWindowConfig.h"
#include "shared/JsonRuntime.h"

#include <algorithm>
#include <unordered_map>

namespace {
Ui::Anchor AnchorFromString(const std::string& value) {
    if (value == "top_left") return Ui::Anchor::TopLeft;
    if (value == "top_right") return Ui::Anchor::TopRight;
    if (value == "bottom_left") return Ui::Anchor::BottomLeft;
    if (value == "bottom_right") return Ui::Anchor::BottomRight;
    return Ui::Anchor::Center;
}

} // namespace

namespace Ui {

std::optional<WindowConfig> ParseWindowConfig(const std::string& json) {
    const std::unordered_map<std::string, std::string> fields = Nganu::JsonRuntime::ParseFlatObject(json);
    WindowConfig config;
    config.windowId = Nganu::JsonRuntime::GetString(fields, "window_id").value_or("");
    if (config.windowId.empty()) {
        return std::nullopt;
    }

    config.title = Nganu::JsonRuntime::GetString(fields, "title").value_or("");
    if (config.title.empty()) {
        config.title = config.windowId;
    }

    config.width = static_cast<float>(std::max(220, Nganu::JsonRuntime::GetInt(fields, "size_w").value_or(static_cast<int>(config.width))));
    config.height = static_cast<float>(std::max(180, Nganu::JsonRuntime::GetInt(fields, "size_h").value_or(static_cast<int>(config.height))));
    config.minWidth = static_cast<float>(std::max(180, Nganu::JsonRuntime::GetInt(fields, "min_w").value_or(static_cast<int>(config.minWidth))));
    config.minHeight = static_cast<float>(std::max(140, Nganu::JsonRuntime::GetInt(fields, "min_h").value_or(static_cast<int>(config.minHeight))));
    config.offsetX = Nganu::JsonRuntime::GetFloat(fields, "offset_x").value_or(0.0f);
    config.offsetY = Nganu::JsonRuntime::GetFloat(fields, "offset_y").value_or(0.0f);
    config.padding = std::max(4, Nganu::JsonRuntime::GetInt(fields, "padding").value_or(config.padding));
    config.slotSize = std::max(24, Nganu::JsonRuntime::GetInt(fields, "slot_size").value_or(config.slotSize));
    config.columns = std::max(1, Nganu::JsonRuntime::GetInt(fields, "columns").value_or(config.columns));
    config.rows = std::max(1, Nganu::JsonRuntime::GetInt(fields, "rows").value_or(config.rows));
    config.slotSpacing = std::max(0, Nganu::JsonRuntime::GetInt(fields, "slot_spacing").value_or(config.slotSpacing));
    config.showUseButton = Nganu::JsonRuntime::GetBool(fields, "show_use_button").value_or(config.showUseButton);
    config.showDropButton = Nganu::JsonRuntime::GetBool(fields, "show_drop_button").value_or(config.showDropButton);
    config.anchor = AnchorFromString(Nganu::JsonRuntime::GetString(fields, "anchor").value_or(""));
    return config;
}

Rect ResolveAnchoredRect(const WindowConfig& config, float screenWidth, float screenHeight) {
    const float inset = 16.0f;
    const float width = std::min(screenWidth - inset * 2.0f, std::max(config.minWidth, config.width));
    const float height = std::min(screenHeight - inset * 2.0f, std::max(config.minHeight, config.height));

    switch (config.anchor) {
    case Anchor::TopLeft:
        return Rect {inset + config.offsetX, inset + config.offsetY, width, height};
    case Anchor::TopRight:
        return Rect {screenWidth - inset - width + config.offsetX, inset + config.offsetY, width, height};
    case Anchor::BottomLeft:
        return Rect {inset + config.offsetX, screenHeight - inset - height + config.offsetY, width, height};
    case Anchor::BottomRight:
        return Rect {screenWidth - inset - width + config.offsetX,
                      screenHeight - inset - height + config.offsetY,
                      width,
                      height};
    case Anchor::Center:
    default:
        return Rect {(screenWidth - width) * 0.5f + config.offsetX,
                     (screenHeight - height) * 0.5f + config.offsetY,
                     width,
                     height};
    }
}

} // namespace Ui
