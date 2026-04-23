#include "network/Network.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace {
constexpr const char* kDiscoveryProbe = "NGANU_DISCOVER_V1";
constexpr const char* kDiscoveryReply = "NGANU_SERVER_V1";
}

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
    gamePort_ = port;
    if (port == 65535) {
        discoveryPort_ = 0;
        logger_.warn("Network", "LAN discovery disabled because game port 65535 has no port+1");
        return true;
    }

    discoveryPort_ = static_cast<uint16_t>(port + 1);
    discoverySocket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (discoverySocket_ != ENET_SOCKET_NULL) {
        ENetAddress discoveryAddress {};
        discoveryAddress.host = ENET_HOST_ANY;
        discoveryAddress.port = discoveryPort_;
        enet_socket_set_option(discoverySocket_, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(discoverySocket_, ENET_SOCKOPT_REUSEADDR, 1);
        if (enet_socket_bind(discoverySocket_, &discoveryAddress) == 0) {
            logger_.info("Network", "LAN discovery listening on UDP port %u", (unsigned)discoveryPort_);
        } else {
            logger_.warn("Network", "Failed to bind LAN discovery port %u", (unsigned)discoveryPort_);
            enet_socket_destroy(discoverySocket_);
            discoverySocket_ = ENET_SOCKET_NULL;
        }
    } else {
        logger_.warn("Network", "Failed to create LAN discovery socket");
    }
    return true;
}

void Network::shutdown() {
    if (discoverySocket_ != ENET_SOCKET_NULL) {
        enet_socket_destroy(discoverySocket_);
        discoverySocket_ = ENET_SOCKET_NULL;
    }
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

void Network::pollDiscovery(const std::string& serverName) {
    if (discoverySocket_ == ENET_SOCKET_NULL) return;

    char buffer[128] {};
    ENetBuffer receiveBuffer {};
    receiveBuffer.data = buffer;
    receiveBuffer.dataLength = sizeof(buffer) - 1;

    ENetAddress sender {};
    while (true) {
        const int received = enet_socket_receive(discoverySocket_, &sender, &receiveBuffer, 1);
        if (received <= 0) {
            break;
        }
        buffer[std::min(received, static_cast<int>(sizeof(buffer) - 1))] = '\0';
        if (std::strncmp(buffer, kDiscoveryProbe, std::strlen(kDiscoveryProbe)) != 0) {
            continue;
        }

        char reply[192] {};
        std::snprintf(reply,
                      sizeof(reply),
                      "%s port=%u name=%s",
                      kDiscoveryReply,
                      static_cast<unsigned>(gamePort_),
                      serverName.empty() ? "nganu.mp" : serverName.c_str());
        ENetBuffer replyBuffer {};
        replyBuffer.data = reply;
        replyBuffer.dataLength = std::strlen(reply);
        enet_socket_send(discoverySocket_, &sender, &replyBuffer, 1);
    }
}

void Network::sendPacket(void* peer, const void* data, size_t len, int channel, bool reliable) {
    if (!peer || !data || len == 0) return;
    const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(data, len, flags);
    if (!packet) {
        logger_.warn("Network", "Failed to allocate packet (%zu bytes)", len);
        return;
    }
    if (enet_peer_send(static_cast<ENetPeer*>(peer), channel, packet) != 0) {
        logger_.warn("Network", "Failed to queue packet (%zu bytes)", len);
        enet_packet_destroy(packet);
    }
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
