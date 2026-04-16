#include "network/Network.h"
#include <cstring>

Network::Network(Logger& logger) : logger_(logger) {}

Network::~Network() {
    shutdown();
}

bool Network::init(uint16_t port, size_t maxClients) {
    if (enet_initialize() != 0) {
        logger_.error("Network", "Failed to initialise ENet");
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    host_ = enet_host_create(&address,
                             maxClients,
                             2,      /* 2 channels */
                             0,      /* no incoming bandwidth limit */
                             0);     /* no outgoing bandwidth limit */
    if (!host_) {
        logger_.error("Network", "Failed to create ENet host on port %u", (unsigned)port);
        enet_deinitialize();
        return false;
    }

    logger_.info("Network", "Listening on port %u (max %zu clients)", (unsigned)port, maxClients);
    return true;
}

void Network::shutdown() {
    if (!host_) return;

    /* Gracefully disconnect all peers */
    for (size_t i = 0; i < host_->peerCount; ++i) {
        ENetPeer* peer = &host_->peers[i];
        if (peer->state == ENET_PEER_STATE_CONNECTED) {
            enet_peer_disconnect(peer, 0);
        }
    }

    /* Flush pending disconnects */
    ENetEvent event;
    while (enet_host_service(host_, &event, 100) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
    }

    enet_host_destroy(host_);
    host_ = nullptr;
    players_.clear();
    enet_deinitialize();
    logger_.info("Network", "Shut down");
}

bool Network::pollEvent(ENetEvent& event) {
    if (!host_) return false;
    return enet_host_service(host_, &event, 0) > 0;
}

void Network::sendPacket(void* peer, const void* data, size_t len, int channel, bool reliable) {
    if (!peer || !data || len == 0) return;
    const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(data, len, flags);
    enet_peer_send(static_cast<ENetPeer*>(peer), channel, packet);
}

void Network::assignPlayer(ENetPeer* peer, int playerid) {
    players_[playerid] = peer;
}

void Network::removePlayer(int playerid) {
    players_.erase(playerid);
}

void* Network::peerForPlayer(int playerid) const {
    auto it = players_.find(playerid);
    if (it != players_.end()) return it->second;
    return nullptr;
}

void Network::broadcastPacket(const void* data, size_t len, int channel, int exceptPlayerid, bool reliable) {
    if (!data || len == 0) return;
    for (const auto& [pid, peer] : players_) {
        if (!peer) continue;
        if (pid == exceptPlayerid) continue;
        sendPacket(peer, data, len, channel, reliable);
    }
}

bool Network::disconnectPlayer(int playerid, uint32_t reason) {
    auto it = players_.find(playerid);
    if (it == players_.end() || !it->second) return false;
    enet_peer_disconnect(it->second, reason);
    return true;
}

std::vector<int> Network::playerIds() const {
    std::vector<int> ids;
    ids.reserve(players_.size());
    for (const auto& [pid, _] : players_) {
        ids.push_back(pid);
    }
    return ids;
}
