#include "ObjectiveUi.h"

#include <algorithm>
#include <sstream>

namespace {
Color WindowBg() { return Color{18, 24, 30, 224}; }
Color WindowBorder() { return Color{88, 105, 120, 255}; }
Color TitleBarBg() { return Color{32, 42, 56, 255}; }
Color Accent() { return Color{111, 219, 159, 255}; }
}

ObjectiveUi::ObjectiveUi(const std::string* objectiveText, const Ui::Theme* theme, const AssetManager* assets)
    : Widget("objective_journal")
    , objectiveText_(objectiveText)
    , theme_(theme)
    , assets_(assets) {
    SetLayer(Ui::Layer::Window);
    windowConfig_.windowId = "objective_journal";
    windowConfig_.title = title_;
    windowConfig_.anchor = Ui::Anchor::TopRight;
    windowConfig_.offsetX = -12.0f;
    windowConfig_.offsetY = 56.0f;
    windowConfig_.width = 360.0f;
    windowConfig_.height = 220.0f;
    bounds = Ui::ResolveAnchoredRect(windowConfig_, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()));
    SetVisible(false);
}

void ObjectiveUi::Update(float dt) {
    (void)dt;
    if (!IsVisible()) return;

    const Vector2 mouse = GetMousePosition();
    const Ui::Rect titleBar{bounds.x, bounds.y, bounds.width, 30.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, titleBar)) {
        windowDrag_ = true;
        windowDragOff_ = Vector2{mouse.x - bounds.x, mouse.y - bounds.y};
    }

    if (windowDrag_) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            bounds.x = std::clamp(mouse.x - windowDragOff_.x, 8.0f, std::max(8.0f, static_cast<float>(GetScreenWidth()) - bounds.width - 8.0f));
            bounds.y = std::clamp(mouse.y - windowDragOff_.y, 8.0f, std::max(8.0f, static_cast<float>(GetScreenHeight()) - bounds.height - 8.0f));
            userMovedWindow_ = true;
        } else {
            windowDrag_ = false;
        }
    }

    const Ui::Rect closeBtn{bounds.x + bounds.width - 24.0f, bounds.y + 5.0f, 18.0f, 18.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, closeBtn)) {
        SetVisible(false);
    }
}

void ObjectiveUi::Draw() const {
    if (!IsVisible()) return;

    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, bounds, WindowBg(), WindowBorder(), Ui::ThemeRole::WindowBackground);
    } else {
        DrawRectangleRec(bounds, WindowBg());
        DrawRectangleLinesEx(bounds, 1.5f, WindowBorder());
    }

    const Ui::Rect titleBar{bounds.x, bounds.y, bounds.width, 30.0f};
    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, titleBar, TitleBarBg(), WindowBorder(), Ui::ThemeRole::TitleBarBackground);
    } else {
        DrawRectangleRec(titleBar, TitleBarBg());
    }
    DrawText(title_.c_str(), static_cast<int>(bounds.x) + 8, static_cast<int>(bounds.y) + 8, 14, RAYWHITE);

    const Ui::Rect closeBtn{bounds.x + bounds.width - 24.0f, bounds.y + 5.0f, 18.0f, 18.0f};
    if (theme_ && assets_) {
        Ui::DrawButtonSurface(*theme_, *assets_, closeBtn, Ui::ThemeRole::ButtonDanger, Color{120, 40, 40, 220}, Color{180, 80, 60, 255});
    } else {
        DrawRectangleRec(closeBtn, Color{120, 40, 40, 220});
    }
    DrawText("X", static_cast<int>(closeBtn.x) + 4, static_cast<int>(closeBtn.y) + 2, 12, WHITE);

    DrawText("Server Objective", static_cast<int>(bounds.x) + 14, static_cast<int>(bounds.y) + 42, 16, Accent());
    const std::string text = (objectiveText_ && !objectiveText_->empty()) ? *objectiveText_ : "No active objective.";
    DrawWrapped(text, Ui::Rect{bounds.x + 14.0f, bounds.y + 70.0f, bounds.width - 28.0f, bounds.height - 84.0f}, 18, Fade(RAYWHITE, 0.84f));
}

void ObjectiveUi::ApplyWindowConfig(const Ui::WindowConfig& config) {
    windowConfig_ = config;
    title_ = config.title.empty() ? "Objective Journal" : config.title;
    if (!userMovedWindow_) {
        AnchorWindow();
    }
}

void ObjectiveUi::AnchorWindow() {
    bounds = Ui::ResolveAnchoredRect(windowConfig_, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()));
}

void ObjectiveUi::DrawWrapped(const std::string& text, Ui::Rect boundsRect, int fontSize, Color color) const {
    std::istringstream stream(text);
    std::string word;
    std::string line;
    int y = static_cast<int>(boundsRect.y);
    const int lineHeight = fontSize + 4;
    while (stream >> word) {
        const std::string candidate = line.empty() ? word : (line + " " + word);
        if (MeasureText(candidate.c_str(), fontSize) <= static_cast<int>(boundsRect.width)) {
            line = candidate;
            continue;
        }
        DrawText(line.c_str(), static_cast<int>(boundsRect.x), y, fontSize, color);
        y += lineHeight;
        if (y + lineHeight > boundsRect.y + boundsRect.height) {
            return;
        }
        line = word;
    }
    if (!line.empty() && y <= boundsRect.y + boundsRect.height) {
        DrawText(line.c_str(), static_cast<int>(boundsRect.x), y, fontSize, color);
    }
}
