#pragma once

#include "ui/UiSystem.h"
#include "ui/UiTheme.h"
#include "ui/UiWindowConfig.h"
#include "AssetManager.h"
#include "raylib.h"
#include <string>

class ObjectiveUi : public Ui::Widget {
public:
    ObjectiveUi(const std::string* objectiveText, const Ui::Theme* theme, const AssetManager* assets);

    void Update(float dt) override;
    void Draw() const override;
    void ApplyWindowConfig(const Ui::WindowConfig& config) override;

private:
    const std::string* objectiveText_ = nullptr;
    const Ui::Theme* theme_ = nullptr;
    const AssetManager* assets_ = nullptr;
    Ui::WindowConfig windowConfig_ {};
    std::string title_ = "Objective Journal";
    bool userMovedWindow_ = false;
    bool windowDrag_ = false;
    Vector2 windowDragOff_ {};

    void AnchorWindow();
    void DrawWrapped(const std::string& text, Ui::Rect bounds, int fontSize, Color color) const;
};
