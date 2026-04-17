#include "ModalDialogUi.h"

#include <sstream>

namespace {
Color BackdropColor() { return Color{8, 10, 14, 145}; }
Color WindowBg() { return Color{22, 28, 36, 245}; }
Color WindowBorder() { return Color{112, 134, 152, 255}; }
Color Accent() { return Color{111, 219, 159, 255}; }
}

ModalDialogUi::ModalDialogUi(const Ui::Theme* theme, const AssetManager* assets)
    : Widget("system_modal")
    , theme_(theme)
    , assets_(assets) {
    SetLayer(Ui::Layer::Modal);
    windowConfig_.windowId = "system_modal";
    windowConfig_.title = "Dialog";
    windowConfig_.anchor = Ui::Anchor::Center;
    windowConfig_.width = 420.0f;
    windowConfig_.height = 220.0f;
    windowConfig_.minWidth = 320.0f;
    windowConfig_.minHeight = 180.0f;
    bounds = Ui::ResolveAnchoredRect(windowConfig_,
                                     static_cast<float>(GetScreenWidth()),
                                     static_cast<float>(GetScreenHeight()));
    SetVisible(false);
}

void ModalDialogUi::Update(float dt) {
    (void)dt;
    if (!IsVisible()) return;

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER)) {
        Hide();
        return;
    }

    const Vector2 mouse = GetMousePosition();
    const Ui::Rect buttonRect{
        bounds.x + (bounds.width - 104.0f) * 0.5f,
        bounds.y + bounds.height - 48.0f,
        104.0f,
        28.0f
    };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, buttonRect)) {
        Hide();
    }
}

void ModalDialogUi::Draw() const {
    if (!IsVisible()) return;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BackdropColor());
    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, bounds, WindowBg(), WindowBorder(), Ui::ThemeRole::WindowBackground);
    } else {
        DrawRectangleRec(bounds, WindowBg());
        DrawRectangleLinesEx(bounds, 1.5f, WindowBorder());
    }

    const Ui::Rect titleBar{bounds.x, bounds.y, bounds.width, 34.0f};
    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, titleBar, Color{30, 38, 48, 255}, WindowBorder(), Ui::ThemeRole::TitleBarBackground);
    }

    DrawText(title_.c_str(), static_cast<int>(bounds.x) + 16, static_cast<int>(bounds.y) + 16, 20, RAYWHITE);
    DrawText("Modal Layer", static_cast<int>(bounds.x) + 16, static_cast<int>(bounds.y) + 42, 14, Accent());

    DrawWrapped(message_,
                Ui::Rect{bounds.x + 16.0f, bounds.y + 74.0f, bounds.width - 32.0f, bounds.height - 124.0f},
                18,
                Fade(RAYWHITE, 0.86f));

    const Ui::Rect buttonRect{
        bounds.x + (bounds.width - 104.0f) * 0.5f,
        bounds.y + bounds.height - 48.0f,
        104.0f,
        28.0f
    };
    if (theme_ && assets_) {
        Ui::DrawButtonSurface(*theme_, *assets_, buttonRect, Ui::ThemeRole::ButtonPrimary, Accent(), Color{80, 180, 90, 255});
    } else {
        DrawRectangleRec(buttonRect, Accent());
    }
    DrawText("Close",
             static_cast<int>(buttonRect.x) + 30,
             static_cast<int>(buttonRect.y) + 6,
             14,
             Color{18, 30, 26, 255});
}

void ModalDialogUi::ApplyWindowConfig(const Ui::WindowConfig& config) {
    windowConfig_ = config;
    if (!config.title.empty()) {
        title_ = config.title;
    }
    AnchorWindow();
}

bool ModalDialogUi::HitTest(Vector2 point) const {
    if (!IsVisible()) {
        return false;
    }
    return CheckCollisionPointRec(point, Ui::Rect{0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
}

void ModalDialogUi::Show(std::string title, std::string message) {
    title_ = title.empty() ? "Dialog" : std::move(title);
    message_ = std::move(message);
    AnchorWindow();
    SetVisible(true);
}

void ModalDialogUi::Hide() {
    SetVisible(false);
}

void ModalDialogUi::AnchorWindow() {
    bounds = Ui::ResolveAnchoredRect(windowConfig_,
                                     static_cast<float>(GetScreenWidth()),
                                     static_cast<float>(GetScreenHeight()));
}

void ModalDialogUi::DrawWrapped(const std::string& text, Ui::Rect boundsRect, int fontSize, Color color) const {
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
