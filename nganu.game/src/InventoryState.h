#pragma once

/* ------------------------------------------------------------------ */
/* InventoryState.h — client-side cache of server inventory state.    */
/* Updated by SMSG_FULL_STATE and SMSG_SLOT_UPDATE packets.           */
/* ------------------------------------------------------------------ */

#include <cstdint>
#include <vector>

struct ClientSlot {
    int     slot_index  = 0;
    bool    occupied    = false;
    int     item_def_id = 0;
    int     amount      = 0;
    uint8_t flags       = 0;  /* CAN_USE=1, CAN_DROP=2, CAN_MOVE=4 */
};

struct ClientInventory {
    bool                    open         = false;
    int                     container_id = 0;
    uint32_t                revision     = 0;
    std::vector<ClientSlot> slots;

    /* Helpers */
    void resize(int count) {
        slots.resize(count);
        for (int i = 0; i < count; ++i) slots[i].slot_index = i;
    }

    ClientSlot* slotAt(int index) {
        if (index < 0 || index >= static_cast<int>(slots.size())) return nullptr;
        return &slots[index];
    }
};
