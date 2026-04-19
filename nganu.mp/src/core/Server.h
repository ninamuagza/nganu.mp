#pragma once
// NOTE: This server is intentionally single-threaded. All subsystems run on the main thread.

#include <string>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include "core/MapData.h"
#include "core/Logger.h"
#include "core/Runtime.h"
#include "plugin/PluginManager.h"
#include "script/LuaRuntime.h"
#include "network/Network.h"
#include "inventory/InventoryService.h"

class Server {
public:
    struct PlayerPosition {
        float x = 0.0f;
        float y = 0.0f;
    };

    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /* Full startup sequence.  Returns false on fatal error. */
    bool startup(const std::string& cfgPath);

    /* Enter the main loop (blocks until shutdown). */
    void run();

    /* Request a graceful shutdown. */
    void requestShutdown();

    /* Full shutdown sequence. */
    void shutdown();

    /* For --test mode: startup + immediate shutdown, returns exit code. */
    int runTest(const std::string& cfgPath);

    Logger&        logger()  { return logger_; }
    Runtime&       runtime() { return runtime_; }
    PluginManager& plugins() { return plugins_; }
    LuaRuntime&    script()  { return script_; }
    Network&       network() { return network_; }
    void setPlayerPosition(int playerid, float x, float y);
    PlayerPosition getPlayerPosition(int playerid) const;
    bool setPlayerName(int playerid, const std::string& name, bool broadcast = true);
    std::string playerName(int playerid) const;
    bool isPlayerConnected(int playerid) const;
    size_t playerCount() const;
    size_t playerCountInMap(const std::string& mapId) const;
    bool isValidPlayerName(const std::string& name) const;
    const MapData& map() const { return map_; }
    const MapData* mapForId(const std::string& mapId) const;
    const MapData* mapForPlayer(int playerid) const;
    std::string playerMapId(int playerid) const;
    InventoryService& inventory() { return inventory_; }
    const InventoryService& inventory() const { return inventory_; }
    bool teleportPlayer(int playerid, float x, float y, const std::string& reason = "");
    bool transferPlayerToMap(int playerid, const std::string& mapId, std::optional<PlayerPosition> overridePosition = std::nullopt);
    void sendObjectiveText(int playerid, const std::string& text);
    void setLastPlayerText(std::string text) { lastPlayerText_ = std::move(text); }
    const std::string& lastPlayerText() const { return lastPlayerText_; }

private:
    Logger           logger_;
    Runtime          runtime_;
    PluginManager    plugins_;
    LuaRuntime       script_;
    Network          network_;
    InventoryService inventory_;

    std::atomic<bool> running_{false};
    int               nextPlayerId_ = 1;
    std::set<int>     freePlayerIds_;
    uint32_t          tick_ = 0;
    int               tickRate_ = 100;
    size_t            maxPacketSize_ = 1024;
    size_t            maxChatLen_ = 200;
    std::string       worldMapPath_;
    std::filesystem::path mapDirectory_;
    std::string       contentRevision_;
    MapData           map_;
    std::unordered_map<std::string, MapData> maps_;
    std::unordered_map<std::string, std::string> mapPaths_;
    std::string       defaultMapId_;
    std::unordered_map<int, PlayerPosition> playerPositions_;
    std::unordered_map<int, std::string> playerNames_;
    std::unordered_map<int, std::string> playerMapIds_;
    std::unordered_map<int, uint64_t> playerLastMoveAtMs_;
    std::unordered_map<int, uint64_t> playerLastSeenAtMs_;
    std::unordered_map<int, uint64_t> playerLastManifestProbeAtMs_;
    std::unordered_map<int, uint64_t> playerAssetReqWindowStartAtMs_;
    std::unordered_map<int, uint32_t> playerAssetReqCountInWindow_;
    std::unordered_map<int, std::unordered_set<int>> playerActiveTriggers_;
    std::unordered_set<std::string> allowedAssetKeys_;
    std::string       lastPlayerText_;

    void processNetworkEvents();
    void processStdin();
    void handleCommand(const std::string& cmd);
    void handlePacket(int playerid, const void* data, size_t len);
    int allocatePlayerId();
    void releasePlayerId(int playerid);
    bool loadMaps();
    void rebuildAllowedAssetKeys();
    std::string computeContentRevision() const;
    PlayerPosition defaultSpawnPosition(int playerid, const std::string& mapId) const;
    bool teleportPlayerWithinMap(int playerid, PlayerPosition nextPosition, const std::string& reason);
    void updatePlayerTriggers(int playerid);
    void disconnectTimedOutPlayers();
    void cleanupPlayerSession(int playerid, int reason, bool notifyNetworkPeer);
    void sendSnapshotToPeer(void* peer, const std::string& mapId);
    void sendPlayerJoinToPeer(void* peer, int playerid);
    void broadcastPlayerJoin(int playerid, int exceptPlayerid = -1);
    void broadcastPlayerLeave(int playerid, const std::string& mapId);
    void sendServerText(void* peer, const std::string& text);
    void sendServerText(int playerid, const std::string& text);
    void sendUpdateManifestToPeer(void* peer);
    void sendAssetBlobToPeer(void* peer, const std::string& assetKey);
    void sendMapTransfer(int playerid, const std::string& mapId, float x, float y);
    void sendAuthoritativePlayerPosition(int playerid);
    void broadcastPlayerName(int playerid);
    void sendPlayerNameToPeer(void* peer, int playerid);

    /* Inventory helpers */
    void handleInventoryPacket(int playerid, const uint8_t* payload, size_t len);
    void sendInventoryFullState(int playerid);
    void sendInventorySlotUpdate(int playerid, const SlotState& slot);
    void sendInventoryError(int playerid, InvActionResult err);

    /* Send-packet trampoline for PluginAPI */
    static void PLUGIN_CALL sendPacketTrampoline(void* peer, const void* data, size_t len, int channel);

public:
    /* Used by the signal handler to request shutdown. */
    static Server* s_instance;
};
