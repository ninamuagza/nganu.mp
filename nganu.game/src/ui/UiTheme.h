#pragma once

#include "AssetManager.h"
#include "raylib.h"
#include <string>
#include <vector>

namespace Ui {

enum class ThemeRole {
    WindowBackground,
    PanelBackground,
    TitleBarBackground,
    ButtonPrimary,
    ButtonDanger,
};

struct ThemeSprite {
    std::string textureKey;
    Rectangle source {};
    bool valid = false;
};

class Theme {
public:
    void Clear();
    bool LoadFromJson(const std::string& json);
    std::vector<std::string> ReferencedTextureKeys() const;
    const ThemeSprite* SpriteForRole(ThemeRole role) const;

private:
    std::string themeId_;
    ThemeSprite windowBackground_;
    ThemeSprite panelBackground_;
    ThemeSprite titleBarBackground_;
    ThemeSprite buttonPrimary_;
    ThemeSprite buttonDanger_;
};

bool DrawThemeSprite(const ThemeSprite& sprite, const AssetManager& assets, Rectangle dest, Color tint = WHITE);
void DrawPanelSurface(const Theme& theme,
                      const AssetManager& assets,
                      Rectangle rect,
                      Color fallbackFill,
                      Color fallbackBorder,
                      ThemeRole role = ThemeRole::PanelBackground);
void DrawButtonSurface(const Theme& theme,
                       const AssetManager& assets,
                       Rectangle rect,
                       ThemeRole role,
                       Color fallbackFill,
                       Color fallbackBorder);

} // namespace Ui
