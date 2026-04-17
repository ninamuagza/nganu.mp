#include "NetworkClient.h"

#include <cstring>
#include <utility>

namespace {
constexpr int kChannelCount = 2;
}

NetworkClient::NetworkClient() {
    if (enet_initialize() != 0) {
        statusText_ = "ENet init failed";
        return;
    }

    client_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!client_) {
        statusText_ = "ENet host failed";
        enet_deinitialize();
    }
}

NetworkClient::~NetworkClient() {
    Disconnect();
    if (client_) {
        enet_host_destroy(client_);
        client_ = nullptr;
    }
    enet_deinitialize();
}

bool NetworkClient::Connect(const std::string& host, uint16_t port) {
    if (!client_) return false;
    if (peer_) return true;

    ENetAddress address {};
    if (enet_address_set_host(&address, host.c_str()) != 0) {
        statusText_ = "DNS/host lookup failed";
        PushEvent(NetworkEvent {NetworkEvent::Type::ConnectionFailed});
        return false;
    }
    address.port = port;

    peer_ = enet_host_connect(client_, &address, kChannelCount, 0);
    if (!peer_) {
        statusText_ = "Connect allocation failed";
        PushEvent(NetworkEvent {NetworkEvent::Type::ConnectionFailed});
        return false;
    }

    awaitingConnect_ = true;
    connectTimeout_ = 0.0f;
    statusText_ = "Connecting to " + host + ":" + std::to_string(port);
    return true;
}

void NetworkClient::Update(float dt) {
    if (!client_) return;

    if (awaitingConnect_ && peer_ && peer_->state == ENET_PEER_STATE_CONNECTING) {
        connectTimeout_ += dt;
        if (connectTimeout_ >= 3.0f) {
            enet_peer_reset(peer_);
            peer_ = nullptr;
            awaitingConnect_ = false;
            connectTimeout_ = 0.0f;
            statusText_ = "Connect timeout";
            PushEvent(NetworkEvent {NetworkEvent::Type::ConnectionFailed});
            return;
        }
    }

    ENetEvent event {};
    while (enet_host_service(client_, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            awaitingConnect_ = false;
            connectTimeout_ = 0.0f;
            statusText_ = "Connected";
            PushEvent(NetworkEvent {NetworkEvent::Type::Connected});
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            peer_ = nullptr;
            awaitingConnect_ = false;
            connectTimeout_ = 0.0f;
            statusText_ = "Disconnected";
            PushEvent(NetworkEvent {NetworkEvent::Type::Disconnected});
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            HandlePacket(event.packet->data, event.packet->dataLength);
            enet_packet_destroy(event.packet);
            break;
        default:
            break;
        }
    }
}

void NetworkClient::Disconnect() {
    if (!peer_) return;

    awaitingConnect_ = false;
    connectTimeout_ = 0.0f;
    enet_peer_disconnect(peer_, 0);
    enet_host_flush(client_);

    ENetEvent event {};
    const uint32_t disconnectWaitMs = 250;
    uint32_t waitedMs = 0;
    while (waitedMs < disconnectWaitMs && enet_host_service(client_, &event, 50) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            peer_ = nullptr;
            break;
        }
        waitedMs += 50;
    }

    if (peer_) {
        enet_peer_reset(peer_);
        peer_ = nullptr;
    }

    statusText_ = "Offline";
}

bool NetworkClient::SendPlayerPosition(float x, float y) {
    if (!IsConnected()) return false;

    uint8_t pkt[1 + sizeof(float) * 2];
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLAYER_DATA);
    std::memcpy(pkt + 1, &x, sizeof(x));
    std::memcpy(pkt + 1 + sizeof(x), &y, sizeof(y));

    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), 0);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendChatMessage(const std::string& text) {
    if (!IsConnected() || text.empty()) return false;

    std::vector<uint8_t> pkt(1 + text.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::CHAT_MESSAGE);
    std::memcpy(pkt.data() + 1, text.data(), text.size());

    ENetPacket* packet = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendPlayerName(const std::string& name) {
    if (!IsConnected() || name.empty() || name.size() > 24) return false;

    std::vector<uint8_t> pkt(2 + name.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
    pkt[1] = static_cast<uint8_t>(PluginMessageType::PLAYER_NAME);
    std::memcpy(pkt.data() + 2, name.data(), name.size());

    ENetPacket* packet = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendObjectInteract(const std::string& objectId) {
    if (!IsConnected() || objectId.empty()) return false;

    std::vector<uint8_t> pkt(1 + objectId.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::OBJECT_INTERACT);
    std::memcpy(pkt.data() + 1, objectId.data(), objectId.size());

    ENetPacket* packet = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::RequestUpdateManifest() {
    if (!IsConnected()) return false;

    uint8_t pkt[2];
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
    pkt[1] = static_cast<uint8_t>(PluginMessageType::UPDATE_PROBE);

    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::RequestAsset(const std::string& assetKey) {
    if (!IsConnected() || assetKey.empty()) return false;

    std::vector<uint8_t> pkt(2 + assetKey.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
    pkt[1] = static_cast<uint8_t>(PluginMessageType::ASSET_REQUEST);
    std::memcpy(pkt.data() + 2, assetKey.data(), assetKey.size());

    ENetPacket* packet = enet_packet_create(pkt.data(), pkt.size(), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendInventoryOpen() {
    if (!IsConnected()) return false;
    const uint8_t pkt[2] {
        static_cast<uint8_t>(PacketOpcode::INVENTORY),
        static_cast<uint8_t>(InventoryMsgType::CMSG_OPEN)
    };
    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendInventoryClose() {
    if (!IsConnected()) return false;
    const uint8_t pkt[2] {
        static_cast<uint8_t>(PacketOpcode::INVENTORY),
        static_cast<uint8_t>(InventoryMsgType::CMSG_CLOSE)
    };
    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendMoveItem(int fromSlot, int toSlot) {
    if (!IsConnected() || fromSlot < 0 || fromSlot > 255 || toSlot < 0 || toSlot > 255) return false;
    const uint8_t pkt[4] {
        static_cast<uint8_t>(PacketOpcode::INVENTORY),
        static_cast<uint8_t>(InventoryMsgType::CMSG_MOVE_ITEM),
        static_cast<uint8_t>(fromSlot),
        static_cast<uint8_t>(toSlot)
    };
    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendUseItem(int slot) {
    if (!IsConnected() || slot < 0 || slot > 255) return false;
    const uint8_t pkt[3] {
        static_cast<uint8_t>(PacketOpcode::INVENTORY),
        static_cast<uint8_t>(InventoryMsgType::CMSG_USE_ITEM),
        static_cast<uint8_t>(slot)
    };
    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::SendDropItem(int slot) {
    if (!IsConnected() || slot < 0 || slot > 255) return false;
    const uint8_t pkt[3] {
        static_cast<uint8_t>(PacketOpcode::INVENTORY),
        static_cast<uint8_t>(InventoryMsgType::CMSG_DROP_ITEM),
        static_cast<uint8_t>(slot)
    };
    ENetPacket* packet = enet_packet_create(pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    return enet_peer_send(peer_, 0, packet) == 0;
}

bool NetworkClient::IsConnected() const {
    return peer_ && peer_->state == ENET_PEER_STATE_CONNECTED;
}

std::vector<NetworkEvent> NetworkClient::ConsumeEvents() {
    std::vector<NetworkEvent> out;
    out.swap(events_);
    return out;
}

void NetworkClient::PushEvent(NetworkEvent event) {
    events_.push_back(std::move(event));
}

void NetworkClient::HandlePacket(const void* data, size_t len) {
    if (len < 1) return;

    switch (Protocol::readOpcode(data, len)) {
    case PacketOpcode::HANDSHAKE: {
        if (Protocol::payloadLen(len) != sizeof(int32_t)) return;
        int32_t localId = 0;
        std::memcpy(&localId, Protocol::payload(data), sizeof(localId));
        NetworkEvent event {};
        event.type = NetworkEvent::Type::Handshake;
        event.playerId = static_cast<int>(localId);
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::PLAYER_MOVE: {
        if (Protocol::payloadLen(len) != sizeof(int32_t) + sizeof(float) * 2) return;
        NetworkEvent event {};
        event.type = NetworkEvent::Type::PlayerMoved;
        std::memcpy(&event.playerId, Protocol::payload(data), sizeof(int32_t));
        std::memcpy(&event.x, Protocol::payload(data) + sizeof(int32_t), sizeof(float));
        std::memcpy(&event.y, Protocol::payload(data) + sizeof(int32_t) + sizeof(float), sizeof(float));
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::CHAT_MESSAGE: {
        if (Protocol::payloadLen(len) < sizeof(int32_t) + 1) return;
        NetworkEvent event {};
        event.type = NetworkEvent::Type::ChatMessage;
        std::memcpy(&event.senderId, Protocol::payload(data), sizeof(int32_t));
        const char* text = reinterpret_cast<const char*>(Protocol::payload(data) + sizeof(int32_t));
        const size_t textLen = Protocol::payloadLen(len) - sizeof(int32_t);
        event.text.assign(text, textLen);
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::GAME_STATE: {
        if (Protocol::payloadLen(len) < 1) return;
        const uint8_t* payload = Protocol::payload(data);
        const GameStateType type = static_cast<GameStateType>(payload[0]);

        switch (type) {
        case GameStateType::SNAPSHOT: {
            if (Protocol::payloadLen(len) < 1 + sizeof(uint16_t)) return;
            uint16_t count = 0;
            std::memcpy(&count, payload + 1, sizeof(count));
            size_t offset = 1 + sizeof(count);
            for (uint16_t i = 0; i < count; ++i) {
                if (offset + sizeof(int32_t) + sizeof(float) * 2 + sizeof(uint8_t) > Protocol::payloadLen(len)) break;
                NetworkEvent event {};
                event.type = NetworkEvent::Type::SnapshotPlayer;
                std::memcpy(&event.playerId, payload + offset, sizeof(int32_t));
                offset += sizeof(int32_t);
                std::memcpy(&event.x, payload + offset, sizeof(float));
                offset += sizeof(float);
                std::memcpy(&event.y, payload + offset, sizeof(float));
                offset += sizeof(float);
                const uint8_t nameLen = *(payload + offset);
                offset += sizeof(uint8_t);
                if (offset + nameLen > Protocol::payloadLen(len)) break;
                event.text.assign(reinterpret_cast<const char*>(payload + offset), nameLen);
                offset += nameLen;
                PushEvent(std::move(event));
            }
            break;
        }
        case GameStateType::PLAYER_JOIN: {
            if (Protocol::payloadLen(len) != 1 + sizeof(int32_t) + sizeof(float) * 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerJoined;
            std::memcpy(&event.playerId, payload + 1, sizeof(int32_t));
            std::memcpy(&event.x, payload + 1 + sizeof(int32_t), sizeof(float));
            std::memcpy(&event.y, payload + 1 + sizeof(int32_t) + sizeof(float), sizeof(float));
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::PLAYER_LEAVE: {
            if (Protocol::payloadLen(len) != 1 + sizeof(int32_t)) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerLeft;
            std::memcpy(&event.playerId, payload + 1, sizeof(int32_t));
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::SERVER_TEXT: {
            if (Protocol::payloadLen(len) < 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::ServerText;
            event.text.assign(reinterpret_cast<const char*>(payload + 1), Protocol::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::PLAYER_NAME: {
            if (Protocol::payloadLen(len) < 1 + sizeof(int32_t) + sizeof(uint8_t)) return;
            int32_t playerId = 0;
            std::memcpy(&playerId, payload + 1, sizeof(playerId));
            const uint8_t nameLen = *(payload + 1 + sizeof(playerId));
            if (Protocol::payloadLen(len) != 1 + sizeof(playerId) + sizeof(uint8_t) + nameLen) return;

            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerName;
            event.playerId = static_cast<int>(playerId);
            event.text.assign(reinterpret_cast<const char*>(payload + 1 + sizeof(playerId) + sizeof(uint8_t)), nameLen);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::OBJECTIVE_TEXT: {
            NetworkEvent event {};
            event.type = NetworkEvent::Type::ObjectiveText;
            event.text.assign(reinterpret_cast<const char*>(payload + 1), Protocol::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::MAP_TRANSFER: {
            if (Protocol::payloadLen(len) < 1 + sizeof(float) * 2 + sizeof(uint8_t)) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::MapTransfer;
            size_t offset = 1;
            std::memcpy(&event.x, payload + offset, sizeof(float));
            offset += sizeof(float);
            std::memcpy(&event.y, payload + offset, sizeof(float));
            offset += sizeof(float);
            const uint8_t mapLen = *(payload + offset);
            offset += sizeof(uint8_t);
            if (offset + mapLen > Protocol::payloadLen(len)) return;
            event.mapId.assign(reinterpret_cast<const char*>(payload + offset), mapLen);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::PLAYER_POSITION: {
            if (Protocol::payloadLen(len) != 1 + sizeof(float) * 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerPosition;
            std::memcpy(&event.x, payload + 1, sizeof(float));
            std::memcpy(&event.y, payload + 1 + sizeof(float), sizeof(float));
            PushEvent(std::move(event));
            break;
        }
        default:
            break;
        }
        break;
    }
    case PacketOpcode::PLUGIN_MESSAGE: {
        if (Protocol::payloadLen(len) < 1) return;
        const PluginMessageType type = static_cast<PluginMessageType>(Protocol::payload(data)[0]);
        if (type == PluginMessageType::UPDATE_MANIFEST) {
            NetworkEvent event {};
            event.type = NetworkEvent::Type::AssetManifest;
            event.text.assign(reinterpret_cast<const char*>(Protocol::payload(data) + 1), Protocol::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        if (type == PluginMessageType::ASSET_BLOB) {
            NetworkEvent event {};
            event.type = NetworkEvent::Type::AssetBlob;
            event.text.assign(reinterpret_cast<const char*>(Protocol::payload(data) + 1), Protocol::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        break;
    }
    case PacketOpcode::INVENTORY: {
        if (Protocol::payloadLen(len) < 1) return;
        const uint8_t* payload = Protocol::payload(data);
        const InventoryMsgType type = static_cast<InventoryMsgType>(payload[0]);
        switch (type) {
        case InventoryMsgType::SMSG_FULL_STATE: {
            if (Protocol::payloadLen(len) < 3) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::InventoryFullState;
            event.rawBytes.assign(payload, payload + Protocol::payloadLen(len));
            PushEvent(std::move(event));
            break;
        }
        case InventoryMsgType::SMSG_SLOT_UPDATE: {
            if (Protocol::payloadLen(len) != 8) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::InventorySlotUpdate;
            event.slotIndex = static_cast<int>(payload[1]);
            event.occupied = payload[2] != 0;
            event.itemDefId = static_cast<int>(payload[3] | (payload[4] << 8));
            event.amount = static_cast<int>(payload[5] | (payload[6] << 8));
            event.flags = payload[7];
            PushEvent(std::move(event));
            break;
        }
        case InventoryMsgType::SMSG_ERROR: {
            if (Protocol::payloadLen(len) != 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::InventoryError;
            event.errCode = payload[1];
            PushEvent(std::move(event));
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}
