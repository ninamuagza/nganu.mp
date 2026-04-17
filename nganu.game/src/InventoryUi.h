#pragma once

/* ------------------------------------------------------------------ */
/* InventoryUi.h — inventory window: slot grid, drag-drop, actions    */
/* Reads ClientInventory + ItemDefs, uses AssetManager for icons,     */
/* sends inventory opcodes via NetworkClient.                         */
/* ------------------------------------------------------------------ */

#include "ui/UiSystem.h"
#include "ui/UiTheme.h"
#include "ui/UiWindowConfig.h"
#include "InventoryState.h"
#include "ItemDefs.h"
#include "AssetManager.h"
#include "raylib.h"
#include <functional>
#include <string>

/* Callbacks for sending network actions */
struct InventoryNetCallbacks {
    std::function<void()>               sendOpen;
    std::function<void()>               sendClose;
    std::function<void(int)>            sendUseItem;
    std::function<void(int from, int to)> sendMoveItem;
    std::function<void(int)>            sendDropItem;
};

class InventoryUi : public Ui::Widget {
public:
    InventoryUi(const ClientInventory*  inventory,
                const ItemDefs*         itemDefs,
                const AssetManager*     assets,
                const Ui::Theme*        theme,
                InventoryNetCallbacks   callbacks);

    /* UiWidget interface */
    void Update(float dt) override;
    void Draw() const override;

    /* Called externally to open/close */
    void Open();
    void Close();
    void Toggle();

    /* Apply full state from server */
    void ApplyFullState(const ClientInventory& inv);

    /* Apply single slot update from server */
    void ApplySlotUpdate(int slotIndex, bool occupied, int itemDefId, int amount, uint8_t flags);
    void ApplyWindowConfig(const Ui::WindowConfig& config) override;

    /* Getter */
    bool IsOpen() const { return IsVisible(); }

private:
    /* Config (overridable from inventory_main.json) */
    int   slotSize_    = 48;
    int   columns_     = 5;
    int   rows_        = 4;
    int   padding_     = 12;
    int   slotSpacing_ = 4;
    std::string title_ = "Inventory";
    bool showUseButton_ = true;
    bool showDropButton_ = true;
    float windowW_     = 0.0f;
    float windowH_     = 0.0f;
    float windowX_     = 100.0f;
    float windowY_     = 100.0f;

    const ClientInventory* inventory_ = nullptr;
    const ItemDefs*        itemDefs_  = nullptr;
    const AssetManager*    assets_    = nullptr;
    const Ui::Theme*       theme_     = nullptr;
    InventoryNetCallbacks  callbacks_;

    /* Drag state */
    int  dragSlot_   = -1;
    bool dragging_   = false;
    Vector2 dragOffset_ {};

    /* Hover / selection */
    int  hoverSlot_    = -1;
    int  selectedSlot_ = -1;

    /* Drag target prediction (visual only, rolled back if server rejects) */
    int pendingFrom_ = -1;
    int pendingTo_   = -1;

    /* Window dragging */
    bool  windowDrag_   = false;
    Vector2 windowDragOff_ {};
    bool  userMovedWindow_ = false;
    Ui::WindowConfig windowConfig_ {};

    Ui::Rect SlotRect(int index) const;
    int  SlotAtPoint(Vector2 p) const;
    void DrawSlot(int index) const;
    void DrawTooltip(int slotIndex) const;
    void ComputeLayout();
    void AnchorWindow();
};
