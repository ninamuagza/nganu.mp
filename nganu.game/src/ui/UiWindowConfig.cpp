#include "ui/UiWindowConfig.h"

#include <algorithm>
#include <cctype>

namespace {

std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        ++pos;
        const size_t end = json.find('"', pos);
        if (end == std::string::npos) return {};
        return json.substr(pos, end - pos);
    }
    const size_t end = json.find_first_of(",}\n\r", pos);
    return json.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

int ExtractJsonInt(const std::string& json, const std::string& key, int fallback) {
    const std::string value = ExtractJsonValue(json, key);
    if (value.empty()) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

float ExtractJsonFloat(const std::string& json, const std::string& key, float fallback) {
    const std::string value = ExtractJsonValue(json, key);
    if (value.empty()) return fallback;
    try {
        return std::stof(value);
    } catch (...) {
        return fallback;
    }
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback) {
    const std::string value = ExtractJsonValue(json, key);
    if (value == "true") return true;
    if (value == "false") return false;
    return fallback;
}

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
    WindowConfig config;
    config.windowId = ExtractJsonValue(json, "window_id");
    if (config.windowId.empty()) {
        return std::nullopt;
    }

    config.title = ExtractJsonValue(json, "title");
    if (config.title.empty()) {
        config.title = config.windowId;
    }

    config.width = static_cast<float>(std::max(220, ExtractJsonInt(json, "size_w", static_cast<int>(config.width))));
    config.height = static_cast<float>(std::max(180, ExtractJsonInt(json, "size_h", static_cast<int>(config.height))));
    config.minWidth = static_cast<float>(std::max(180, ExtractJsonInt(json, "min_w", static_cast<int>(config.minWidth))));
    config.minHeight = static_cast<float>(std::max(140, ExtractJsonInt(json, "min_h", static_cast<int>(config.minHeight))));
    config.offsetX = ExtractJsonFloat(json, "offset_x", 0.0f);
    config.offsetY = ExtractJsonFloat(json, "offset_y", 0.0f);
    config.padding = std::max(4, ExtractJsonInt(json, "padding", config.padding));
    config.slotSize = std::max(24, ExtractJsonInt(json, "slot_size", config.slotSize));
    config.columns = std::max(1, ExtractJsonInt(json, "columns", config.columns));
    config.rows = std::max(1, ExtractJsonInt(json, "rows", config.rows));
    config.slotSpacing = std::max(0, ExtractJsonInt(json, "slot_spacing", config.slotSpacing));
    config.showUseButton = ExtractJsonBool(json, "show_use_button", config.showUseButton);
    config.showDropButton = ExtractJsonBool(json, "show_drop_button", config.showDropButton);
    config.anchor = AnchorFromString(ExtractJsonValue(json, "anchor"));
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
