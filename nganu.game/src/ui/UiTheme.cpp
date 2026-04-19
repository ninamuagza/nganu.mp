#include "ui/UiTheme.h"
#include "shared/JsonRuntime.h"

#include <algorithm>

namespace {

Ui::ThemeSprite ParseThemeSprite(const std::string& value) {
    Ui::ThemeSprite sprite;
    if (value.empty()) {
        return sprite;
    }

    size_t firstAt = value.find('@');
    if (firstAt == std::string::npos) {
        return sprite;
    }

    std::string filePart = value.substr(0, firstAt);
    std::string coordsPart = value.substr(firstAt + 1);
    const size_t domainSep = filePart.find(':');
    if (domainSep == std::string::npos || filePart.substr(0, domainSep) != "ui") {
        return sprite;
    }
    const std::string file = filePart.substr(domainSep + 1);
    if (file.empty()) {
        return sprite;
    }

    float coords[4] {};
    int index = 0;
    size_t start = 0;
    while (index < 4 && start <= coordsPart.size()) {
        const size_t nextAt = coordsPart.find('@', start);
        const std::string token = coordsPart.substr(start, nextAt == std::string::npos ? std::string::npos : nextAt - start);
        try {
            coords[index++] = std::stof(token);
        } catch (...) {
            return sprite;
        }
        if (nextAt == std::string::npos) {
            break;
        }
        start = nextAt + 1;
    }
    if (index != 4) {
        return sprite;
    }

    sprite.textureKey = "ui:" + file;
    sprite.source = Rectangle {coords[0], coords[1], coords[2], coords[3]};
    sprite.valid = sprite.source.width > 0.0f && sprite.source.height > 0.0f;
    return sprite;
}

} // namespace

namespace Ui {

void Theme::Clear() {
    themeId_.clear();
    windowBackground_ = ThemeSprite {};
    panelBackground_ = ThemeSprite {};
    titleBarBackground_ = ThemeSprite {};
    buttonPrimary_ = ThemeSprite {};
    buttonDanger_ = ThemeSprite {};
}

bool Theme::LoadFromJson(const std::string& json) {
    Clear();
    const auto fields = Nganu::JsonRuntime::ParseFlatObject(json);
    themeId_ = Nganu::JsonRuntime::GetString(fields, "theme_id").value_or("");
    windowBackground_ = ParseThemeSprite(Nganu::JsonRuntime::GetString(fields, "window_bg").value_or(""));
    panelBackground_ = ParseThemeSprite(Nganu::JsonRuntime::GetString(fields, "panel_bg").value_or(""));
    titleBarBackground_ = ParseThemeSprite(Nganu::JsonRuntime::GetString(fields, "title_bar_bg").value_or(""));
    buttonPrimary_ = ParseThemeSprite(Nganu::JsonRuntime::GetString(fields, "button_primary").value_or(""));
    buttonDanger_ = ParseThemeSprite(Nganu::JsonRuntime::GetString(fields, "button_danger").value_or(""));
    return !themeId_.empty();
}

std::vector<std::string> Theme::ReferencedTextureKeys() const {
    std::vector<std::string> keys;
    auto append = [&keys](const ThemeSprite& sprite) {
        if (!sprite.valid || sprite.textureKey.empty()) return;
        if (std::find(keys.begin(), keys.end(), sprite.textureKey) == keys.end()) {
            keys.push_back(sprite.textureKey);
        }
    };
    append(windowBackground_);
    append(panelBackground_);
    append(titleBarBackground_);
    append(buttonPrimary_);
    append(buttonDanger_);
    return keys;
}

const ThemeSprite* Theme::SpriteForRole(ThemeRole role) const {
    switch (role) {
    case ThemeRole::WindowBackground: return windowBackground_.valid ? &windowBackground_ : nullptr;
    case ThemeRole::PanelBackground: return panelBackground_.valid ? &panelBackground_ : nullptr;
    case ThemeRole::TitleBarBackground: return titleBarBackground_.valid ? &titleBarBackground_ : nullptr;
    case ThemeRole::ButtonPrimary: return buttonPrimary_.valid ? &buttonPrimary_ : nullptr;
    case ThemeRole::ButtonDanger: return buttonDanger_.valid ? &buttonDanger_ : nullptr;
    default: return nullptr;
    }
}

bool DrawThemeSprite(const ThemeSprite& sprite, const AssetManager& assets, Rectangle dest, Color tint) {
    if (!sprite.valid || !assets.Has(sprite.textureKey)) {
        return false;
    }
    const Texture2D texture = assets.GetTexture(sprite.textureKey);
    if (texture.id <= 0) {
        return false;
    }
    DrawTexturePro(texture, sprite.source, dest, Vector2 {}, 0.0f, tint);
    return true;
}

void DrawPanelSurface(const Theme& theme,
                      const AssetManager& assets,
                      Rectangle rect,
                      Color fallbackFill,
                      Color fallbackBorder,
                      ThemeRole role) {
    const ThemeSprite* sprite = theme.SpriteForRole(role);
    if (sprite && DrawThemeSprite(*sprite, assets, rect, WHITE)) {
        DrawRectangleLinesEx(rect, 1.5f, ColorAlpha(fallbackBorder, 0.75f));
        return;
    }
    DrawRectangleRec(rect, fallbackFill);
    DrawRectangleLinesEx(rect, 1.5f, fallbackBorder);
}

void DrawButtonSurface(const Theme& theme,
                       const AssetManager& assets,
                       Rectangle rect,
                       ThemeRole role,
                       Color fallbackFill,
                       Color fallbackBorder) {
    const ThemeSprite* sprite = theme.SpriteForRole(role);
    if (sprite && DrawThemeSprite(*sprite, assets, rect, WHITE)) {
        DrawRectangleLinesEx(rect, 1.0f, ColorAlpha(fallbackBorder, 0.82f));
        return;
    }
    DrawRectangleRec(rect, fallbackFill);
    DrawRectangleLinesEx(rect, 1.0f, fallbackBorder);
}

} // namespace Ui
