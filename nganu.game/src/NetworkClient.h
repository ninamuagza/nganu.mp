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
        MapTransfer
    };

    Type type {};
    int playerId = 0;
    int senderId = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
    std::string mapId;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool Connect(const std::string& host, uint16_t port);
    void Update(float dt);
    void Disconnect();

    bool SendPlayerPosition(float x, float y);
    bool SendChatMessage(const std::string& text);
    bool SendPlayerName(const std::string& name);
    bool SendObjectInteract(const std::string& objectId);
    bool RequestUpdateManifest();
    bool RequestAsset(const std::string& assetKey);

    bool IsConnected() const;
    std::string StatusText() const { return statusText_; }
    std::vector<NetworkEvent> ConsumeEvents();

private:
    ENetHost* client_ = nullptr;
    ENetPeer* peer_ = nullptr;
    std::string statusText_ = "Offline";
    std::vector<NetworkEvent> events_;
    bool awaitingConnect_ = false;
    float connectTimeout_ = 0.0f;

    void PushEvent(NetworkEvent event);
    void HandlePacket(const void* data, size_t len);
};
