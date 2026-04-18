#pragma once

#include "shared/Protocol.h"

#include <enet/enet.h>
#include <cstdint>
#include <string>
#include <vector>

struct NetworkEvent {
    enum class Type {
        Connected,
        ConnectionFailed,
        Disconnected,
        LocalServerFound,
        Handshake,
        AssetManifest,
        AssetBlob,
        SnapshotPlayer,
        PlayerJoined,
        PlayerLeft,
        PlayerMoved,
        PlayerPosition,
        ChatMessage,
        ServerText,
        PlayerName,
        ObjectiveText,
        MapTransfer,
        /* Inventory */
        InventoryFullState,
        InventorySlotUpdate,
        InventoryError,
    };

    Type type {};
    int playerId = 0;
    int senderId = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
    std::string mapId;
    /* Inventory payload (for InventoryFullState / InventorySlotUpdate) */
    std::vector<uint8_t> rawBytes;  /* full state blob */
    int  slotIndex  = -1;
    bool occupied   = false;
    int  itemDefId  = 0;
    int  amount     = 0;
    uint8_t flags   = 0;
    uint8_t errCode = 0;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool Connect(const std::string& host, uint16_t port);
    bool BeginLocalDiscovery(uint16_t port);
    void Update(float dt);
    void Disconnect();

    bool SendPlayerPosition(float x, float y);
    bool SendChatMessage(const std::string& text);
    bool SendPlayerName(const std::string& name);
    bool SendObjectInteract(const std::string& objectId);
    bool RequestUpdateManifest();
    bool RequestAsset(const std::string& assetKey);
    /* Inventory */
    bool SendInventoryOpen();
    bool SendInventoryClose();
    bool SendMoveItem(int fromSlot, int toSlot);
    bool SendUseItem(int slot);
    bool SendDropItem(int slot);

    bool IsConnected() const;
    std::string StatusText() const { return statusText_; }
    std::vector<NetworkEvent> ConsumeEvents();

private:
    ENetHost* client_ = nullptr;
    ENetPeer* peer_ = nullptr;
    ENetSocket discoverySocket_ = ENET_SOCKET_NULL;
    std::string statusText_ = "Offline";
    std::vector<NetworkEvent> events_;
    bool awaitingConnect_ = false;
    float connectTimeout_ = 0.0f;
    bool discoveryActive_ = false;
    float discoveryElapsed_ = 0.0f;
    float discoverySendTimer_ = 0.0f;
    uint16_t discoveryGamePort_ = 0;
    uint16_t discoveryPort_ = 0;

    void PushEvent(NetworkEvent event);
    void HandlePacket(const void* data, size_t len);
    void StopDiscovery();
    void SendDiscoveryProbe();
    void PollDiscovery();
};
