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

    ENetEvent event {};
    while (enet_host_service(client_, &event, 50) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            peer_ = nullptr;
            break;
        }
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

    switch (Packet::readOpcode(data, len)) {
    case PacketOpcode::HANDSHAKE: {
        if (Packet::payloadLen(len) != sizeof(int32_t)) return;
        int32_t localId = 0;
        std::memcpy(&localId, Packet::payload(data), sizeof(localId));
        NetworkEvent event {};
        event.type = NetworkEvent::Type::Handshake;
        event.playerId = static_cast<int>(localId);
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::PLAYER_MOVE: {
        if (Packet::payloadLen(len) != sizeof(int32_t) + sizeof(float) * 2) return;
        NetworkEvent event {};
        event.type = NetworkEvent::Type::PlayerMoved;
        std::memcpy(&event.playerId, Packet::payload(data), sizeof(int32_t));
        std::memcpy(&event.x, Packet::payload(data) + sizeof(int32_t), sizeof(float));
        std::memcpy(&event.y, Packet::payload(data) + sizeof(int32_t) + sizeof(float), sizeof(float));
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::CHAT_MESSAGE: {
        if (Packet::payloadLen(len) < sizeof(int32_t) + 1) return;
        NetworkEvent event {};
        event.type = NetworkEvent::Type::ChatMessage;
        std::memcpy(&event.senderId, Packet::payload(data), sizeof(int32_t));
        const char* text = reinterpret_cast<const char*>(Packet::payload(data) + sizeof(int32_t));
        const size_t textLen = Packet::payloadLen(len) - sizeof(int32_t);
        event.text.assign(text, textLen);
        PushEvent(std::move(event));
        break;
    }
    case PacketOpcode::GAME_STATE: {
        if (Packet::payloadLen(len) < 1) return;
        const uint8_t* payload = Packet::payload(data);
        const GameStateType type = static_cast<GameStateType>(payload[0]);

        switch (type) {
        case GameStateType::SNAPSHOT: {
            if (Packet::payloadLen(len) < 1 + sizeof(uint16_t)) return;
            uint16_t count = 0;
            std::memcpy(&count, payload + 1, sizeof(count));
            size_t offset = 1 + sizeof(count);
            for (uint16_t i = 0; i < count; ++i) {
                if (offset + sizeof(int32_t) + sizeof(float) * 2 + sizeof(uint8_t) > Packet::payloadLen(len)) break;
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
                if (offset + nameLen > Packet::payloadLen(len)) break;
                event.text.assign(reinterpret_cast<const char*>(payload + offset), nameLen);
                offset += nameLen;
                PushEvent(std::move(event));
            }
            break;
        }
        case GameStateType::PLAYER_JOIN: {
            if (Packet::payloadLen(len) != 1 + sizeof(int32_t) + sizeof(float) * 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerJoined;
            std::memcpy(&event.playerId, payload + 1, sizeof(int32_t));
            std::memcpy(&event.x, payload + 1 + sizeof(int32_t), sizeof(float));
            std::memcpy(&event.y, payload + 1 + sizeof(int32_t) + sizeof(float), sizeof(float));
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::PLAYER_LEAVE: {
            if (Packet::payloadLen(len) != 1 + sizeof(int32_t)) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::PlayerLeft;
            std::memcpy(&event.playerId, payload + 1, sizeof(int32_t));
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::SERVER_TEXT: {
            if (Packet::payloadLen(len) < 2) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::ServerText;
            event.text.assign(reinterpret_cast<const char*>(payload + 1), Packet::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::PLAYER_NAME: {
            if (Packet::payloadLen(len) < 1 + sizeof(int32_t) + sizeof(uint8_t)) return;
            int32_t playerId = 0;
            std::memcpy(&playerId, payload + 1, sizeof(playerId));
            const uint8_t nameLen = *(payload + 1 + sizeof(playerId));
            if (Packet::payloadLen(len) != 1 + sizeof(playerId) + sizeof(uint8_t) + nameLen) return;

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
            event.text.assign(reinterpret_cast<const char*>(payload + 1), Packet::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        case GameStateType::MAP_TRANSFER: {
            if (Packet::payloadLen(len) < 1 + sizeof(float) * 2 + sizeof(uint8_t)) return;
            NetworkEvent event {};
            event.type = NetworkEvent::Type::MapTransfer;
            size_t offset = 1;
            std::memcpy(&event.x, payload + offset, sizeof(float));
            offset += sizeof(float);
            std::memcpy(&event.y, payload + offset, sizeof(float));
            offset += sizeof(float);
            const uint8_t mapLen = *(payload + offset);
            offset += sizeof(uint8_t);
            if (offset + mapLen > Packet::payloadLen(len)) return;
            event.mapId.assign(reinterpret_cast<const char*>(payload + offset), mapLen);
            PushEvent(std::move(event));
            break;
        }
        default:
            break;
        }
        break;
    }
    case PacketOpcode::PLUGIN_MESSAGE: {
        if (Packet::payloadLen(len) < 1) return;
        const PluginMessageType type = static_cast<PluginMessageType>(Packet::payload(data)[0]);
        if (type == PluginMessageType::UPDATE_MANIFEST) {
            NetworkEvent event {};
            event.type = NetworkEvent::Type::AssetManifest;
            event.text.assign(reinterpret_cast<const char*>(Packet::payload(data) + 1), Packet::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        if (type == PluginMessageType::ASSET_BLOB) {
            NetworkEvent event {};
            event.type = NetworkEvent::Type::AssetBlob;
            event.text.assign(reinterpret_cast<const char*>(Packet::payload(data) + 1), Packet::payloadLen(len) - 1);
            PushEvent(std::move(event));
            break;
        }
        break;
    }
    default:
        break;
    }
}
