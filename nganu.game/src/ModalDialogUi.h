#pragma once

#include "ui/UiSystem.h"
#include "ui/UiTheme.h"
#include "ui/UiWindowConfig.h"
#include "AssetManager.h"
#include "raylib.h"
#include <string>

class ModalDialogUi : public Ui::Widget {
public:
    ModalDialogUi(const Ui::Theme* theme, const AssetManager* assets);

    void Update(float dt) override;
    void Draw() const override;
    void ApplyWindowConfig(const Ui::WindowConfig& config) override;
    bool HitTest(Vector2 point) const override;
    bool BlocksLowerLayers() const override { return IsVisible(); }

    void Show(std::string title, std::string message);
    void Hide();
    bool IsOpen() const { return IsVisible(); }

private:
    Ui::WindowConfig windowConfig_ {};
    const Ui::Theme* theme_ = nullptr;
    const AssetManager* assets_ = nullptr;
    std::string title_ = "Dialog";
    std::string message_;

    void AnchorWindow();
    void DrawWrapped(const std::string& text, Ui::Rect bounds, int fontSize, Color color) const;
};
