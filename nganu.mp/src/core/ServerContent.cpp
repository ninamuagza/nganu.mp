#include "core/Server.h"
#include "core/ContentRevision.h"
#include "shared/ContentIntegrity.h"
#include "shared/Protocol.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace {
uint64_t nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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
    logger_.info("Server", "Player %d disconnected (reason=%d)", playerid, reason);
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
    playerLastManifestProbeAtMs_.erase(playerid);
    playerAssetReqWindowStartAtMs_.erase(playerid);
    playerAssetReqCountInWindow_.erase(playerid);
    playerActiveTriggers_.erase(playerid);
    playerSessionReadyIds_.erase(playerid);
    inventory_.removeInventory(playerid);
    releasePlayerId(playerid);

    if (peer) {
        static_cast<ENetPeer*>(peer)->data = nullptr;
    }
}

void Server::disconnectTimedOutPlayers() {
    const uint64_t currentMs = nowMs();
    std::vector<int> stalePlayers;
    stalePlayers.reserve(playerLastSeenAtMs_.size());

    for (const auto& [playerid, lastSeenMs] : playerLastSeenAtMs_) {
        const bool sessionReady = playerSessionReadyIds_.find(playerid) != playerSessionReadyIds_.end();
        const uint64_t timeoutMs = sessionReady ? activeClientTimeoutMs_ : bootstrapClientTimeoutMs_;
        if (currentMs > lastSeenMs && (currentMs - lastSeenMs) >= timeoutMs) {
            stalePlayers.push_back(playerid);
        }
    }

    for (int playerid : stalePlayers) {
        const bool sessionReady = playerSessionReadyIds_.find(playerid) != playerSessionReadyIds_.end();
        const uint64_t timeoutMs = sessionReady ? activeClientTimeoutMs_ : bootstrapClientTimeoutMs_;
        logger_.warn("Server", "Timing out player %d after %llu ms without packets (phase=%s timeout=%llu ms)",
                     playerid,
                     static_cast<unsigned long long>(currentMs - playerLastSeenAtMs_[playerid]),
                     sessionReady ? "active" : "bootstrap",
                     static_cast<unsigned long long>(timeoutMs));
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

void Server::sendHandshakeToPeer(void* peer, int playerid) {
    if (!peer || playerid <= 0) return;

    uint8_t pkt[1 + sizeof(int32_t) + sizeof(uint16_t)];
    pkt[0] = static_cast<uint8_t>(PacketOpcode::HANDSHAKE);
    const int32_t pid = static_cast<int32_t>(playerid);
    std::memcpy(pkt + 1, &pid, sizeof(pid));
    const uint16_t protocolVersion = Protocol::kProtocolVersion;
    std::memcpy(pkt + 1 + sizeof(pid), &protocolVersion, sizeof(protocolVersion));
    network_.sendPacket(peer, pkt, sizeof(pkt), 0);
}

void Server::sendUpdateManifestToPeer(void* peer) {
    if (!peer) return;

    std::ostringstream manifest;
    manifest << "server_name=nganu.mp\n";
    manifest << "protocol_version=" << Protocol::kProtocolVersion << "\n";
    manifest << "asset_chunk_bytes=" << Protocol::kAssetChunkBytes << "\n";
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

    const std::string checksum = Nganu::ContentIntegrity::Fnv1a64Hex(content);
    static_assert(Protocol::kAssetChunkBytes > 0, "Asset chunk size must be greater than zero");
    const size_t chunkBytes = Protocol::kAssetChunkBytes;
    const size_t chunkTotal = std::max<size_t>(1, (content.size() + chunkBytes - 1) / chunkBytes);
    for (size_t chunkIndex = 0; chunkIndex < chunkTotal; ++chunkIndex) {
        const size_t start = chunkIndex * chunkBytes;
        const size_t count = (start < content.size())
                                 ? std::min(chunkBytes, content.size() - start)
                                 : 0;
        const std::string chunkContent = content.substr(start, count);
        std::ostringstream blob;
        blob << "key=" << assetKey << "\n";
        blob << "kind=" << kind << "\n";
        blob << "revision=" << contentRevision_ << "\n";
        blob << "checksum=" << checksum << "\n";
        blob << "content_size=" << content.size() << "\n";
        blob << "chunk_index=" << chunkIndex << "\n";
        blob << "chunk_total=" << chunkTotal << "\n";
        if (kind == "image") {
            blob << "encoding=hex\n";
        }
        blob << "---\n";
        blob << chunkContent;

        const std::string payload = blob.str();
        std::vector<uint8_t> pkt(1 + 1 + payload.size());
        pkt[0] = static_cast<uint8_t>(PacketOpcode::PLUGIN_MESSAGE);
        pkt[1] = static_cast<uint8_t>(PluginMessageType::ASSET_BLOB);
        std::memcpy(pkt.data() + 2, payload.data(), payload.size());
        network_.sendPacket(peer, pkt.data(), pkt.size(), 0);
    }
}
