#pragma once

/* ------------------------------------------------------------------ */
/* UiTypes.h — fundamental types shared by all UI widgets             */
/* ------------------------------------------------------------------ */

#include "raylib.h"
#include <functional>
#include <string>

namespace Ui {

/* Rect alias that carries pixel-space bounds */
using Rect = Rectangle;

/* Input result returned by widget Update() */
enum class InputResult {
    None,
    Hovered,
    Pressed,    /* just clicked */
    Held,       /* held this frame */
    Released,
};

/* Widget visibility state */
enum class Visibility { Visible, Hidden };

/* Anchor for positioning children */
enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight, Center };

/* Draw/input ordering bands for UI windows */
enum class Layer : int {
    Base = 0,
    Window = 100,
    Popup = 200,
    Modal = 300,
    Tooltip = 400,
};

/* Common callback */
using Callback = std::function<void()>;
using SlotCallback = std::function<void(int slotIndex)>;

/* Rarity color lookup */
inline Color RarityColor(const std::string& rarity) {
    if (rarity == "uncommon") return Color{80,  200, 80,  255};
    if (rarity == "rare")     return Color{80,  120, 220, 255};
    if (rarity == "epic")     return Color{160, 80,  220, 255};
    if (rarity == "legendary")return Color{220, 160, 40,  255};
    return Color{200, 200, 200, 255}; /* common */
}

} // namespace Ui
