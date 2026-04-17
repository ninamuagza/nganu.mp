#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SlotState {
    int     slot_index  = 0;
    bool    occupied    = false;
    int     item_def_id = 0;
    int     amount      = 0;
    uint8_t flags       = 0;   /* bitmask: CAN_USE=1, CAN_DROP=2, CAN_MOVE=4 */
};

struct ContainerState {
    int                      player_id    = 0;
    int                      container_id = 0;
    uint32_t                 revision     = 0;
    bool                     open         = false;
    std::vector<SlotState>   slots;
};

enum class InvActionResult : uint8_t {
    OK            = 0,
    INVALID_SLOT  = 1,
    NO_ITEM       = 2,
    TARGET_FULL   = 3,
    NOT_ALLOWED   = 4,
};

class InventoryService {
public:
    static constexpr int kInventorySize = 20;

    /* Initialise a fresh inventory for a newly connected player */
    void createInventory(int playerid);

    /* Remove inventory state when player disconnects */
    void removeInventory(int playerid);

    /* Get the container state (nullptr if not found) */
    ContainerState* getInventory(int playerid);
    const ContainerState* getInventory(int playerid) const;

    /* Mark inventory as open / closed */
    void setOpen(int playerid, bool open);

    /* Move item from slot_from to slot_to within the same container */
    InvActionResult moveItem(int playerid, int slot_from, int slot_to);

    /* Use (consume) the item in a slot */
    InvActionResult useItem(int playerid, int slot_index);

    /* Drop the item in a slot (removes it) */
    InvActionResult dropItem(int playerid, int slot_index);

    /* Directly set a slot (for Lua scripting) */
    void setSlot(int playerid, int slot_index, int item_def_id, int amount, uint8_t flags = 7);

    /* Clear a slot */
    void clearSlot(int playerid, int slot_index);

    /* Count total amount of an item in inventory */
    int countItem(int playerid, int item_def_id) const;

    /* Find first occupied slot for an item, returns -1 if not found */
    int findFirstSlotWithItem(int playerid, int item_def_id) const;

    /* Find first free slot, returns -1 if inventory full */
    int findFirstFreeSlot(int playerid) const;

    /* Add item into inventory, stacking where possible. Returns false if not enough space */
    bool addItem(int playerid, int item_def_id, int amount, uint8_t flags = 7);

    /* Remove item amount across stacks. Returns false if player does not have enough */
    bool removeItem(int playerid, int item_def_id, int amount);

    /* Give test/starter items to a fresh player */
    void giveStarterItems(int playerid);

private:
    std::unordered_map<int, ContainerState> inventories_;
    int nextContainerId_ = 1;

    bool validSlot(int playerid, int slot_index) const;
};
