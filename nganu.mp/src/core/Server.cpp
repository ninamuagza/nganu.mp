#include "core/Server.h"
#include "core/ContentRevision.h"
#include "core/MovementValidation.h"
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
std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

uint64_t nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string hexEncode(const std::string& bytes) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char ch : bytes) {
        out.push_back(kHex[(ch >> 4) & 0x0F]);
        out.push_back(kHex[ch & 0x0F]);
    }
    return out;
}

std::string readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool isSafeRelativeAssetPath(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos || value.find('\\') != std::string::npos) {
        return false;
    }

    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

bool pathIsInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(candidate, root, ec);
    if (ec || relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::optional<std::filesystem::path> resolveAssetPath(const std::filesystem::path& root, const std::string& relativePath) {
    if (!isSafeRelativeAssetPath(relativePath)) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::filesystem::path rootPath = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        return std::nullopt;
    }

    const std::filesystem::path candidate = std::filesystem::weakly_canonical(rootPath / std::filesystem::path(relativePath), ec);
    if (ec || !pathIsInsideRoot(rootPath, candidate)) {
        return std::nullopt;
    }
    return candidate;
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

void addAssetKeysFromDirectory(Logger& logger,
                               std::unordered_set<std::string>& outKeys,
                               const std::filesystem::path& root,
                               const std::string& prefix,
                               const std::unordered_set<std::string>& allowedExtensions = {}) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        if (ec) {
            logger.warn("Server",
                        "Failed to inspect asset directory %s: %s",
                        root.string().c_str(),
                        ec.message().c_str());
        }
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            logger.warn("Server",
                        "Failed to scan asset directory %s: %s",
                        root.string().c_str(),
                        ec.message().c_str());
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (!allowedExtensions.empty()) {
            const std::string ext = entry.path().extension().string();
            if (allowedExtensions.find(ext) == allowedExtensions.end()) {
                continue;
            }
        }
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), root, ec);
        if (ec || relative.empty()) {
            continue;
        }
        std::string key = relative.generic_string();
        if (key.empty()) {
            continue;
        }
        outKeys.insert(prefix + key);
    }
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

void Server::rebuildAllowedAssetKeys() {
    allowedAssetKeys_.clear();

    const std::filesystem::path assetsRoot = mapDirectory_.parent_path();
    addAssetKeysFromDirectory(logger_, allowedAssetKeys_, assetsRoot / "data", "data:");
    addAssetKeysFromDirectory(logger_, allowedAssetKeys_, assetsRoot / "ui", "ui_image:", {".png"});
    addAssetKeysFromDirectory(logger_, allowedAssetKeys_, assetsRoot / "ui", "ui_meta:", {".atlas"});

    for (const auto& [mapId, loadedMap] : maps_) {
        allowedAssetKeys_.insert("map:" + mapId);
        for (const std::string& asset : loadedMap.mapImageRefs()) {
            if (asset.empty()) continue;
            allowedAssetKeys_.insert("map_image:" + asset);
            const std::string metaFile = std::filesystem::path(asset).stem().string() + ".atlas";
            if (std::filesystem::exists(mapDirectory_.parent_path() / "map_images" / metaFile)) {
                allowedAssetKeys_.insert("map_meta:" + metaFile);
            }
        }
        for (const std::string& asset : loadedMap.characterImageRefs()) {
            if (asset.empty()) continue;
            allowedAssetKeys_.insert("character_image:" + asset);
        }
    }
}

std::string Server::computeContentRevision() const {
    std::string combined;
    std::vector<std::string> orderedMapIds;
    orderedMapIds.reserve(mapPaths_.size());
    for (const auto& [mapId, path] : mapPaths_) {
        (void)path;
        orderedMapIds.push_back(mapId);
    }
    std::sort(orderedMapIds.begin(), orderedMapIds.end());

    for (const std::string& mapId : orderedMapIds) {
        const std::string& path = mapPaths_.at(mapId);
        combined += mapId;
        combined += '\n';
        combined += readTextFile(path);
        combined += "\n---\n";
        const MapData* loadedMap = mapForId(mapId);
        if (!loadedMap) {
            continue;
        }
        for (const std::string& asset : loadedMap->mapImageRefs()) {
            combined += asset;
            combined += '\n';
            combined += readBinaryFile(mapDirectory_.parent_path() / "map_images" / asset);
            combined += "\n---\n";
            combined += readTextFile((mapDirectory_.parent_path() / "map_images" / (std::filesystem::path(asset).stem().string() + ".atlas")).string());
            combined += "\n---\n";
        }
        for (const std::string& asset : loadedMap->characterImageRefs()) {
            combined += asset;
            combined += '\n';
            combined += readBinaryFile(mapDirectory_.parent_path() / "characters" / asset);
            combined += "\n---\n";
        }
    }

    std::vector<std::string> orderedAssets(allowedAssetKeys_.begin(), allowedAssetKeys_.end());
    std::sort(orderedAssets.begin(), orderedAssets.end());
    for (const std::string& assetKey : orderedAssets) {
        combined += assetKey;
        combined += '\n';
        if (assetKey.rfind("data:", 0) == 0) {
            const auto path = resolveAssetPath(mapDirectory_.parent_path() / "data", assetKey.substr(5));
            if (path.has_value()) {
                combined += readTextFile(path->string());
            }
        } else if (assetKey.rfind("ui_image:", 0) == 0) {
            const auto path = resolveAssetPath(mapDirectory_.parent_path() / "ui", assetKey.substr(9));
            if (path.has_value()) {
                combined += readBinaryFile(*path);
            }
        } else if (assetKey.rfind("ui_meta:", 0) == 0) {
            const auto path = resolveAssetPath(mapDirectory_.parent_path() / "ui", assetKey.substr(8));
            if (path.has_value()) {
                combined += readTextFile(path->string());
            }
        }
        combined += "\n---\n";
    }
    return BuildDeterministicContentRevision(combined);
}

Server::PlayerPosition Server::defaultSpawnPosition(int playerid, const std::string& mapId) const {
    const MapData* activeMap = mapForId(mapId);
    if (!activeMap) {
        activeMap = &map_;
    }
    return PlayerPosition {
        activeMap->spawnX() + static_cast<float>((std::max(0, playerid - 1) % 6) * 36),
        activeMap->spawnY() + static_cast<float>((std::max(0, playerid - 1) / 6) * 36)
    };
}

bool Server::teleportPlayerWithinMap(int playerid, PlayerPosition nextPosition, const std::string& reason) {
    const MapData* activeMap = mapForPlayer(playerid);
    auto posIt = playerPositions_.find(playerid);
    if (!activeMap || posIt == playerPositions_.end()) {
        return false;
    }
    if (!activeMap->isWalkable(nextPosition.x, nextPosition.y, 15.0f)) {
        logger_.warn("Server", "Rejected intra-map teleport for player %d on %s", playerid, playerMapId(playerid).c_str());
        return false;
    }

    posIt->second = nextPosition;
    playerLastMoveAtMs_[playerid] = nowMs();
    playerActiveTriggers_.erase(playerid);

    void* peer = network_.peerForPlayer(playerid);
    sendPlayerJoinToPeer(peer, playerid);

    std::vector<uint8_t> pkt(1 + sizeof(int32_t) + sizeof(float) * 2);
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLAYER_MOVE);
    const int32_t sender = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + 1, &sender, sizeof(sender));
    std::memcpy(pkt.data() + 1 + sizeof(sender), &nextPosition.x, sizeof(nextPosition.x));
    std::memcpy(pkt.data() + 1 + sizeof(sender) + sizeof(nextPosition.x), &nextPosition.y, sizeof(nextPosition.y));
    for (int otherId : network_.playerIds()) {
        if (otherId == playerid) continue;
        if (playerMapId(otherId) != playerMapId(playerid)) continue;
        network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), pkt.size(), 0);
    }

    updatePlayerTriggers(playerid);
    if (!reason.empty()) {
        sendServerText(playerid, reason);
    }
    logger_.info("Server",
                 "Teleported player %d within %s to %.1f, %.1f",
                 playerid,
                 playerMapId(playerid).c_str(),
                 nextPosition.x,
                 nextPosition.y);
    return true;
}

void Server::updatePlayerTriggers(int playerid) {
    auto posIt = playerPositions_.find(playerid);
    if (posIt == playerPositions_.end()) return;
    const MapData* activeMap = mapForPlayer(playerid);
    if (!activeMap) return;

    std::unordered_set<int>& active = playerActiveTriggers_[playerid];
    std::unordered_set<int> nextActive;
    for (size_t i = 0; i < activeMap->objects().size(); ++i) {
        const auto& object = activeMap->objects()[i];
        const bool isTrigger = object.kind == "trigger";
        if (!isTrigger) continue;
        if (!activeMap->objectContainsPoint(object, posIt->second.x, posIt->second.y)) continue;
        nextActive.insert(static_cast<int>(i));
        if (active.find(static_cast<int>(i)) == active.end()) {
            script_.callFunction("OnMapTriggerEnter", playerid, static_cast<int>(i));
        }
    }
    active = std::move(nextActive);
}

void Server::cleanupPlayerSession(int playerid, int reason, bool notifyNetworkPeer) {
    if (playerid <= 0) {
        return;
    }

    void* peer = network_.peerForPlayer(playerid);
    if (notifyNetworkPeer && peer) {
        network_.disconnectPlayer(playerid, static_cast<uint32_t>(reason));
    }

    const std::string mapId = playerMapId(playerid);
    logger_.info("Server", "Player %d disconnected", playerid);
    script_.callFunction("OnPlayerLeaveMap", playerid);
    script_.callFunction("OnPlayerDisconnect", playerid, reason);
    plugins_.firePlayerDisconnect(playerid, reason);
    broadcastPlayerLeave(playerid, mapId);

    network_.removePlayer(playerid);
    playerPositions_.erase(playerid);
    playerNames_.erase(playerid);
    playerMapIds_.erase(playerid);
    playerLastMoveAtMs_.erase(playerid);
    playerLastSeenAtMs_.erase(playerid);
    playerActiveTriggers_.erase(playerid);
    inventory_.removeInventory(playerid);
    releasePlayerId(playerid);

    if (peer) {
        static_cast<ENetPeer*>(peer)->data = nullptr;
    }
}

void Server::disconnectTimedOutPlayers() {
    const uint64_t currentMs = nowMs();
    constexpr uint64_t kClientTimeoutMs = 5000;
    std::vector<int> stalePlayers;
    stalePlayers.reserve(playerLastSeenAtMs_.size());

    for (const auto& [playerid, lastSeenMs] : playerLastSeenAtMs_) {
        if (currentMs > lastSeenMs && (currentMs - lastSeenMs) >= kClientTimeoutMs) {
            stalePlayers.push_back(playerid);
        }
    }

    for (int playerid : stalePlayers) {
        logger_.warn("Server", "Timing out player %d after %llu ms without packets",
                     playerid,
                     static_cast<unsigned long long>(currentMs - playerLastSeenAtMs_[playerid]));
        cleanupPlayerSession(playerid, 11, true);
    }
}

void Server::sendSnapshotToPeer(void* peer, const std::string& mapId) {
    if (!peer) return;

    size_t sameMapCount = 0;
    for (const auto& [playerid, pos] : playerPositions_) {
        (void)pos;
        if (playerMapId(playerid) == mapId) {
            ++sameMapCount;
        }
    }
    const uint16_t count = static_cast<uint16_t>(std::min<size_t>(sameMapCount, 0xFFFF));
    size_t packetSize = 1 + 1 + sizeof(count);
    for (const auto& [playerid, pos] : playerPositions_) {
        if (playerMapId(playerid) != mapId) continue;
        (void)pos;
        const auto nameIt = playerNames_.find(playerid);
        const size_t nameLen = (nameIt != playerNames_.end()) ? std::min<size_t>(nameIt->second.size(), 24) : 0;
        packetSize += sizeof(int32_t) + sizeof(float) * 2 + sizeof(uint8_t) + nameLen;
    }

    std::vector<uint8_t> pkt(packetSize);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::SNAPSHOT);
    std::memcpy(pkt.data() + offset, &count, sizeof(count));
    offset += sizeof(count);

    size_t written = 0;
    for (const auto& [playerid, pos] : playerPositions_) {
        if (playerMapId(playerid) != mapId) continue;
        if (written >= count) break;
        const int32_t pid = static_cast<int32_t>(playerid);
        std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
        offset += sizeof(pid);
        std::memcpy(pkt.data() + offset, &pos.x, sizeof(pos.x));
        offset += sizeof(pos.x);
        std::memcpy(pkt.data() + offset, &pos.y, sizeof(pos.y));
        offset += sizeof(pos.y);
        const auto nameIt = playerNames_.find(playerid);
        const uint8_t nameLen = static_cast<uint8_t>((nameIt != playerNames_.end()) ? std::min<size_t>(nameIt->second.size(), 24) : 0);
        std::memcpy(pkt.data() + offset, &nameLen, sizeof(nameLen));
        offset += sizeof(nameLen);
        if (nameLen > 0) {
            std::memcpy(pkt.data() + offset, nameIt->second.data(), nameLen);
            offset += nameLen;
        }
        ++written;
    }

    network_.sendPacket(peer, pkt.data(), offset, 0);
}

void Server::sendPlayerJoinToPeer(void* peer, int playerid) {
    if (!peer) return;
    auto it = playerPositions_.find(playerid);
    if (it == playerPositions_.end()) return;

    std::vector<uint8_t> pkt(1 + 1 + sizeof(int32_t) + sizeof(float) * 2);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_JOIN);

    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
    offset += sizeof(pid);
    std::memcpy(pkt.data() + offset, &it->second.x, sizeof(it->second.x));
    offset += sizeof(it->second.x);
    std::memcpy(pkt.data() + offset, &it->second.y, sizeof(it->second.y));
    offset += sizeof(it->second.y);

    network_.sendPacket(peer, pkt.data(), offset, 0);
}

void Server::broadcastPlayerJoin(int playerid, int exceptPlayerid) {
    auto it = playerPositions_.find(playerid);
    if (it == playerPositions_.end()) return;
    const std::string mapId = playerMapId(playerid);

    std::vector<uint8_t> pkt(1 + 1 + sizeof(int32_t) + sizeof(float) * 2);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_JOIN);

    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
    offset += sizeof(pid);
    std::memcpy(pkt.data() + offset, &it->second.x, sizeof(it->second.x));
    offset += sizeof(it->second.x);
    std::memcpy(pkt.data() + offset, &it->second.y, sizeof(it->second.y));
    offset += sizeof(it->second.y);

    for (int otherId : network_.playerIds()) {
        if (otherId == exceptPlayerid) continue;
        if (otherId == playerid) continue;
        if (playerMapId(otherId) != mapId) continue;
        network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), offset, 0);
    }
}

void Server::broadcastPlayerLeave(int playerid, const std::string& mapId) {
    std::vector<uint8_t> pkt(1 + 1 + sizeof(int32_t));
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_LEAVE);
    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
    offset += sizeof(pid);

    for (int otherId : network_.playerIds()) {
        if (otherId == playerid) continue;
        if (playerMapId(otherId) != mapId) continue;
        network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), offset, 0);
    }
}

void Server::broadcastPlayerName(int playerid) {
    auto it = playerNames_.find(playerid);
    if (it == playerNames_.end() || it->second.empty()) return;
    const std::string mapId = playerMapId(playerid);

    const uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(it->second.size(), 24));
    std::vector<uint8_t> pkt(1 + 1 + sizeof(int32_t) + sizeof(uint8_t) + nameLen);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_NAME);
    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
    offset += sizeof(pid);
    std::memcpy(pkt.data() + offset, &nameLen, sizeof(nameLen));
    offset += sizeof(nameLen);
    std::memcpy(pkt.data() + offset, it->second.data(), nameLen);
    offset += nameLen;
    for (int otherId : network_.playerIds()) {
        if (playerMapId(otherId) != mapId) continue;
        network_.sendPacket(network_.peerForPlayer(otherId), pkt.data(), offset, 0);
    }
}

void Server::sendPlayerNameToPeer(void* peer, int playerid) {
    if (!peer) return;
    auto it = playerNames_.find(playerid);
    if (it == playerNames_.end() || it->second.empty()) return;

    const uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(it->second.size(), 24));
    std::vector<uint8_t> pkt(1 + 1 + sizeof(int32_t) + sizeof(uint8_t) + nameLen);
    size_t offset = 0;
    pkt[offset++] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[offset++] = static_cast<uint8_t>(GameStateType::PLAYER_NAME);
    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt.data() + offset, &pid, sizeof(pid));
    offset += sizeof(pid);
    std::memcpy(pkt.data() + offset, &nameLen, sizeof(nameLen));
    offset += sizeof(nameLen);
    std::memcpy(pkt.data() + offset, it->second.data(), nameLen);
    offset += nameLen;
    network_.sendPacket(peer, pkt.data(), offset, 0);
}

void Server::sendServerText(void* peer, const std::string& text) {
    if (!peer || text.empty()) return;

    std::vector<uint8_t> pkt(1 + 1 + text.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[1] = static_cast<uint8_t>(GameStateType::SERVER_TEXT);
    std::memcpy(pkt.data() + 2, text.data(), text.size());
    network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
}

void Server::sendServerText(int playerid, const std::string& text) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer) return;
    sendServerText(peer, text);
}

void Server::sendObjectiveText(int playerid, const std::string& text) {
    void* peer = network_.peerForPlayer(playerid);
    if (!peer) return;
    std::vector<uint8_t> pkt(1 + 1 + text.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[1] = static_cast<uint8_t>(GameStateType::OBJECTIVE_TEXT);
    if (!text.empty()) {
        std::memcpy(pkt.data() + 2, text.data(), text.size());
    }
    network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
}

void Server::sendUpdateManifestToPeer(void* peer) {
    if (!peer) return;

    std::ostringstream manifest;
    manifest << "server_name=nganu.mp\n";
    manifest << "content_revision=" << contentRevision_ << "\n";
    manifest << "world_name=" << map_.worldName() << "\n";
    manifest << "map_id=" << map_.mapId() << "\n";

    std::vector<std::string> assets(allowedAssetKeys_.begin(), allowedAssetKeys_.end());
    std::sort(assets.begin(), assets.end());
    for (const std::string& asset : assets) {
        manifest << "asset=" << asset << "\n";
    }

    const std::string blob = manifest.str();
    std::vector<uint8_t> pkt(1 + 1 + blob.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
    pkt[1] = static_cast<uint8_t>(PluginMessageType::UPDATE_MANIFEST);
    std::memcpy(pkt.data() + 2, blob.data(), blob.size());
    network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
}

void Server::sendAssetBlobToPeer(void* peer, const std::string& assetKey) {
    if (!peer || assetKey.empty()) return;
    if (allowedAssetKeys_.find(assetKey) == allowedAssetKeys_.end()) {
        logger_.warn("Server", "Rejected unlisted asset request: %s", assetKey.c_str());
        return;
    }

    std::string kind;
    std::string content;
    if (assetKey.rfind("map:", 0) == 0) {
        kind = "map";
        const std::string mapId = assetKey.substr(4);
        auto pathIt = mapPaths_.find(mapId);
        if (pathIt == mapPaths_.end()) {
            logger_.warn("Server", "Unknown map requested: %s", mapId.c_str());
            return;
        }
        content = readTextFile(pathIt->second);
        if (content.empty()) {
            logger_.error("Server", "Failed to read map asset for %s", assetKey.c_str());
            return;
        }
    } else if (assetKey.rfind("map_image:", 0) == 0) {
        kind = "image";
        const std::string fileName = assetKey.substr(10);
        const auto imagePath = resolveAssetPath(mapDirectory_.parent_path() / "map_images", fileName);
        if (!imagePath.has_value()) {
            logger_.warn("Server", "Rejected unsafe map image asset request: %s", assetKey.c_str());
            return;
        }
        content = readBinaryFile(*imagePath);
        if (content.empty()) {
            logger_.error("Server", "Failed to read image asset for %s", assetKey.c_str());
            return;
        }
        content = hexEncode(content);
    } else if (assetKey.rfind("map_meta:", 0) == 0) {
        kind = "meta";
        const std::string fileName = assetKey.substr(9);
        const auto metaPath = resolveAssetPath(mapDirectory_.parent_path() / "map_images", fileName);
        if (!metaPath.has_value()) {
            logger_.warn("Server", "Rejected unsafe map metadata asset request: %s", assetKey.c_str());
            return;
        }
        content = readTextFile(metaPath->string());
        if (content.empty()) {
            logger_.warn("Server", "Failed to read map metadata asset for %s", assetKey.c_str());
            return;
        }
    } else if (assetKey.rfind("character_image:", 0) == 0) {
        kind = "image";
        const std::string fileName = assetKey.substr(16);
        const auto imagePath = resolveAssetPath(mapDirectory_.parent_path() / "characters", fileName);
        if (!imagePath.has_value()) {
            logger_.warn("Server", "Rejected unsafe character image asset request: %s", assetKey.c_str());
            return;
        }
        content = readBinaryFile(*imagePath);
        if (content.empty()) {
            logger_.error("Server", "Failed to read image asset for %s", assetKey.c_str());
            return;
        }
        content = hexEncode(content);
    } else if (assetKey.rfind("ui_image:", 0) == 0) {
        kind = "image";
        const std::string fileName = assetKey.substr(9);
        const auto imagePath = resolveAssetPath(mapDirectory_.parent_path() / "ui", fileName);
        if (!imagePath.has_value()) {
            logger_.warn("Server", "Rejected unsafe UI image asset request: %s", assetKey.c_str());
            return;
        }
        content = readBinaryFile(*imagePath);
        if (content.empty()) {
            logger_.error("Server", "Failed to read UI image asset for %s", assetKey.c_str());
            return;
        }
        content = hexEncode(content);
    } else if (assetKey.rfind("ui_meta:", 0) == 0) {
        kind = "meta";
        const std::string fileName = assetKey.substr(8);
        const auto metaPath = resolveAssetPath(mapDirectory_.parent_path() / "ui", fileName);
        if (!metaPath.has_value()) {
            logger_.warn("Server", "Rejected unsafe UI metadata asset request: %s", assetKey.c_str());
            return;
        }
        content = readTextFile(metaPath->string());
        if (content.empty()) {
            logger_.warn("Server", "Failed to read UI metadata asset for %s", assetKey.c_str());
            return;
        }
    } else if (assetKey.rfind("data:", 0) == 0) {
        kind = "data";
        const std::string dataPath = assetKey.substr(5); /* strip "data:" */
        const auto filePath = resolveAssetPath(mapDirectory_.parent_path() / "data", dataPath);
        if (!filePath.has_value()) {
            logger_.warn("Server", "Rejected unsafe data asset request: %s", assetKey.c_str());
            return;
        }
        content = readTextFile(filePath->string());
        if (content.empty()) {
            logger_.warn("Server", "Failed to read data asset for %s", assetKey.c_str());
            return;
        }
    } else {
        logger_.warn("Server", "Unknown asset requested: %s", assetKey.c_str());
        return;
    }

    std::ostringstream blob;
    blob << "key=" << assetKey << "\n";
    blob << "kind=" << kind << "\n";
    blob << "revision=" << contentRevision_ << "\n";
    if (kind == "image") {
        blob << "encoding=hex\n";
    }
    blob << "---\n";
    blob << content;

    const std::string payload = blob.str();
    std::vector<uint8_t> pkt(1 + 1 + payload.size());
    pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
    pkt[1] = static_cast<uint8_t>(PluginMessageType::ASSET_BLOB);
    std::memcpy(pkt.data() + 2, payload.data(), payload.size());
    network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
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
            playerLastMoveAtMs_[pid] = nowMs();
            playerLastSeenAtMs_[pid] = nowMs();
            playerNames_[pid] = "Player " + std::to_string(pid);
            logger_.info("Server", "Player %d connected", pid);

            inventory_.createInventory(pid);
            script_.callFunction("OnPlayerConnect", pid);
            script_.callFunction("OnPlayerEnterMap", pid);
            plugins_.firePlayerConnect(pid);

            /* Send HANDSHAKE with player ID */
            uint8_t pkt[5];
            pkt[0] = static_cast<uint8_t>(PacketOpcode::HANDSHAKE);
            std::memcpy(pkt + 1, &pid, sizeof(pid));
            network_.sendPacket(event.peer, pkt, sizeof(pkt), 0);
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
        if (Packet::payload(data)[0] == static_cast<uint8_t>(PluginMessageType::PLAYER_NAME)) {
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
        if (Packet::payload(data)[0] == static_cast<uint8_t>(PluginMessageType::HEARTBEAT)) {
            break;
        }
        if (Packet::payload(data)[0] == static_cast<uint8_t>(PluginMessageType::UPDATE_PROBE)) {
            sendUpdateManifestToPeer(network_.peerForPlayer(playerid));
            break;
        }
        if (Packet::payload(data)[0] == static_cast<uint8_t>(PluginMessageType::ASSET_REQUEST)) {
            const size_t keyLen = Packet::payloadLen(len) - 1;
            if (keyLen < 1 || keyLen > 128) {
                logger_.warn("Server", "Malformed asset request length %zu from player %d", keyLen, playerid);
                break;
            }

            std::string assetKey(reinterpret_cast<const char*>(Packet::payload(data) + 1), keyLen);
            sendAssetBlobToPeer(network_.peerForPlayer(playerid), assetKey);
            break;
        }
        plugins_.fireReceive(playerid, Packet::payload(data), Packet::payloadLen(len));
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
