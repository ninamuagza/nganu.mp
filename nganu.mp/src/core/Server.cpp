#include "core/Server.h"
#include "core/ContentRevision.h"
#include "core/MovementValidation.h"
#include "shared/ContentIntegrity.h"
#include "script/Builtins.h"
#include "network/Packet.h"

#include <chrono>
#include <thread>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <functional>
#include <filesystem>
#include <optional>
#include <system_error>

#ifndef _WIN32
  #include <sys/select.h>
  #include <unistd.h>
#else
  #include <io.h>
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Static members                                                     */
/* ------------------------------------------------------------------ */
Server* Server::s_instance = nullptr;

namespace {
constexpr uint64_t kManifestProbeMinIntervalMs = 500;
constexpr uint64_t kAssetRequestWindowMs = 1000;
constexpr uint32_t kMaxAssetRequestsPerWindow = 64;

uint64_t nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool isSafeAssetKey(const std::string& value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    if (value.find('\0') != std::string::npos || value.find('\\') != std::string::npos) {
        return false;
    }
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            continue;
        }
        if (ch == '_' || ch == '-' || ch == '.' || ch == ':' || ch == '/') {
            continue;
        }
        return false;
    }
    return true;
}

bool objectInteractableByClient(const MapData::Object& object) {
    auto interact = object.properties.find("interact");
    if (interact != object.properties.end()) {
        return !interact->second.empty() && interact->second != "false" && interact->second != "0";
    }
    return object.kind == "npc" || object.kind == "portal";
}

float objectInteractionRange(const MapData::Object& object) {
    auto range = object.properties.find("interact_radius");
    if (range != object.properties.end()) {
        try {
            return std::max(24.0f, std::stof(range->second));
        } catch (...) {
        }
    }
    return std::clamp((std::max(object.width, object.height) * 0.5f) + 40.0f, 56.0f, 120.0f);
}

bool playerCanInteractWithObject(const Server::PlayerPosition& playerPosition, const MapData::Object& object) {
    const float centerX = object.x + (object.width * 0.5f);
    const float centerY = object.y + (object.height * 0.5f);
    const float dx = playerPosition.x - centerX;
    const float dy = playerPosition.y - centerY;
    const float range = objectInteractionRange(object);
    return (dx * dx) + (dy * dy) <= (range * range);
}

}

static void signalHandler(int /*sig*/) {
    if (Server::s_instance) Server::s_instance->requestShutdown();
}

void PLUGIN_CALL Server::sendPacketTrampoline(void* peer, const void* data, size_t len, int channel) {
    if (s_instance) s_instance->network_.sendPacket(peer, data, len, channel);
}

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                           */
/* ------------------------------------------------------------------ */
Server::Server()
    : runtime_(logger_),
      plugins_(logger_),
      script_(logger_),
      network_(logger_)
{
    s_instance = this;
}

Server::~Server() {
    if (s_instance == this) s_instance = nullptr;
}

/* ------------------------------------------------------------------ */
/* Startup                                                            */
/* ------------------------------------------------------------------ */
bool Server::startup(const std::string& cfgPath) {
    /* 1. Parse server.cfg */
    if (!runtime_.loadConfig(cfgPath)) return false;

    /* 2. Initialise Logger level */
    logger_.setLevel(Logger::parseLevel(runtime_.getString("loglevel", "info")));
    logger_.info("Server", "Starting game server...");
    tickRate_ = std::clamp(runtime_.getInt("tickrate", 100), 1, 1000);
    maxPacketSize_ = static_cast<size_t>(std::clamp(runtime_.getInt("maxpacketsize", 1024), 64, 65535));
    maxChatLen_ = static_cast<size_t>(std::clamp(runtime_.getInt("chatmaxlen", 200), 16, 512));
    logger_.info("Server", "Config: tickrate=%d, maxpacketsize=%zu, chatmaxlen=%zu",
                 tickRate_, maxPacketSize_, maxChatLen_);

    worldMapPath_ = runtime_.getString("worldmap", "assets/maps/overworld.map");
    mapDirectory_ = std::filesystem::path(worldMapPath_).parent_path();
    if (!loadMaps()) {
        return false;
    }
    rebuildAllowedAssetKeys();
    contentRevision_ = computeContentRevision();
    logger_.info("Server", "Loaded map %s (%s)", map_.mapId().c_str(), map_.worldName().c_str());

    /* 3. Initialise ENet */
    uint16_t port = static_cast<uint16_t>(runtime_.getInt("port", 7777));
    size_t maxClients = static_cast<size_t>(runtime_.getInt("maxclients", 100));
    if (!network_.init(port, maxClients)) {
        logger_.error("Server", "Failed to initialise network");
        return false;
    }

    /* 4. Load plugins */
    plugins_.setScriptRegisterCallback([this](const char* name, SCRIPT_FUNCTION fn) {
        script_.registerFunction(name, fn);
    });
    plugins_.setSendPacketFn(&sendPacketTrampoline);

    std::string pluginList = runtime_.getString("plugins");
    if (!pluginList.empty()) {
        std::istringstream ss(pluginList);
        std::string name;
        while (ss >> name) {
            if (!plugins_.loadPlugin(name, "plugins")) {
                logger_.error("Server", "Failed to load required plugin: %s", name.c_str());
                return false;
            }
        }
    }

    /* 5. Load LuaJIT script */
    RegisterBuiltinFunctions(script_, logger_, network_, *this);

    std::string gamemode = runtime_.getString("gamemode", "");
    if (!gamemode.empty()) {
        std::string scriptPath = "scripts/" + gamemode;
        if (!script_.load(scriptPath)) {
            logger_.error("Server", "Failed to load Lua script: %s", scriptPath.c_str());
            return false;
        }
        script_.callFunction("OnGameModeInit");
    }

    /* Install signal handlers */
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    return true;
}

/* ------------------------------------------------------------------ */
/* Main loop                                                          */
/* ------------------------------------------------------------------ */
void Server::run() {
    running_ = true;
    logger_.info("Server", "Entering main loop (%d ticks/s)", tickRate_);

    using clock = std::chrono::steady_clock;
    const auto tickDuration = std::chrono::milliseconds(std::max(1, 1000 / tickRate_));

    while (running_) {
        auto tickStart = clock::now();

        /* 1. Poll ENet */
        processNetworkEvents();
        disconnectTimedOutPlayers();

        /* 2. Read stdin (non-blocking) */
        processStdin();

        /* 3. Tick script runtime */
        script_.callFunction("OnGameModeUpdate", static_cast<int>(tick_));
        ++tick_;

        /* 4. Plugin connect/disconnect callbacks are fired immediately in
              processNetworkEvents(), so nothing queued here. */

        /* Sleep remaining time */
        auto elapsed = clock::now() - tickStart;
        if (elapsed < tickDuration) {
            std::this_thread::sleep_for(tickDuration - elapsed);
        }
    }
}

void Server::requestShutdown() {
    running_ = false;
}

int Server::allocatePlayerId() {
    if (!freePlayerIds_.empty()) {
        const int playerid = *freePlayerIds_.begin();
        freePlayerIds_.erase(freePlayerIds_.begin());
        return playerid;
    }

    return nextPlayerId_++;
}

void Server::releasePlayerId(int playerid) {
    if (playerid <= 0) return;
    freePlayerIds_.insert(playerid);
}

void Server::setPlayerPosition(int playerid, float x, float y) {
    playerPositions_[playerid] = PlayerPosition {x, y};
}

Server::PlayerPosition Server::getPlayerPosition(int playerid) const {
    auto it = playerPositions_.find(playerid);
    if (it != playerPositions_.end()) {
        return it->second;
    }
    return PlayerPosition {};
}

const MapData* Server::mapForId(const std::string& mapId) const {
    auto it = maps_.find(mapId);
    if (it == maps_.end()) {
        return nullptr;
    }
    return &it->second;
}

const MapData* Server::mapForPlayer(int playerid) const {
    auto mapIt = playerMapIds_.find(playerid);
    if (mapIt == playerMapIds_.end()) {
        return &map_;
    }
    return mapForId(mapIt->second);
}

std::string Server::playerMapId(int playerid) const {
    auto it = playerMapIds_.find(playerid);
    if (it == playerMapIds_.end()) {
        return defaultMapId_;
    }
    return it->second;
}

bool Server::teleportPlayer(int playerid, float x, float y, const std::string& reason) {
    return teleportPlayerWithinMap(playerid, PlayerPosition {x, y}, reason);
}

bool Server::setPlayerName(int playerid, const std::string& name, bool broadcast) {
    if (!isValidPlayerName(name)) {
        return false;
    }

    playerNames_[playerid] = name;
    if (broadcast) {
        broadcastPlayerName(playerid);
    }
    return true;
}

std::string Server::playerName(int playerid) const {
    auto it = playerNames_.find(playerid);
    if (it != playerNames_.end()) {
        return it->second;
    }
    return "Player " + std::to_string(playerid);
}

bool Server::isPlayerConnected(int playerid) const {
    return playerid > 0 && network_.peerForPlayer(playerid) != nullptr;
}

size_t Server::playerCount() const {
    return network_.playerCount();
}

size_t Server::playerCountInMap(const std::string& mapId) const {
    size_t count = 0;
    for (int playerid : network_.playerIds()) {
        if (playerMapId(playerid) == mapId) {
            ++count;
        }
    }
    return count;
}

bool Server::isValidPlayerName(const std::string& name) const {
    if (name.empty() || name.size() > 24) {
        return false;
    }

    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != ' ' && ch != '_' && ch != '-') {
            return false;
        }
    }

    return true;
}

bool Server::loadMaps() {
    maps_.clear();
    mapPaths_.clear();
    defaultMapId_.clear();

    const std::filesystem::path configuredPath(worldMapPath_);
    if (!std::filesystem::exists(configuredPath)) {
        logger_.error("Server", "Failed to load world map: %s", worldMapPath_.c_str());
        return false;
    }

    if (!std::filesystem::exists(mapDirectory_)) {
        logger_.error("Server", "Map directory not found: %s", mapDirectory_.string().c_str());
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(mapDirectory_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".map") continue;

        MapData loaded;
        const std::string path = entry.path().string();
        if (!loaded.loadFromFile(path, logger_)) {
            logger_.error("Server", "Failed to load map file: %s", path.c_str());
            return false;
        }
        mapPaths_[loaded.mapId()] = path;
        maps_[loaded.mapId()] = loaded;
    }

    MapData defaultMap;
    if (!defaultMap.loadFromFile(worldMapPath_, logger_)) {
        logger_.error("Server", "Failed to load configured world map: %s", worldMapPath_.c_str());
        return false;
    }
    defaultMapId_ = defaultMap.mapId();
    map_ = defaultMap;
    maps_[defaultMapId_] = defaultMap;
    mapPaths_[defaultMapId_] = worldMapPath_;

    logger_.info("Server", "Loaded %zu map(s); default=%s", maps_.size(), defaultMapId_.c_str());
    return true;
}

void Server::sendMapTransfer(int playerid, const std::string& mapId, float x, float y) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer || mapId.empty() || mapId.size() > 63) return;

    const uint8_t mapLen = static_cast<uint8_t>(mapId.size());
    std::vector<uint8_t> pkt(1 + 1 + sizeof(float) * 2 + sizeof(uint8_t) + mapLen);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::MAP_TRANSFER);
    std::memcpy(pkt.data() + offset, &x, sizeof(x));
    offset += sizeof(x);
    std::memcpy(pkt.data() + offset, &y, sizeof(y));
    offset += sizeof(y);
    pkt[offset++] = mapLen;
    std::memcpy(pkt.data() + offset, mapId.data(), mapLen);
    offset += mapLen;
    network_.sendPacket(peer, pkt.data(), offset, 0);
}

void Server::sendAuthoritativePlayerPosition(int playerid) {
    void* peer = network_.peerForPlayer(playerid);
    auto it = playerPositions_.find(playerid);
    if (!peer || it == playerPositions_.end()) return;

    std::vector<uint8_t> pkt(1 + 1 + sizeof(float) * 2);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_POSITION);
    std::memcpy(pkt.data() + offset, &it->second.x, sizeof(it->second.x));
    offset += sizeof(it->second.x);
    std::memcpy(pkt.data() + offset, &it->second.y, sizeof(it->second.y));
    offset += sizeof(it->second.y);
    network_.sendPacket(peer, pkt.data(), offset, 0, false);
}

bool Server::transferPlayerToMap(int playerid, const std::string& mapId, std::optional<PlayerPosition> overridePosition) {
    const MapData* destinationMap = mapForId(mapId);
    if (!destinationMap) {
        logger_.warn("Server", "Transfer target map not found: %s", mapId.c_str());
        return false;
    }

    const std::string oldMapId = playerMapId(playerid);
    if (oldMapId == mapId && !overridePosition.has_value()) {
        return false;
    }

    script_.callFunction("OnPlayerLeaveMap", playerid);
    broadcastPlayerLeave(playerid, oldMapId);
    playerActiveTriggers_.erase(playerid);

    const PlayerPosition nextPosition = overridePosition.has_value()
        ? *overridePosition
        : defaultSpawnPosition(playerid, mapId);

    playerMapIds_[playerid] = mapId;
    playerPositions_[playerid] = nextPosition;
    playerLastMoveAtMs_[playerid] = nowMs();

    sendMapTransfer(playerid, mapId, nextPosition.x, nextPosition.y);
    sendAuthoritativePlayerPosition(playerid);
    sendSnapshotToPeer(network_.peerForPlayer(playerid), mapId);
    for (const auto& [otherId, name] : playerNames_) {
        if (otherId == playerid || name.empty()) continue;
        if (playerMapId(otherId) != mapId) continue;
        sendPlayerNameToPeer(network_.peerForPlayer(playerid), otherId);
    }
    script_.callFunction("OnPlayerEnterMap", playerid);
    broadcastPlayerJoin(playerid, playerid);
    logger_.info("Server", "Transferred player %d from %s to %s", playerid, oldMapId.c_str(), mapId.c_str());
    return true;
}

/* ------------------------------------------------------------------ */
/* Shutdown                                                           */
/* ------------------------------------------------------------------ */
void Server::shutdown() {
    logger_.info("Server", "Shutting down...");

    /* 1. Stop main loop (already stopped if we get here normally) */
    running_ = false;

    /* 2. Call OnGameModeExit while the network and plugins are still alive. */
    script_.callFunction("OnGameModeExit");

    /* 3. Free Lua runtime */
    script_.unload();

    /* 4. Unload plugins (reverse order) */
    plugins_.unloadAll();

    /* 5. Disconnect ENet peers */
    network_.shutdown();
    playerPositions_.clear();
    playerNames_.clear();
    playerMapIds_.clear();
    playerLastMoveAtMs_.clear();
    playerLastSeenAtMs_.clear();
    playerLastManifestProbeAtMs_.clear();
    playerAssetReqWindowStartAtMs_.clear();
    playerAssetReqCountInWindow_.clear();
    playerActiveTriggers_.clear();
    freePlayerIds_.clear();
    nextPlayerId_ = 1;

    logger_.info("Server", "Shutdown complete");
}

/* ------------------------------------------------------------------ */
/* --test mode                                                        */
/* ------------------------------------------------------------------ */
int Server::runTest(const std::string& cfgPath) {
    if (!startup(cfgPath)) return 1;
    /* Skip the main loop — go straight to shutdown */
    shutdown();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Network event processing                                           */
/* ------------------------------------------------------------------ */
void Server::processNetworkEvents() {
    network_.pollDiscovery(runtime_.getString("servername", "nganu.mp"));

    ENetEvent event;
    while (network_.pollEvent(event)) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            int pid = allocatePlayerId();
            event.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(pid));
            network_.assignPlayer(event.peer, pid);
            playerMapIds_[pid] = defaultMapId_;
            playerPositions_[pid] = defaultSpawnPosition(pid, defaultMapId_);
            const uint64_t connectedAtMs = nowMs();
            playerLastMoveAtMs_[pid] = connectedAtMs;
            playerLastSeenAtMs_[pid] = connectedAtMs;
            playerLastManifestProbeAtMs_[pid] = 0;
            playerAssetReqWindowStartAtMs_[pid] = connectedAtMs;
            playerAssetReqCountInWindow_[pid] = 0;
            playerNames_[pid] = "Player " + std::to_string(pid);
            logger_.info("Server", "Player %d connected", pid);

            inventory_.createInventory(pid);
            script_.callFunction("OnPlayerConnect", pid);
            script_.callFunction("OnPlayerEnterMap", pid);
            plugins_.firePlayerConnect(pid);

            sendHandshakeToPeer(event.peer, pid);
            sendAuthoritativePlayerPosition(pid);
            sendSnapshotToPeer(event.peer, defaultMapId_);
            broadcastPlayerJoin(pid, pid);
            for (const auto& [otherId, name] : playerNames_) {
                if (otherId == pid || name.empty()) continue;
                if (playerMapId(otherId) != defaultMapId_) continue;
                sendPlayerNameToPeer(event.peer, otherId);
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            int pid = static_cast<int>(reinterpret_cast<uintptr_t>(event.peer->data));
            if (pid > 0) {
                cleanupPlayerSession(pid, 0, false);
            } else {
                event.peer->data = nullptr;
            }
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            int pid = static_cast<int>(reinterpret_cast<uintptr_t>(event.peer->data));
            if (pid > 0) {
                playerLastSeenAtMs_[pid] = nowMs();
            }
            handlePacket(pid, event.packet->data, event.packet->dataLength);
            enet_packet_destroy(event.packet);
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Packet dispatch                                                    */
/* ------------------------------------------------------------------ */
void Server::handlePacket(int playerid, const void* data, size_t len) {
    if (len < 1) return;
    if (len > maxPacketSize_) {
        logger_.warn("Server", "Dropping oversized packet (%zu) from player %d", len, playerid);
        network_.disconnectPlayer(playerid, 2);
        return;
    }

    PacketOpcode op = Packet::readOpcode(data, len);

    switch (op) {
    case PacketOpcode::HANDSHAKE:
        /* Client shouldn't send this — ignore. */
        break;
    case PacketOpcode::PLAYER_DATA:
        if (Packet::payloadLen(len) != sizeof(float) * 2) {
            logger_.warn("Server", "Malformed PLAYER_DATA length %zu from player %d", len, playerid);
            network_.disconnectPlayer(playerid, 3);
            break;
        }
        {
            const MapData* activeMap = mapForPlayer(playerid);
            auto prevIt = playerPositions_.find(playerid);
            if (!activeMap || prevIt == playerPositions_.end()) {
                break;
            }

            PlayerPosition pos {};
            std::memcpy(&pos.x, Packet::payload(data), sizeof(float));
            std::memcpy(&pos.y, Packet::payload(data) + sizeof(float), sizeof(float));

            const uint64_t currentMs = nowMs();
            const uint64_t lastMs = playerLastMoveAtMs_.count(playerid) ? playerLastMoveAtMs_[playerid] : currentMs;
            const MovementValidationResult validation = ValidateMovement(prevIt->second, pos, currentMs, lastMs, *activeMap);
            if (!validation.accepted) {
                logger_.warn("Server",
                              "Rejected movement from player %d on %s (dist=%.2f dt=%.3f raw_dt=%.3f walkable=%d)",
                              playerid,
                              playerMapId(playerid).c_str(),
                              validation.distance,
                              validation.dtSeconds,
                              validation.rawDtSeconds,
                              validation.walkable ? 1 : 0);
                sendAuthoritativePlayerPosition(playerid);
                break;
            }

            playerPositions_[playerid] = pos;
            playerLastMoveAtMs_[playerid] = currentMs;

            std::vector<uint8_t> pkt(1 + sizeof(int32_t) + sizeof(float) * 2);
            pkt[0] = static_cast<uint8_t>(PacketOpcode::PLAYER_MOVE);
            const int32_t sender = static_cast<int32_t>(playerid);
            std::memcpy(pkt.data() + 1, &sender, sizeof(sender));
            std::memcpy(pkt.data() + 1 + sizeof(sender), &pos.x, sizeof(pos.x));
            std::memcpy(pkt.data() + 1 + sizeof(sender) + sizeof(pos.x), &pos.y, sizeof(pos.y));
            script_.callFunction("OnPlayerMove", playerid);
            updatePlayerTriggers(playerid);
            const std::string mapId = playerMapId(playerid);
            for (int otherId : network_.playerIds()) {
                if (otherId == playerid) continue;
                if (playerMapId(otherId) != mapId) continue;
                network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), pkt.size(), 0, false);
            }
        }
        break;
    case PacketOpcode::OBJECT_INTERACT:
        if (Packet::payloadLen(len) < 1 || Packet::payloadLen(len) > 128) {
            logger_.warn("Server", "Malformed OBJECT_INTERACT length %zu from player %d", len, playerid);
            break;
        }
        {
            std::string objectId(reinterpret_cast<const char*>(Packet::payload(data)), Packet::payloadLen(len));
            const MapData* activeMap = mapForPlayer(playerid);
            if (!activeMap) {
                break;
            }
            const int objectIndex = activeMap->objectIndexById(objectId);
            if (objectIndex >= 0) {
                const auto* object = activeMap->objectByIndex(objectIndex);
                auto positionIt = playerPositions_.find(playerid);
                if (!object || positionIt == playerPositions_.end()) {
                    break;
                }
                if (!objectInteractableByClient(*object)) {
                    logger_.warn("Server", "Rejected non-interactable object %s from player %d", object->id.c_str(), playerid);
                    break;
                }
                if (!playerCanInteractWithObject(positionIt->second, *object)) {
                    logger_.warn("Server", "Rejected out-of-range object %s from player %d", object->id.c_str(), playerid);
                    break;
                }
                const bool hasScript = object
                    && object->properties.find("script") != object->properties.end()
                    && !object->properties.at("script").empty();
                if (hasScript) {
                    script_.callFunction("OnMapObjectInteract", playerid, objectIndex);
                } else if (object && object->kind == "portal") {
                    const std::string currentMapId = playerMapId(playerid);
                    auto targetMap = object->properties.find("target_map");
                    std::string destinationMapId = currentMapId;
                    if (targetMap != object->properties.end() && !targetMap->second.empty()) {
                        destinationMapId = targetMap->second;
                    }

                    PlayerPosition nextPosition = defaultSpawnPosition(playerid, destinationMapId);
                    auto targetX = object->properties.find("target_x");
                    auto targetY = object->properties.find("target_y");
                    try {
                        if (targetX != object->properties.end() && targetY != object->properties.end()) {
                            nextPosition.x = std::stof(targetX->second);
                            nextPosition.y = std::stof(targetY->second);
                        }
                    } catch (...) {
                        logger_.warn("Server", "Invalid portal target on object %s", object->id.c_str());
                        break;
                    }

                    const std::string targetTitle = object->properties.count("target_title")
                        ? object->properties.at("target_title")
                        : object->properties.count("title") ? object->properties.at("title") : std::string("portal");
                    if (destinationMapId == currentMapId) {
                        teleportPlayerWithinMap(playerid,
                                                nextPosition,
                                                "Travelled to " + targetTitle + ".");
                    } else {
                        transferPlayerToMap(playerid, destinationMapId, nextPosition);
                    }
                } else {
                    script_.callFunction("OnMapObjectInteract", playerid, objectIndex);
                }
            }
        }
        break;
    case PacketOpcode::CHAT_MESSAGE:
        if (Packet::payloadLen(len) < 1 || Packet::payloadLen(len) > maxChatLen_) {
            logger_.warn("Server", "Malformed CHAT_MESSAGE length %zu from player %d", len, playerid);
            network_.disconnectPlayer(playerid, 4);
            break;
        }
        {
            const uint8_t* msg = Packet::payload(data);
            const size_t msgLen = Packet::payloadLen(len);
            bool hasNul = false;
            for (size_t i = 0; i < msgLen; ++i) {
                if (msg[i] == '\0') {
                    hasNul = true;
                    break;
                }
            }
            if (hasNul) {
                logger_.warn("Server", "Malformed CHAT_MESSAGE with NUL byte from player %d", playerid);
                network_.disconnectPlayer(playerid, 5);
                break;
            }

            std::vector<uint8_t> pkt(1 + sizeof(int32_t) + msgLen);
            pkt[0] = static_cast<uint8_t>(PacketOpcode::CHAT_MESSAGE);
            const int32_t sender = static_cast<int32_t>(playerid);
            std::memcpy(pkt.data() + 1, &sender, sizeof(sender));
            std::memcpy(pkt.data() + 1 + sizeof(sender), msg, msgLen);
            lastPlayerText_.assign(reinterpret_cast<const char*>(msg), msgLen);
            if (!lastPlayerText_.empty() && lastPlayerText_[0] == '/') {
                script_.callFunction("OnPlayerCommand", playerid);
            } else {
                script_.callFunction("OnPlayerText", playerid);
                const std::string mapId = playerMapId(playerid);
                for (int otherId : network_.playerIds()) {
                    if (playerMapId(otherId) != mapId) continue;
                    network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), pkt.size(), 0);
                }
            }
        }
        break;
    case PacketOpcode::GAME_STATE:
        /* Client shouldn't send this — ignore. */
        break;
    case PacketOpcode::INVENTORY:
        if (Packet::payloadLen(len) >= 1) {
            handleInventoryPacket(playerid, Packet::payload(data), Packet::payloadLen(len));
        }
        break;
    case PacketOpcode::PLUGIN_MESSAGE:
        if (Packet::payloadLen(len) < 1) {
            logger_.warn("Server", "Malformed PLUGIN_MESSAGE length %zu from player %d", len, playerid);
            break;
        }
        {
        const PluginMessageType msgType = static_cast<PluginMessageType>(Packet::payload(data)[0]);
        if (msgType == PluginMessageType::PLAYER_NAME) {
            const size_t nameLen = Packet::payloadLen(len) - 1;
            if (nameLen < 1 || nameLen > 24) {
                logger_.warn("Server", "Malformed name update length %zu from player %d", nameLen, playerid);
                break;
            }

            std::string name(reinterpret_cast<const char*>(Packet::payload(data) + 1), nameLen);
            if (!setPlayerName(playerid, name, true)) {
                logger_.warn("Server", "Rejected invalid player name from player %d", playerid);
                break;
            }
            script_.callFunction("OnPlayerNameChange", playerid);
            break;
        }
        if (msgType == PluginMessageType::HEARTBEAT) {
            if (Packet::payloadLen(len) != 1) {
                logger_.warn("Server", "Malformed HEARTBEAT length %zu from player %d", len, playerid);
            }
            break;
        }
        if (msgType == PluginMessageType::UPDATE_PROBE) {
            if (Packet::payloadLen(len) != 1 && Packet::payloadLen(len) != 2) {
                logger_.warn("Server", "Malformed UPDATE_PROBE length %zu from player %d", len, playerid);
                break;
            }
            const uint64_t currentMs = nowMs();
            const uint64_t lastProbeMs = playerLastManifestProbeAtMs_.count(playerid) ? playerLastManifestProbeAtMs_[playerid] : 0;
            if (lastProbeMs != 0 && currentMs < lastProbeMs) {
                logger_.warn("Server", "Clock anomaly for player %d while rate-limiting UPDATE_PROBE", playerid);
            } else if (lastProbeMs != 0 && (currentMs - lastProbeMs) < kManifestProbeMinIntervalMs) {
                logger_.warn("Server", "Rate-limited UPDATE_PROBE from player %d", playerid);
                break;
            }
            playerLastManifestProbeAtMs_[playerid] = currentMs;
            void* peer = network_.peerForPlayer(playerid);
            sendHandshakeToPeer(peer, playerid);
            sendUpdateManifestToPeer(peer);
            break;
        }
        if (msgType == PluginMessageType::ASSET_REQUEST) {
            const size_t keyLen = Packet::payloadLen(len) - 1;
            if (keyLen < 1 || keyLen > 128) {
                logger_.warn("Server", "Malformed asset request length %zu from player %d", keyLen, playerid);
                break;
            }
            const uint64_t currentMs = nowMs();
            uint64_t& windowStartMs = playerAssetReqWindowStartAtMs_[playerid];
            uint32_t& windowCount = playerAssetReqCountInWindow_[playerid];
            if (windowStartMs != 0 && currentMs < windowStartMs) {
                logger_.warn("Server", "Clock anomaly for player %d while rate-limiting ASSET_REQUEST", playerid);
                windowStartMs = currentMs;
                windowCount = 0;
            } else if (windowStartMs == 0 || (currentMs - windowStartMs) >= kAssetRequestWindowMs) {
                windowStartMs = currentMs;
                windowCount = 0;
            }
            if (windowCount >= kMaxAssetRequestsPerWindow) {
                logger_.warn("Server", "Rate-limited ASSET_REQUEST from player %d", playerid);
                break;
            }
            ++windowCount;

            std::string assetKey(reinterpret_cast<const char*>(Packet::payload(data) + 1), keyLen);
            if (!isSafeAssetKey(assetKey)) {
                logger_.warn("Server", "Rejected malformed asset key from player %d", playerid);
                break;
            }
            sendAssetBlobToPeer(network_.peerForPlayer(playerid), assetKey);
            break;
        }
        plugins_.fireReceive(playerid, Packet::payload(data), Packet::payloadLen(len));
        }
        break;
    default:
        logger_.warn("Server", "Unknown opcode 0x%02X from player %d", static_cast<int>(op), playerid);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Stdin processing (non-blocking)                                    */
/* ------------------------------------------------------------------ */
void Server::processStdin() {
#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD avail = 0;
    if (PeekNamedPipe(hStdin, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        std::string line;
        std::getline(std::cin, line);
        handleCommand(line);
    }
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
        std::string line;
        if (std::getline(std::cin, line)) {
            handleCommand(line);
        }
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Command handling                                                   */
/* ------------------------------------------------------------------ */
void Server::handleCommand(const std::string& cmd) {
    if (cmd == "quit") {
        requestShutdown();
    } else if (cmd == "list players") {
        auto ids = network_.playerIds();
        logger_.info("Server", "Connected players: %zu", ids.size());
        for (int pid : ids) {
            logger_.info("Server", "  player %d", pid);
        }
    } else if (cmd.rfind("kick ", 0) == 0) {
        std::string idStr = cmd.substr(5);
        try {
            int pid = std::stoi(idStr);
            if (network_.disconnectPlayer(pid, 10)) {
                logger_.info("Server", "Kick requested for player %d", pid);
            } else {
                logger_.warn("Server", "Player %d not found", pid);
            }
        } catch (...) {
            logger_.warn("Server", "Invalid kick command, expected: kick <playerid>");
        }
    } else if (cmd == "list plugins") {
        for (const auto& p : plugins_.plugins()) {
            logger_.info("Server", "  %s v%d", p.fn_name(), p.fn_version());
        }
    } else if (cmd.rfind("exec ", 0) == 0) {
        std::string funcName = cmd.substr(5);
        if (!script_.callFunction(funcName.c_str())) {
            logger_.warn("Server", "Lua function '%s' not found or failed", funcName.c_str());
        }
    } else if (!cmd.empty()) {
        logger_.warn("Server", "Unknown command: %s", cmd.c_str());
    }
}

/* ------------------------------------------------------------------ */
/* Inventory helpers                                                  */
/* ------------------------------------------------------------------ */
void Server::handleInventoryPacket(int playerid, const uint8_t* payload, size_t len) {
    if (len < 1) return;
    const auto msgType = static_cast<InventoryMsgType>(payload[0]);

    switch (msgType) {
    case InventoryMsgType::CMSG_OPEN:
        inventory_.setOpen(playerid, true);
        sendInventoryFullState(playerid);
        break;

    case InventoryMsgType::CMSG_CLOSE:
        inventory_.setOpen(playerid, false);
        break;

    case InventoryMsgType::CMSG_USE_ITEM: {
        if (len < 2) { sendInventoryError(playerid, InvActionResult::INVALID_SLOT); break; }
        const int slot = static_cast<int>(payload[1]);
        const auto result = inventory_.useItem(playerid, slot);
        if (result != InvActionResult::OK) {
            sendInventoryError(playerid, result);
        } else {
            const auto* inv = inventory_.getInventory(playerid);
            if (inv) sendInventorySlotUpdate(playerid, inv->slots[slot]);
            script_.callFunction("OnPlayerUseItem", playerid, slot);
        }
        break;
    }

    case InventoryMsgType::CMSG_MOVE_ITEM: {
        if (len < 3) { sendInventoryError(playerid, InvActionResult::INVALID_SLOT); break; }
        const int slotFrom = static_cast<int>(payload[1]);
        const int slotTo   = static_cast<int>(payload[2]);
        const auto result = inventory_.moveItem(playerid, slotFrom, slotTo);
        if (result != InvActionResult::OK) {
            sendInventoryError(playerid, result);
        } else {
            const auto* inv = inventory_.getInventory(playerid);
            if (inv) {
                sendInventorySlotUpdate(playerid, inv->slots[slotFrom]);
                sendInventorySlotUpdate(playerid, inv->slots[slotTo]);
            }
        }
        break;
    }

    case InventoryMsgType::CMSG_DROP_ITEM: {
        if (len < 2) { sendInventoryError(playerid, InvActionResult::INVALID_SLOT); break; }
        const int slot = static_cast<int>(payload[1]);
        const auto result = inventory_.dropItem(playerid, slot);
        if (result != InvActionResult::OK) {
            sendInventoryError(playerid, result);
        } else {
            const auto* inv = inventory_.getInventory(playerid);
            if (inv) sendInventorySlotUpdate(playerid, inv->slots[slot]);
        }
        break;
    }

    default:
        logger_.warn("Server", "Unknown inventory msg 0x%02X from player %d",
                     static_cast<int>(msgType), playerid);
        break;
    }
}

void Server::sendInventoryFullState(int playerid) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer) return;
    const auto* inv = inventory_.getInventory(playerid);
    if (!inv) return;

    /* Format: [INVENTORY opcode][SMSG_OPEN][container_id u8][slot_count u8]
     *         per slot: [slot_index u8][occupied u8][item_def_id u16 LE][amount u16 LE][flags u8]
     */
    const int slotCount = static_cast<int>(inv->slots.size());
    std::vector<uint8_t> pkt;
    pkt.reserve(4 + slotCount * 8);
    pkt.push_back(static_cast<uint8_t>(PacketOpcode::INVENTORY));
    pkt.push_back(static_cast<uint8_t>(InventoryMsgType::SMSG_FULL_STATE));
    pkt.push_back(static_cast<uint8_t>(inv->container_id & 0xFF));
    pkt.push_back(static_cast<uint8_t>(slotCount));
    for (const auto& slot : inv->slots) {
        pkt.push_back(static_cast<uint8_t>(slot.slot_index));
        pkt.push_back(slot.occupied ? 1u : 0u);
        const uint16_t defId = static_cast<uint16_t>(slot.item_def_id);
        pkt.push_back(static_cast<uint8_t>(defId & 0xFF));
        pkt.push_back(static_cast<uint8_t>((defId >> 8) & 0xFF));
        const uint16_t amount = static_cast<uint16_t>(slot.amount);
        pkt.push_back(static_cast<uint8_t>(amount & 0xFF));
        pkt.push_back(static_cast<uint8_t>((amount >> 8) & 0xFF));
        pkt.push_back(slot.flags);
    }
    network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
}

void Server::sendInventorySlotUpdate(int playerid, const SlotState& slot) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer) return;
    /* Format: [INVENTORY][SMSG_SLOT_UPDATE][slot_index][occupied][def_id_lo][def_id_hi][amt_lo][amt_hi][flags] */
    uint8_t pkt[9];
    pkt[0] = static_cast<uint8_t>(PacketOpcode::INVENTORY);
    pkt[1] = static_cast<uint8_t>(InventoryMsgType::SMSG_SLOT_UPDATE);
    pkt[2] = static_cast<uint8_t>(slot.slot_index);
    pkt[3] = slot.occupied ? 1u : 0u;
    const uint16_t defId  = static_cast<uint16_t>(slot.item_def_id);
    const uint16_t amount = static_cast<uint16_t>(slot.amount);
    pkt[4] = static_cast<uint8_t>(defId & 0xFF);
    pkt[5] = static_cast<uint8_t>((defId >> 8) & 0xFF);
    pkt[6] = static_cast<uint8_t>(amount & 0xFF);
    pkt[7] = static_cast<uint8_t>((amount >> 8) & 0xFF);
    pkt[8] = slot.flags;
    network_.sendPacket(peer, pkt, sizeof(pkt), 0);
}

void Server::sendInventoryError(int playerid, InvActionResult err) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer) return;
    uint8_t pkt[3];
    pkt[0] = static_cast<uint8_t>(PacketOpcode::INVENTORY);
    pkt[1] = static_cast<uint8_t>(InventoryMsgType::SMSG_ERROR);
    pkt[2] = static_cast<uint8_t>(err);
    network_.sendPacket(peer, pkt, sizeof(pkt), 0);
}
