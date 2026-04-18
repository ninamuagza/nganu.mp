#include "inventory/InventoryService.h"

#include <algorithm>

/* ------------------------------------------------------------------ */
void InventoryService::createInventory(int playerid) {
    ContainerState container;
    container.player_id    = playerid;
    container.container_id = nextContainerId_++;
    container.revision     = 0;
    container.open         = false;
    container.slots.resize(kInventorySize);
    for (int i = 0; i < kInventorySize; ++i) {
        container.slots[i].slot_index = i;
    }
    inventories_[playerid] = std::move(container);
    giveStarterItems(playerid);
}

void InventoryService::removeInventory(int playerid) {
    inventories_.erase(playerid);
}

ContainerState* InventoryService::getInventory(int playerid) {
    auto it = inventories_.find(playerid);
    return it != inventories_.end() ? &it->second : nullptr;
}

const ContainerState* InventoryService::getInventory(int playerid) const {
    auto it = inventories_.find(playerid);
    return it != inventories_.end() ? &it->second : nullptr;
}

void InventoryService::setOpen(int playerid, bool open) {
    auto* inv = getInventory(playerid);
    if (inv) inv->open = open;
}

/* ------------------------------------------------------------------ */
bool InventoryService::validSlot(int playerid, int slot_index) const {
    const auto* inv = getInventory(playerid);
    if (!inv) return false;
    return slot_index >= 0 && slot_index < static_cast<int>(inv->slots.size());
}

InvActionResult InventoryService::moveItem(int playerid, int slot_from, int slot_to) {
    if (!validSlot(playerid, slot_from) || !validSlot(playerid, slot_to))
        return InvActionResult::INVALID_SLOT;
    if (slot_from == slot_to)
        return InvActionResult::OK;

    auto* inv = getInventory(playerid);
    SlotState& from = inv->slots[slot_from];
    SlotState& to   = inv->slots[slot_to];

    if (!from.occupied)
        return InvActionResult::NO_ITEM;

    /* Merge stack if same item */
    if (to.occupied && to.item_def_id == from.item_def_id) {
        to.amount += from.amount;
        from = SlotState{};
        from.slot_index = slot_from;
    } else {
        /* Swap */
        std::swap(from, to);
        from.slot_index = slot_from;
        to.slot_index   = slot_to;
    }

    ++inv->revision;
    return InvActionResult::OK;
}

InvActionResult InventoryService::useItem(int playerid, int slot_index) {
    if (!validSlot(playerid, slot_index))
        return InvActionResult::INVALID_SLOT;

    auto* inv = getInventory(playerid);
    SlotState& slot = inv->slots[slot_index];

    if (!slot.occupied)
        return InvActionResult::NO_ITEM;

    if (!(slot.flags & 0x01))   /* CAN_USE bit */
        return InvActionResult::NOT_ALLOWED;

    /* MVP: just consume one */
    --slot.amount;
    if (slot.amount <= 0) {
        slot = SlotState{};
        slot.slot_index = slot_index;
    }
    ++inv->revision;
    return InvActionResult::OK;
}

InvActionResult InventoryService::dropItem(int playerid, int slot_index) {
    if (!validSlot(playerid, slot_index))
        return InvActionResult::INVALID_SLOT;

    auto* inv = getInventory(playerid);
    SlotState& slot = inv->slots[slot_index];

    if (!slot.occupied)
        return InvActionResult::NO_ITEM;

    if (!(slot.flags & 0x02))   /* CAN_DROP bit */
        return InvActionResult::NOT_ALLOWED;

    slot = SlotState{};
    slot.slot_index = slot_index;
    ++inv->revision;
    return InvActionResult::OK;
}

void InventoryService::setSlot(int playerid, int slot_index, int item_def_id, int amount, uint8_t flags) {
    if (!validSlot(playerid, slot_index)) return;
    auto* inv = getInventory(playerid);
    SlotState& slot    = inv->slots[slot_index];
    slot.slot_index    = slot_index;
    slot.occupied      = (item_def_id > 0 && amount > 0);
    slot.item_def_id   = item_def_id;
    slot.amount        = amount;
    slot.flags         = flags;
    ++inv->revision;
}

void InventoryService::clearSlot(int playerid, int slot_index) {
    if (!validSlot(playerid, slot_index)) return;
    auto* inv = getInventory(playerid);
    inv->slots[slot_index] = SlotState{};
    inv->slots[slot_index].slot_index = slot_index;
    ++inv->revision;
}

int InventoryService::countItem(int playerid, int item_def_id) const {
    const auto* inv = getInventory(playerid);
    if (!inv || item_def_id <= 0) return 0;

    int total = 0;
    for (const SlotState& slot : inv->slots) {
        if (!slot.occupied || slot.item_def_id != item_def_id) continue;
        total += slot.amount;
    }
    return total;
}

int InventoryService::findFirstSlotWithItem(int playerid, int item_def_id) const {
    const auto* inv = getInventory(playerid);
    if (!inv || item_def_id <= 0) return -1;

    for (const SlotState& slot : inv->slots) {
        if (slot.occupied && slot.item_def_id == item_def_id) {
            return slot.slot_index;
        }
    }
    return -1;
}

int InventoryService::findFirstFreeSlot(int playerid) const {
    const auto* inv = getInventory(playerid);
    if (!inv) return -1;

    for (const SlotState& slot : inv->slots) {
        if (!slot.occupied) {
            return slot.slot_index;
        }
    }
    return -1;
}

bool InventoryService::addItem(int playerid, int item_def_id, int amount, uint8_t flags) {
    auto* inv = getInventory(playerid);
    if (!inv || item_def_id <= 0 || amount <= 0) return false;

    int remaining = amount;

    for (SlotState& slot : inv->slots) {
        if (!slot.occupied || slot.item_def_id != item_def_id) continue;
        slot.amount += remaining;
        slot.flags = flags;
        ++inv->revision;
        return true;
    }

    while (remaining > 0) {
        const int freeSlot = findFirstFreeSlot(playerid);
        if (freeSlot < 0) {
            return false;
        }
        setSlot(playerid, freeSlot, item_def_id, remaining, flags);
        remaining = 0;
    }

    return true;
}

bool InventoryService::removeItem(int playerid, int item_def_id, int amount) {
    auto* inv = getInventory(playerid);
    if (!inv || item_def_id <= 0 || amount <= 0) return false;
    if (countItem(playerid, item_def_id) < amount) return false;

    int remaining = amount;
    for (SlotState& slot : inv->slots) {
        if (!slot.occupied || slot.item_def_id != item_def_id) continue;
        const int take = std::min(slot.amount, remaining);
        slot.amount -= take;
        remaining -= take;
        if (slot.amount <= 0) {
            slot = SlotState {};
            slot.slot_index = (&slot - inv->slots.data());
        }
        ++inv->revision;
        if (remaining <= 0) {
            return true;
        }
    }

    return remaining <= 0;
}

void InventoryService::giveStarterItems(int playerid) {
    /* item_def_id, amount, flags (CAN_USE=1, CAN_DROP=2, CAN_MOVE=4) */
    setSlot(playerid, 0, 1, 5, 7); /* 5x Health Potion */
    setSlot(playerid, 1, 2, 1, 6); /* 1x Iron Sword (not usable directly) */
    setSlot(playerid, 2, 3, 1, 6); /* 1x Leather Armor */
    setSlot(playerid, 3, 7, 10,7); /* 10x Gold Coin */
}
