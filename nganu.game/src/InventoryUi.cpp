#include "InventoryUi.h"

#include <algorithm>
#include <cmath>
#include <string>

/* ------------------------------------------------------------------ */
/* Colour palette for inventory UI                                    */
/* ------------------------------------------------------------------ */
namespace {
Color WindowBg()      { return Color{20,  22,  28,  220}; }
Color WindowBorder()  { return Color{80,  85,  100, 255}; }
Color TitleBarBg()    { return Color{35,  40,  55,  255}; }
Color SlotBg()        { return Color{35,  38,  48,  200}; }
Color SlotBorder()    { return Color{60,  65,  80,  255}; }
Color SlotHover()     { return Color{60,  70,  95,  255}; }
Color SlotSelected()  { return Color{80,  110, 160, 255}; }
Color TextColor()     { return WHITE; }
Color DimText()       { return Color{160, 165, 180, 255}; }
Color AmountText()    { return Color{220, 220, 100, 255}; }

}

/* ------------------------------------------------------------------ */
InventoryUi::InventoryUi(const ClientInventory* inventory,
                         const ItemDefs*        itemDefs,
                         const AssetManager*    assets,
                         const Ui::Theme*       theme,
                         InventoryNetCallbacks  callbacks)
    : Widget("inventory_main")
    , inventory_(inventory)
    , itemDefs_(itemDefs)
    , assets_(assets)
    , theme_(theme)
    , callbacks_(std::move(callbacks))
{
    SetLayer(Ui::Layer::Window);
    SetVisible(false);
    windowConfig_.windowId = "inventory_main";
    windowConfig_.title = title_;
    ComputeLayout();
}

void InventoryUi::ComputeLayout() {
    const float minW = static_cast<float>(padding_ * 2 + columns_ * (slotSize_ + slotSpacing_) - slotSpacing_ + 12);
    const float minH = static_cast<float>(padding_ * 2 + rows_ * (slotSize_ + slotSpacing_) - slotSpacing_ + 38 /* title bar */ + 40 /* action bar */);
    windowW_ = std::max(windowW_, minW);
    windowH_ = std::max(windowH_, minH);
    bounds = Ui::Rect{windowX_, windowY_, windowW_, windowH_};
}

void InventoryUi::Open() {
    SetVisible(true);
    if (!userMovedWindow_) {
        AnchorWindow();
    }
    if (callbacks_.sendOpen) callbacks_.sendOpen();
}

void InventoryUi::Close() {
    SetVisible(false);
    dragging_ = false;
    dragSlot_ = -1;
    selectedSlot_ = -1;
    if (callbacks_.sendClose) callbacks_.sendClose();
}

void InventoryUi::Toggle() {
    if (IsVisible()) Close(); else Open();
}

/* ------------------------------------------------------------------ */
Ui::Rect InventoryUi::SlotRect(int index) const {
    const int col = index % columns_;
    const int row = index / columns_;
    return Ui::Rect{
        windowX_ + padding_ + col * (slotSize_ + slotSpacing_),
        windowY_ + 38.0f + padding_ + row * (slotSize_ + slotSpacing_),
        static_cast<float>(slotSize_),
        static_cast<float>(slotSize_)
    };
}

int InventoryUi::SlotAtPoint(Vector2 p) const {
    const int slotCount = inventory_ ? static_cast<int>(inventory_->slots.size()) : 0;
    for (int i = 0; i < slotCount; ++i) {
        if (CheckCollisionPointRec(p, SlotRect(i))) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
void InventoryUi::ApplyFullState(const ClientInventory& inv) {
    /* Note: we hold a pointer to the caller's ClientInventory,
     * so the caller should update their struct and we'll reflect it. */
    (void)inv;
}

void InventoryUi::ApplySlotUpdate(int slotIndex, bool occupied, int itemDefId, int amount, uint8_t flags) {
    if (!inventory_) return;
    /* The caller owns the inventory; this is called after caller updates it */
    (void)slotIndex; (void)occupied; (void)itemDefId; (void)amount; (void)flags;
}

void InventoryUi::ApplyWindowConfig(const Ui::WindowConfig& config) {
    windowConfig_ = config;
    title_ = config.title.empty() ? "Inventory" : config.title;
    slotSize_ = std::max(32, config.slotSize);
    columns_ = std::max(1, config.columns);
    rows_ = std::max(1, config.rows);
    padding_ = std::max(4, config.padding);
    slotSpacing_ = std::max(0, config.slotSpacing);
    windowW_ = std::max(config.minWidth, config.width);
    windowH_ = std::max(config.minHeight, config.height);
    showUseButton_ = config.showUseButton;
    showDropButton_ = config.showDropButton;
    ComputeLayout();
    if (!userMovedWindow_) {
        AnchorWindow();
    }
}

/* ------------------------------------------------------------------ */
void InventoryUi::Update(float dt) {
    (void)dt;
    if (!IsVisible() || !inventory_) return;

    const Vector2 mouse = GetMousePosition();

    /* — Window drag via title bar — */
    const Ui::Rect titleBar{windowX_, windowY_, windowW_, 30.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !dragging_) {
        if (CheckCollisionPointRec(mouse, titleBar)) {
            windowDrag_    = true;
            windowDragOff_ = Vector2{mouse.x - windowX_, mouse.y - windowY_};
        }
    }
    if (windowDrag_) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            windowX_ = mouse.x - windowDragOff_.x;
            windowY_ = mouse.y - windowDragOff_.y;
            windowX_ = std::clamp(windowX_, 8.0f, std::max(8.0f, static_cast<float>(GetScreenWidth()) - windowW_ - 8.0f));
            windowY_ = std::clamp(windowY_, 8.0f, std::max(8.0f, static_cast<float>(GetScreenHeight()) - windowH_ - 8.0f));
            userMovedWindow_ = true;
            ComputeLayout();
        } else {
            windowDrag_ = false;
        }
    }

    /* — Close button — */
    const Ui::Rect closeBtn{windowX_ + windowW_ - 24.0f, windowY_ + 5.0f, 18.0f, 18.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, closeBtn)) {
        Close();
        return;
    }

    /* — Hover detection — */
    hoverSlot_ = SlotAtPoint(mouse);

    /* — Drag begin — */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hoverSlot_ >= 0 && !windowDrag_) {
        const auto* slot = inventory_->slots.empty() ? nullptr : &inventory_->slots[hoverSlot_];
        if (slot && slot->occupied && (slot->flags & 0x04)) {
            dragging_   = true;
            dragSlot_   = hoverSlot_;
            selectedSlot_ = hoverSlot_;
            dragOffset_ = Vector2{mouse.x - SlotRect(hoverSlot_).x, mouse.y - SlotRect(hoverSlot_).y};
        } else {
            selectedSlot_ = hoverSlot_;
        }
    }

    /* — Drag end / drop — */
    if (dragging_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        const int targetSlot = SlotAtPoint(mouse);
        if (targetSlot >= 0 && targetSlot != dragSlot_) {
            if (callbacks_.sendMoveItem) callbacks_.sendMoveItem(dragSlot_, targetSlot);
        }
        dragging_ = false;
        dragSlot_ = -1;
    }

    /* — Right-click: use item — */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && hoverSlot_ >= 0) {
        const auto* slot = inventory_->slots.empty() ? nullptr : &inventory_->slots[hoverSlot_];
        if (slot && slot->occupied && (slot->flags & 0x01)) {
            if (callbacks_.sendUseItem) callbacks_.sendUseItem(hoverSlot_);
        }
    }
}

/* ------------------------------------------------------------------ */
void InventoryUi::Draw() const {
    if (!IsVisible() || !inventory_) return;

    /* — Window background — */
    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, bounds, WindowBg(), WindowBorder(), Ui::ThemeRole::WindowBackground);
    } else {
        DrawRectangleRec(bounds, WindowBg());
        DrawRectangleLinesEx(bounds, 1.5f, WindowBorder());
    }

    /* — Title bar — */
    const Ui::Rect titleBar{windowX_, windowY_, windowW_, 30.0f};
    if (theme_ && assets_) {
        Ui::DrawPanelSurface(*theme_, *assets_, titleBar, TitleBarBg(), WindowBorder(), Ui::ThemeRole::TitleBarBackground);
    } else {
        DrawRectangleRec(titleBar, TitleBarBg());
    }
    DrawText(title_.c_str(), static_cast<int>(windowX_) + 8, static_cast<int>(windowY_) + 8, 14, TextColor());

    /* — Close button — */
    const Ui::Rect closeBtn{windowX_ + windowW_ - 24.0f, windowY_ + 5.0f, 18.0f, 18.0f};
    if (theme_ && assets_) {
        Ui::DrawButtonSurface(*theme_, *assets_, closeBtn, Ui::ThemeRole::ButtonDanger, Color{120, 40, 40, 220}, Color{180, 80, 60, 255});
    } else {
        DrawRectangleRec(closeBtn, Color{120, 40, 40, 220});
    }
    DrawText("X", static_cast<int>(closeBtn.x) + 4, static_cast<int>(closeBtn.y) + 2, 12, WHITE);

    /* — Slots — */
    const int slotCount = static_cast<int>(inventory_->slots.size());
    for (int i = 0; i < slotCount; ++i) {
        /* Skip dragged slot — drawn last */
        if (dragging_ && i == dragSlot_) continue;
        DrawSlot(i);
    }

    /* — Dragged item follows cursor — */
    if (dragging_ && dragSlot_ >= 0 && dragSlot_ < slotCount) {
        const auto& slot = inventory_->slots[dragSlot_];
        const Vector2 mouse = GetMousePosition();
        const Ui::Rect dr{mouse.x - dragOffset_.x, mouse.y - dragOffset_.y,
                      static_cast<float>(slotSize_), static_cast<float>(slotSize_)};
        DrawRectangleRec(dr, Color{80, 110, 160, 200});
        if (slot.occupied && assets_) {
            const ItemDef* def = itemDefs_ ? itemDefs_->Find(slot.item_def_id) : nullptr;
            if (def && assets_->Has(def->icon_key)) {
                const Texture2D& tex = assets_->GetTexture(def->icon_key);
                DrawTexturePro(tex,
                    Rectangle{0, 0, (float)tex.width, (float)tex.height},
                    Rectangle{dr.x + 2, dr.y + 2, dr.width - 4, dr.height - 4},
                    Vector2{0,0}, 0.0f, WHITE);
            }
        }
    }

    /* — Tooltip — */
    if (!dragging_ && hoverSlot_ >= 0 && hoverSlot_ < slotCount) {
        const auto& slot = inventory_->slots[hoverSlot_];
        if (slot.occupied) DrawTooltip(hoverSlot_);
    }

    /* — Action bar — */
    const float abY = windowY_ + windowH_ - 36.0f;
    if (selectedSlot_ >= 0 && selectedSlot_ < slotCount) {
        const auto& slot = inventory_->slots[selectedSlot_];
        if (slot.occupied) {
            /* Use button */
            if (showUseButton_ && (slot.flags & 0x01)) {
                const Ui::Rect useBtn{windowX_ + padding_, abY + 4, 60, 24};
                if (theme_ && assets_) {
                    Ui::DrawButtonSurface(*theme_, *assets_, useBtn, Ui::ThemeRole::ButtonPrimary, Color{50, 120, 60, 230}, Color{80, 180, 90, 255});
                } else {
                    DrawRectangleRec(useBtn, Color{50, 120, 60, 230});
                    DrawRectangleLinesEx(useBtn, 1.0f, Color{80, 180, 90, 255});
                }
                DrawText("Use", static_cast<int>(useBtn.x) + 14, static_cast<int>(useBtn.y) + 4, 12, WHITE);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(GetMousePosition(), useBtn)) {
                    if (callbacks_.sendUseItem) callbacks_.sendUseItem(selectedSlot_);
                }
            }
            /* Drop button */
            if (showDropButton_ && (slot.flags & 0x02)) {
                const Ui::Rect dropBtn{windowX_ + padding_ + 66, abY + 4, 60, 24};
                if (theme_ && assets_) {
                    Ui::DrawButtonSurface(*theme_, *assets_, dropBtn, Ui::ThemeRole::ButtonDanger, Color{120, 50, 40, 230}, Color{180, 80, 60, 255});
                } else {
                    DrawRectangleRec(dropBtn, Color{120, 50, 40, 230});
                    DrawRectangleLinesEx(dropBtn, 1.0f, Color{180, 80, 60, 255});
                }
                DrawText("Drop", static_cast<int>(dropBtn.x) + 10, static_cast<int>(dropBtn.y) + 4, 12, WHITE);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(GetMousePosition(), dropBtn)) {
                    if (callbacks_.sendDropItem) callbacks_.sendDropItem(selectedSlot_);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
void InventoryUi::DrawSlot(int index) const {
    if (!inventory_) return;
    const Ui::Rect sr = SlotRect(index);
    const bool isHovered  = (index == hoverSlot_);
    const bool isSelected = (index == selectedSlot_);

    /* Background */
    Color bg = isSelected ? SlotSelected() : (isHovered ? SlotHover() : SlotBg());
    DrawRectangleRec(sr, bg);
    DrawRectangleLinesEx(sr, 1.0f, SlotBorder());

    if (index >= static_cast<int>(inventory_->slots.size())) return;
    const ClientSlot& slot = inventory_->slots[index];
    if (!slot.occupied) return;

    /* Icon */
    const ItemDef* def = itemDefs_ ? itemDefs_->Find(slot.item_def_id) : nullptr;
    if (def && assets_ && assets_->Has(def->icon_key)) {
        const Texture2D& tex = assets_->GetTexture(def->icon_key);
        DrawTexturePro(tex,
            Rectangle{0, 0, (float)tex.width, (float)tex.height},
            Rectangle{sr.x + 2, sr.y + 2, sr.width - 4, sr.height - 4},
            Vector2{0, 0}, 0.0f, WHITE);
    } else {
        /* Colored fallback based on rarity */
        const Color rarityCol = def ? Ui::RarityColor(def->rarity) : Ui::RarityColor("common");
        DrawRectangle(static_cast<int>(sr.x + 4), static_cast<int>(sr.y + 4),
                      slotSize_ - 8, slotSize_ - 8, ColorAlpha(rarityCol, 0.5f));
        /* First letter of item name */
        if (def && !def->name.empty()) {
            const std::string letter(1, def->name[0]);
            DrawText(letter.c_str(),
                     static_cast<int>(sr.x + slotSize_ / 2 - 4),
                     static_cast<int>(sr.y + slotSize_ / 2 - 7), 14, WHITE);
        }
    }

    /* Amount label (bottom-right) */
    if (slot.amount > 1) {
        const std::string amtStr = std::to_string(slot.amount);
        const int tw = MeasureText(amtStr.c_str(), 10);
        DrawText(amtStr.c_str(),
                 static_cast<int>(sr.x + sr.width  - tw - 3),
                 static_cast<int>(sr.y + sr.height - 13), 10, AmountText());
    }
}

/* ------------------------------------------------------------------ */
void InventoryUi::DrawTooltip(int slotIndex) const {
    if (!inventory_ || slotIndex < 0 || slotIndex >= static_cast<int>(inventory_->slots.size())) return;
    const ClientSlot& slot = inventory_->slots[slotIndex];
    if (!slot.occupied) return;

    const ItemDef* def = itemDefs_ ? itemDefs_->Find(slot.item_def_id) : nullptr;
    const std::string name = def ? def->name : ItemDefs::UnknownName(slot.item_def_id);
    const std::string rarity = def ? def->rarity : "common";
    const Color rarityCol = Ui::RarityColor(rarity);

    const Ui::Rect slotR = SlotRect(slotIndex);
    const float tw = static_cast<float>(MeasureText(name.c_str(), 13));
    const float tipW = tw + 20.0f;
    const float tipH = 46.0f;
    float tipX = slotR.x + slotSize_ + 4;
    float tipY = slotR.y;
    /* Clamp to screen */
    if (tipX + tipW > GetScreenWidth())  tipX = slotR.x - tipW - 4;
    if (tipY + tipH > GetScreenHeight()) tipY = static_cast<float>(GetScreenHeight()) - tipH - 4;

    DrawRectangle(static_cast<int>(tipX), static_cast<int>(tipY),
                  static_cast<int>(tipW), static_cast<int>(tipH), Color{15, 18, 25, 230});
    DrawRectangleLinesEx(Ui::Rect{tipX, tipY, tipW, tipH}, 1.0f, rarityCol);
    DrawText(name.c_str(), static_cast<int>(tipX) + 8, static_cast<int>(tipY) + 8, 13, WHITE);
    /* Rarity label */
    DrawText(rarity.c_str(), static_cast<int>(tipX) + 8, static_cast<int>(tipY) + 26, 10, rarityCol);
}

void InventoryUi::AnchorWindow() {
    const Ui::Rect anchored = Ui::ResolveAnchoredRect(windowConfig_,
                                                      static_cast<float>(GetScreenWidth()),
                                                      static_cast<float>(GetScreenHeight()));
    windowX_ = anchored.x;
    windowY_ = anchored.y;
    windowW_ = anchored.width;
    windowH_ = anchored.height;
    ComputeLayout();
}
