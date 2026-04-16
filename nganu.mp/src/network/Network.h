#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <enet/enet.h>
#include "core/Logger.h"

class Network {
public:
    explicit Network(Logger& logger);
    ~Network();

    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    /* Initialise ENet and create the host.  Returns false on error. */
    bool init(uint16_t port, size_t maxClients);

    /* Shut down: disconnect all peers, destroy host, deinitialise ENet. */
    void shutdown();

    /* Poll for events.  Returns true if an event was dequeued. */
    bool pollEvent(ENetEvent& event);

    /* Send a raw packet to a peer. */
    void sendPacket(void* peer, const void* data, size_t len, int channel, bool reliable = true);

    /* Assign a player id to a peer. */
    void assignPlayer(ENetPeer* peer, int playerid);

    /* Remove a player mapping. */
    void removePlayer(int playerid);

    /* Get the ENet peer for a player id (or nullptr). */
    void* peerForPlayer(int playerid) const;

    /* Broadcast packet to all players (optionally except one player id). */
    void broadcastPacket(const void* data, size_t len, int channel, int exceptPlayerid = -1, bool reliable = true);

    /* Disconnect a player by id. Returns true if player was found. */
    bool disconnectPlayer(int playerid, uint32_t reason = 0);

    /* Snapshot list of currently connected player IDs. */
    std::vector<int> playerIds() const;

    size_t playerCount() const { return players_.size(); }

    bool initialized() const { return host_ != nullptr; }

private:
    Logger&    logger_;
    ENetHost*  host_ = nullptr;
    std::unordered_map<int, ENetPeer*> players_;
};
