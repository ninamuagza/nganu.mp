#pragma once

#include <cstddef>
#include <cstdint>

enum class PacketOpcode : uint8_t {
    HANDSHAKE      = 0x01,
    PLAYER_DATA    = 0x02,
    GAME_STATE     = 0x03,
    PLAYER_MOVE    = 0x04,
    OBJECT_INTERACT= 0x05,
    PLUGIN_MESSAGE = 0x10,
    CHAT_MESSAGE   = 0x11,
    INVENTORY      = 0x12,   /* client <-> server inventory messages */
};

enum class InventoryMsgType : uint8_t {
    /* Client → Server */
    CMSG_OPEN       = 0x01,
    CMSG_CLOSE      = 0x02,
    CMSG_USE_ITEM   = 0x03,
    CMSG_MOVE_ITEM  = 0x04,
    CMSG_DROP_ITEM  = 0x05,
    /* Server → Client */
    SMSG_OPEN       = 0x10,
    SMSG_FULL_STATE = 0x11,
    SMSG_SLOT_UPDATE= 0x12,
    SMSG_ERROR      = 0x13,
};

enum class InvError : uint8_t {
    INVALID_SLOT        = 1,
    ITEM_NOT_FOUND      = 2,
    TARGET_FULL         = 3,
    ACTION_NOT_ALLOWED  = 4,
    COOLDOWN            = 5,
};

enum class GameStateType : uint8_t {
    SNAPSHOT = 0x01,
    PLAYER_JOIN = 0x02,
    PLAYER_LEAVE = 0x03,
    SERVER_TEXT = 0x04,
    PLAYER_NAME = 0x05,
    OBJECTIVE_TEXT = 0x06,
    MAP_TRANSFER = 0x07,
    PLAYER_POSITION = 0x08
};

enum class PluginMessageType : uint8_t {
    PLAYER_NAME = 0x01,
    HEARTBEAT = 0x02,
    UPDATE_PROBE = 0x20,
    UPDATE_MANIFEST = 0x21,
    ASSET_REQUEST = 0x22,
    ASSET_BLOB = 0x23
};

namespace Protocol {

constexpr uint16_t kProtocolVersion = 2;
constexpr size_t kAssetChunkBytes = 24 * 1024;

inline PacketOpcode readOpcode(const void* data, size_t len) {
    if (len < 1) return static_cast<PacketOpcode>(0);
    return static_cast<PacketOpcode>(static_cast<const uint8_t*>(data)[0]);
}

inline const uint8_t* payload(const void* data) {
    return static_cast<const uint8_t*>(data) + 1;
}

inline size_t payloadLen(size_t totalLen) {
    return (totalLen > 0) ? totalLen - 1 : 0;
}

} // namespace Protocol
