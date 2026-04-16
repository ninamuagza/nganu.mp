#include "Game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace {
Color BackgroundSky() { return Color {197, 226, 233, 255}; }
Color LocalColor() { return Color {255, 205, 96, 255}; }
Color PanelColor() { return Color {20, 29, 34, 215}; }
Color AccentColor() { return Color {111, 219, 159, 255}; }
Color LoginCardColor() { return Color {18, 28, 33, 228}; }

Vector2 NormalizeOrZero(Vector2 value) {
    const float length = std::sqrt((value.x * value.x) + (value.y * value.y));
    if (length <= 0.0001f) {
        return Vector2 {0.0f, 0.0f};
    }
    return Vector2 {value.x / length, value.y / length};
}

float LerpValue(float from, float to, float amount) {
    return from + ((to - from) * amount);
}

Vector2 ScaleVector(Vector2 value, float scale) {
    return Vector2 {value.x * scale, value.y * scale};
}

std::string FacingForVelocity(Vector2 velocity) {
    if (std::fabs(velocity.x) > std::fabs(velocity.y)) {
        return velocity.x >= 0.0f ? "east" : "west";
    }
    return velocity.y >= 0.0f ? "south" : "north";
}

int HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

}

Game::Game() {
    player_.name = "Fanorisky";
    player_.position = Vector2 {160.0f, 160.0f};
    player_.bodyColor = LocalColor();
    player_.radius = 15.0f;
    loginName_ = player_.name;
    loginHost_ = "127.0.0.1";
    loginPort_ = "7777";
    loginStatus_ = "Checking server update...";

    camera_.offset = Vector2 {640.0f, 360.0f};
    camera_.target = player_.position;
    camera_.rotation = 0.0f;
    camera_.zoom = 1.0f;
    lastSentPosition_ = player_.position;
    guideNpc_.name.clear();
    guideNpc_.title.clear();
    guideNpc_.position = Vector2 {160.0f, 160.0f};
    guideNpc_.radius = 16.0f;
    guideNpc_.bodyColor = Color {120, 210, 255, 255};
    starterQuest_.targetPosition = Vector2 {160.0f, 160.0f};
    starterQuest_.targetRadius = 90.0f;
    starterQuest_.title.clear();
    starterQuest_.description.clear();
    ConfigureWorldDrivenGameplay();

    AddChatLine("[System] Booting nganu.game client");
}

void Game::Update(float dt) {
    worldTime_ += dt;

    if (IsKeyPressed(KEY_F1)) {
        showDebug_ = !showDebug_;
    }

    if (!bootStarted_) {
        BeginBootUpdateCheck();
    }

    if (uiMode_ == UiMode::Boot) {
        UpdateNetwork(dt);
        return;
    }

    if (uiMode_ == UiMode::RetryWait) {
        UpdateRetryWait(dt);
        return;
    }

    if (uiMode_ == UiMode::MainMenu) {
        UpdateLoginInput();
        UpdateNetwork(dt);
        return;
    }

    if (uiMode_ == UiMode::LoggingIn) {
        UpdateLoggingIn(dt);
        UpdateNetwork(dt);
        return;
    }

    UpdateChatInput();
    UpdateChatScroll(dt);
    UpdatePlayer(dt);
    UpdateNetwork(dt);
    UpdateNpcAndQuest();
    UpdateRemoteSmoothing(dt);
    UpdateCamera(dt);
}

void Game::BeginBootUpdateCheck() {
    bootStarted_ = true;
    network_.Disconnect();
    remotePlayers_.clear();
    localPlayerId_ = 0;
    handshakeReady_ = false;
    bootstrapRequested_ = false;
    manifestWait_ = 0.0f;
    retryCountdown_ = 0.0f;
    stateTimer_ = 0.0f;
    manifest_ = ContentManifest {};
    mapReady_ = false;
    pendingMapAssetKey_.clear();
    pendingMapId_.clear();
    hasPendingSpawnPosition_ = false;
    lastMapAssetSource_ = "none";
    lastAppliedMapAssetKey_.clear();
    player_.position = world_.spawnPoint();
    lastSentPosition_ = player_.position;
    camera_.target = player_.position;
    loginStatus_ = "Checking server update...";
    uiMode_ = UiMode::Boot;
    AddChatLine("[System] Checking server update...");

    int port = 0;
    try {
        port = std::stoi(loginPort_);
    } catch (...) {
        BeginRetryWait("Invalid server port. Retrying in 10 seconds.");
        return;
    }

    if (port < 1 || port > 65535) {
        BeginRetryWait("Invalid server port. Retrying in 10 seconds.");
        return;
    }

    if (!network_.Connect(loginHost_, static_cast<uint16_t>(port))) {
        BeginRetryWait("Server did not respond. Retrying in 10 seconds.");
    }
}

void Game::BeginRetryWait(const std::string& reason) {
    network_.Disconnect();
    bootstrapRequested_ = false;
    manifestWait_ = 0.0f;
    retryCountdown_ = 10.0f;
    loginStatus_ = reason;
    uiMode_ = UiMode::RetryWait;
    AddChatLine("[System] " + reason);
}

void Game::UpdateLoginInput() {
    if (IsKeyPressed(KEY_F5)) {
        StartConnection();
        return;
    }

    if (IsKeyPressed(KEY_TAB)) {
        switch (loginField_) {
        case LoginField::Name:
            loginField_ = LoginField::Host;
            break;
        case LoginField::Host:
            loginField_ = LoginField::Port;
            break;
        case LoginField::Port:
            loginField_ = LoginField::Name;
            break;
        }
    }

    if (IsKeyPressed(KEY_ENTER)) {
        StartLogin();
        return;
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        std::string* target = nullptr;
        switch (loginField_) {
        case LoginField::Name:
            target = &loginName_;
            break;
        case LoginField::Host:
            target = &loginHost_;
            break;
        case LoginField::Port:
            target = &loginPort_;
            break;
        }
        if (target && !target->empty()) {
            target->pop_back();
        }
    }

    int pressed = GetCharPressed();
    while (pressed > 0) {
        const bool printable = pressed >= 32 && pressed <= 126;
        if (printable) {
            switch (loginField_) {
            case LoginField::Name:
                if (loginName_.size() < 24) {
                    loginName_.push_back(static_cast<char>(pressed));
                }
                break;
            case LoginField::Host:
                if (loginHost_.size() < 64) {
                    loginHost_.push_back(static_cast<char>(pressed));
                }
                break;
            case LoginField::Port:
                if (std::isdigit(pressed) && loginPort_.size() < 5) {
                    loginPort_.push_back(static_cast<char>(pressed));
                }
                break;
            }
        }
        pressed = GetCharPressed();
    }
}

void Game::StartConnection() {
    BeginBootUpdateCheck();
}

void Game::UpdateRetryWait(float dt) {
    retryCountdown_ = std::max(0.0f, retryCountdown_ - dt);
    const int secondsLeft = static_cast<int>(std::ceil(retryCountdown_));
    loginStatus_ = "Server did not respond. Retrying in " + std::to_string(secondsLeft) + " seconds.";
    if (retryCountdown_ <= 0.0f) {
        BeginBootUpdateCheck();
    }
}

void Game::UpdateLoggingIn(float dt) {
    stateTimer_ = std::max(0.0f, stateTimer_ - dt);
    if (stateTimer_ > 0.0f) {
        return;
    }

    if (!network_.IsConnected() || !handshakeReady_) {
        return;
    }

    loginStatus_ = "Spawned as " + player_.name;
    AddChatLine("[System] Spawned into " + (manifest_.worldName.empty() ? std::string("the world") : manifest_.worldName));
    uiMode_ = UiMode::World;
}

void Game::StartLogin() {
    if (loginName_.empty()) {
        loginStatus_ = "Player name is required";
        return;
    }
    if (!manifest_.valid) {
        loginStatus_ = "Client content is not ready. Press F5 to re-check.";
        return;
    }
    if (!mapReady_) {
        loginStatus_ = "Map content is still loading. Press F5 if needed.";
        return;
    }
    if (!network_.IsConnected() || !handshakeReady_) {
        loginStatus_ = "Server session is offline. Press F5 to reconnect.";
        return;
    }

    player_.name = loginName_;
    player_.position = world_.spawnPoint();
    lastSentPosition_ = player_.position;
    camera_.target = player_.position;
    chatFocused_ = false;
    chatInput_.clear();
    sendAccumulator_ = 0.0f;
    starterQuest_.offered = false;
    starterQuest_.accepted = false;
    starterQuest_.completed = false;
    starterQuest_.turnedIn = false;
    currentObjective_.clear();
    if (!network_.SendPlayerName(player_.name)) {
        loginStatus_ = "Login failed to reach server";
        return;
    }

    loginStatus_ = "Login succeeded. Spawning...";
    stateTimer_ = 0.20f;
    uiMode_ = UiMode::LoggingIn;
}

void Game::UpdatePlayer(float dt) {
    if (!mapReady_) {
        player_.velocity = Vector2 {};
        return;
    }
    if (chatFocused_) {
        player_.velocity = Vector2 {};
        return;
    }

    Vector2 input {};
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) input.x -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) input.x += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) input.y -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) input.y += 1.0f;

    const bool running = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const float moveSpeed = running ? 240.0f : 165.0f;
    player_.velocity = ScaleVector(NormalizeOrZero(input), moveSpeed);

    Vector2 nextPosition = player_.position;

    nextPosition.x += player_.velocity.x * dt;
    if (world_.IsWalkable(nextPosition, player_.radius)) {
        player_.position.x = nextPosition.x;
    }

    nextPosition = player_.position;
    nextPosition.y += player_.velocity.y * dt;
    if (world_.IsWalkable(nextPosition, player_.radius)) {
        player_.position.y = nextPosition.y;
    }

}

void Game::UpdateCamera(float dt) {
    camera_.offset = Vector2 {
        static_cast<float>(GetScreenWidth()) * 0.5f,
        static_cast<float>(GetScreenHeight()) * 0.5f
    };

    const float smoothing = std::clamp(dt * 6.0f, 0.0f, 1.0f);
    camera_.target.x = LerpValue(camera_.target.x, player_.position.x, smoothing);
    camera_.target.y = LerpValue(camera_.target.y, player_.position.y, smoothing);
}

void Game::UpdateNetwork(float dt) {
    network_.Update(dt);
    for (const NetworkEvent& event : network_.ConsumeEvents()) {
        HandleNetworkEvent(event);
    }

    if (uiMode_ == UiMode::Boot && bootstrapRequested_ && !manifest_.valid) {
        manifestWait_ += dt;
        if (manifestWait_ >= 4.0f) {
            BeginRetryWait("Server update check timed out. Retrying in 10 seconds.");
            return;
        }
    }

    if (uiMode_ != UiMode::World || !mapReady_) {
        return;
    }

    sendAccumulator_ += dt;
    const bool movedEnough =
        std::fabs(player_.position.x - lastSentPosition_.x) > 0.5f ||
        std::fabs(player_.position.y - lastSentPosition_.y) > 0.5f;

    if (network_.IsConnected() && sendAccumulator_ >= 0.10f && movedEnough) {
        if (network_.SendPlayerPosition(player_.position.x, player_.position.y)) {
            lastSentPosition_ = player_.position;
        }
        sendAccumulator_ = 0.0f;
    }
}

void Game::HandleNetworkEvent(const NetworkEvent& event) {
    switch (event.type) {
    case NetworkEvent::Type::Connected:
        AddChatLine("[System] Connected to server");
        if (uiMode_ == UiMode::Boot) {
            loginStatus_ = "Server reached. Checking content revision...";
            manifestWait_ = 0.0f;
            bootstrapRequested_ = network_.RequestUpdateManifest();
            if (!bootstrapRequested_) {
                BeginRetryWait("Server update probe failed. Retrying in 10 seconds.");
            }
        }
        break;
    case NetworkEvent::Type::ConnectionFailed:
        if (uiMode_ == UiMode::Boot) {
            BeginRetryWait("Server did not respond. Retrying in 10 seconds.");
        } else {
            AddChatLine("[System] Connection failed");
            loginStatus_ = "Connection failed. Press F5 to retry.";
            uiMode_ = UiMode::MainMenu;
        }
        break;
    case NetworkEvent::Type::Disconnected:
        remotePlayers_.clear();
        localPlayerId_ = 0;
        handshakeReady_ = false;
        bootstrapRequested_ = false;
        mapReady_ = false;
        pendingMapAssetKey_.clear();
        pendingMapId_.clear();
        hasPendingSpawnPosition_ = false;
        AddChatLine("[System] Disconnected from server");
        if (uiMode_ == UiMode::Boot && !manifest_.valid) {
            BeginRetryWait("Server did not respond. Retrying in 10 seconds.");
        } else {
            manifest_ = ContentManifest {};
            loginStatus_ = "Connection lost. Back at main menu. Press F5 to reconnect.";
            uiMode_ = UiMode::MainMenu;
        }
        break;
    case NetworkEvent::Type::Handshake:
        localPlayerId_ = event.playerId;
        handshakeReady_ = true;
        AddChatLine("[System] Assigned player id " + std::to_string(localPlayerId_));
        if (uiMode_ == UiMode::Boot) {
            loginStatus_ = "Session ready, waiting for content manifest...";
        }
        break;
    case NetworkEvent::Type::AssetManifest:
        ApplyManifest(event.text);
        manifestWait_ = 0.0f;
        bootstrapRequested_ = false;
        BeginMapBootstrap();
        if (mapReady_) {
            loginStatus_ = "Server ready. Press Enter to log in.";
            uiMode_ = UiMode::MainMenu;
        } else {
            loginStatus_ = "Manifest ready. Downloading map asset...";
            uiMode_ = UiMode::Boot;
        }
        break;
    case NetworkEvent::Type::AssetBlob: {
        const std::optional<AssetBlob> blob = ParseAssetBlob(event.text);
        if (!blob.has_value()) {
            BeginRetryWait("Received invalid asset blob. Retrying in 10 seconds.");
            break;
        }
        SaveAssetToCache(*blob);
        if (blob->kind == "map") {
            ApplyMapAsset(*blob);
            if (mapReady_) {
                if (uiMode_ == UiMode::Boot) {
                    loginStatus_ = "Server ready. Press Enter to log in.";
                    uiMode_ = UiMode::MainMenu;
                } else if (uiMode_ == UiMode::World) {
                    loginStatus_ = "Transferred to " + (world_.worldName().empty() ? world_.mapId() : world_.worldName());
                    AddChatLine("[System] World loaded: " + world_.mapId());
                }
            }
        } else if (blob->kind == "meta" && mapReady_ && !lastAppliedMapAssetKey_.empty()) {
            std::string cachedMap;
            if (LoadCachedAsset(lastAppliedMapAssetKey_, manifest_.revision, cachedMap)) {
                const Vector2 keepPosition = player_.position;
                if (world_.LoadFromMapAsset(cachedMap)) {
                    ConfigureWorldDrivenGameplay();
                    player_.position = keepPosition;
                    lastSentPosition_ = keepPosition;
                }
            }
        }
        break;
    }
    case NetworkEvent::Type::MapTransfer:
        remotePlayers_.clear();
        currentObjective_.clear();
        pendingMapId_ = event.mapId;
        pendingSpawnPosition_ = Vector2 {event.x, event.y};
        hasPendingSpawnPosition_ = true;
        AddChatLine("[System] Transferring to map " + event.mapId);
        BeginMapBootstrapForAsset("map:" + event.mapId, "Loading map " + event.mapId + "...");
        break;
    case NetworkEvent::Type::SnapshotPlayer:
    case NetworkEvent::Type::PlayerJoined: {
        if (event.playerId == localPlayerId_ && localPlayerId_ != 0) {
            player_.position = Vector2 {event.x, event.y};
            lastSentPosition_ = player_.position;
            break;
        }
        ApplyRemotePlayerState(event.playerId, event.x, event.y);
        if (!event.text.empty()) {
            ApplyPlayerName(event.playerId, event.text);
        }

        if (event.type == NetworkEvent::Type::PlayerJoined) {
            AddChatLine("[System] " + NameForPlayer(event.playerId) + " joined");
        }
        break;
    }
    case NetworkEvent::Type::PlayerLeft:
        if (event.playerId != localPlayerId_) {
            AddChatLine("[System] " + NameForPlayer(event.playerId) + " left");
            remotePlayers_.erase(event.playerId);
        }
        break;
    case NetworkEvent::Type::PlayerMoved: {
        if (event.playerId == localPlayerId_) break;
        ApplyRemotePlayerState(event.playerId, event.x, event.y);
        break;
    }
    case NetworkEvent::Type::ChatMessage:
        AddChatLine("[" + NameForPlayer(event.senderId) + "] " + event.text);
        break;
    case NetworkEvent::Type::PlayerName:
        ApplyPlayerName(event.playerId, event.text);
        break;
    case NetworkEvent::Type::ServerText:
        AddChatLine("[Server] " + event.text);
        break;
    case NetworkEvent::Type::ObjectiveText:
        currentObjective_ = event.text;
        break;
    }
}

void Game::AddChatLine(const std::string& line) {
    chatEntries_.push_back(ChatEntry {line, 0.0f});
    if (chatEntries_.size() > 100) {
        chatEntries_.erase(chatEntries_.begin());
    }
    chatScrollTarget_ = 0.0f;
}

void Game::ApplyManifest(const std::string& rawManifest) {
    ContentManifest next;
    std::istringstream stream(rawManifest);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "server_name") {
            next.serverName = value;
        } else if (key == "content_revision") {
            next.revision = value;
        } else if (key == "world_name") {
            next.worldName = value;
        } else if (key == "map_id") {
            next.mapId = value;
        } else if (key == "asset") {
            next.assets.push_back(value);
        }
    }

    next.valid = !next.serverName.empty() || !next.revision.empty() || !next.worldName.empty();
    manifest_ = std::move(next);

    if (manifest_.valid) {
        const std::string serverName = manifest_.serverName.empty() ? "server" : manifest_.serverName;
        AddChatLine("[System] Content manifest ready from " + serverName);
        if (!manifest_.revision.empty()) {
            AddChatLine("[System] Content revision " + manifest_.revision);
        }
        AddChatLine("[System] Assets listed: " + std::to_string(manifest_.assets.size()));
    }
}

std::optional<AssetBlob> Game::ParseAssetBlob(const std::string& rawBlob) const {
    AssetBlob blob;
    std::istringstream stream(rawBlob);
    std::string line;
    bool inContent = false;
    while (std::getline(stream, line)) {
        if (!inContent) {
            if (line == "---") {
                inContent = true;
                continue;
            }

            const size_t sep = line.find('=');
            if (sep == std::string::npos) {
                continue;
            }

            const std::string key = line.substr(0, sep);
            const std::string value = line.substr(sep + 1);
            if (key == "key") {
                blob.key = value;
            } else if (key == "kind") {
                blob.kind = value;
            } else if (key == "revision") {
                blob.revision = value;
            } else if (key == "encoding") {
                blob.encoding = value;
            }
        } else {
            if (!blob.content.empty()) {
                blob.content.push_back('\n');
            }
            blob.content += line;
        }
    }

    if (blob.key.empty() || blob.kind.empty()) {
        return std::nullopt;
    }

    return blob;
}

std::filesystem::path Game::CacheDirectory() const {
    return std::filesystem::current_path() / "cache";
}

std::filesystem::path Game::CachePathForAsset(const std::string& assetKey, const std::string& revision) const {
    std::string safe = assetKey;
    for (char& ch : safe) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }

    std::string safeRevision = revision.empty() ? "unknown" : revision;
    for (char& ch : safeRevision) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }

    return CacheDirectory() / (safeRevision + "_" + safe + ".txt");
}

std::filesystem::path Game::ImageCachePathForAsset(const std::string& assetKey, const std::string& revision) const {
    std::string filename = assetKey;
    std::string bucket = "misc";
    if (filename.rfind("map_image:", 0) == 0) {
        filename = filename.substr(10);
        bucket = "map";
    } else if (filename.rfind("map_meta:", 0) == 0) {
        filename = filename.substr(9);
        bucket = "map";
    } else if (filename.rfind("character_image:", 0) == 0) {
        filename = filename.substr(16);
        bucket = "character";
    }
    return CacheDirectory() / "assets" / bucket / (revision.empty() ? "unknown" : revision) / filename;
}

bool Game::SaveAssetToCache(const AssetBlob& asset) const {
    try {
        if (asset.kind == "image") {
            if (asset.encoding != "hex" || (asset.content.size() % 2) != 0) {
                return false;
            }
            std::filesystem::path outPath = ImageCachePathForAsset(asset.key, asset.revision);
            std::filesystem::create_directories(outPath.parent_path());
            std::ofstream out(outPath, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
            for (size_t i = 0; i < asset.content.size(); i += 2) {
                const int hi = HexNibble(asset.content[i]);
                const int lo = HexNibble(asset.content[i + 1]);
                if (hi < 0 || lo < 0) {
                    return false;
                }
                const char byte = static_cast<char>((hi << 4) | lo);
                out.write(&byte, 1);
            }
            return out.good();
        } else if (asset.kind == "meta") {
            std::filesystem::path outPath = ImageCachePathForAsset(asset.key, asset.revision);
            std::filesystem::create_directories(outPath.parent_path());
            std::ofstream out(outPath, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
            out.write(asset.content.data(), static_cast<std::streamsize>(asset.content.size()));
            return out.good();
        }

        std::filesystem::create_directories(CacheDirectory());
        std::ofstream out(CachePathForAsset(asset.key, asset.revision), std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        out.write(asset.content.data(), static_cast<std::streamsize>(asset.content.size()));
        return out.good();
    } catch (...) {
        return false;
    }
}

bool Game::LoadCachedAsset(const std::string& assetKey, const std::string& revision, std::string& outContent) const {
    try {
        std::ifstream in(CachePathForAsset(assetKey, revision), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        outContent = buffer.str();
        return true;
    } catch (...) {
        return false;
    }
}

bool Game::HasCachedImageAsset(const std::string& assetKey, const std::string& revision) const {
    try {
        return std::filesystem::exists(ImageCachePathForAsset(assetKey, revision));
    } catch (...) {
        return false;
    }
}

void Game::BeginMapBootstrap() {
    if (manifest_.mapId.empty()) {
        BeginRetryWait("Manifest has no map asset. Retrying in 10 seconds.");
        return;
    }
    BeginMapBootstrapForAsset("map:" + manifest_.mapId, "Manifest ready. Downloading map asset...");
}

void Game::BeginMapBootstrapForAsset(const std::string& assetKey, const std::string& statusText) {
    mapReady_ = false;
    pendingMapAssetKey_.clear();
    pendingMapAssetKey_ = assetKey;

    std::string cached;
    if (LoadCachedAsset(assetKey, manifest_.revision, cached)) {
        AssetBlob cachedBlob;
        cachedBlob.key = assetKey;
        cachedBlob.kind = "map";
        cachedBlob.revision = manifest_.revision;
        cachedBlob.content = cached;
        lastMapAssetSource_ = "cache";
        ApplyMapAsset(cachedBlob);
        AddChatLine("[System] Loaded map asset from cache");
        return;
    }

    if (!network_.RequestAsset(assetKey)) {
        BeginRetryWait("Map request failed. Retrying in 10 seconds.");
        return;
    }
    lastMapAssetSource_ = "server";
    loginStatus_ = statusText;
    AddChatLine("[System] Requesting map asset " + assetKey);
}

void Game::ApplyMapAsset(const AssetBlob& asset) {
    if (asset.kind != "map") {
        return;
    }
    const std::string revision = asset.revision.empty() ? "unknown" : asset.revision;
    world_.SetMapAssetRoot(CacheDirectory() / "assets" / "map" / revision);
    world_.SetCharacterAssetRoot(CacheDirectory() / "assets" / "character" / revision);
    if (!world_.LoadFromMapAsset(asset.content)) {
        BeginRetryWait("Map asset failed to load. Retrying in 10 seconds.");
        return;
    }

    ConfigureWorldDrivenGameplay();
    player_.position = hasPendingSpawnPosition_ ? pendingSpawnPosition_ : world_.spawnPoint();
    lastSentPosition_ = player_.position;
    camera_.target = player_.position;
    mapReady_ = true;
    pendingMapAssetKey_.clear();
    pendingMapId_ = world_.mapId();
    hasPendingSpawnPosition_ = false;
    lastAppliedMapAssetKey_ = asset.key;
    manifest_.mapId = world_.mapId();
    manifest_.worldName = world_.worldName();
    AddChatLine("[System] Map asset applied: " + asset.key);
    EnsureReferencedImagesRequested();
}

void Game::EnsureReferencedImagesRequested() {
    if (!network_.IsConnected()) {
        return;
    }

    for (const std::string& file : world_.referencedMapImageFiles()) {
        const std::string assetKey = "map_image:" + file;
        if (HasCachedImageAsset(assetKey, manifest_.revision)) {
        } else {
            network_.RequestAsset(assetKey);
        }
        const std::string metaKey = "map_meta:" + std::filesystem::path(file).stem().string() + ".atlas";
        if (!HasCachedImageAsset(metaKey, manifest_.revision)) {
            network_.RequestAsset(metaKey);
        }
    }
    for (const std::string& file : world_.referencedCharacterImageFiles()) {
        const std::string assetKey = "character_image:" + file;
        if (HasCachedImageAsset(assetKey, manifest_.revision)) {
            continue;
        }
        network_.RequestAsset(assetKey);
    }
}

void Game::ConfigureWorldDrivenGameplay() {
    guideNpc_.name.clear();
    guideNpc_.title.clear();
    guideNpc_.radius = 16.0f;
    guideNpc_.bodyColor = Color {120, 210, 255, 255};

    starterQuest_.targetRadius = 90.0f;
    starterQuest_.title.clear();
    starterQuest_.description.clear();

    for (const WorldObject& object : world_.objects()) {
        if (object.kind == "npc" && object.id == "luna") {
            guideNpc_.position = Vector2 {
                object.bounds.x + (object.bounds.width * 0.5f),
                object.bounds.y + (object.bounds.height * 0.5f)
            };
            auto titleIt = object.properties.find("title");
            if (titleIt != object.properties.end() && !titleIt->second.empty()) {
                guideNpc_.title = titleIt->second;
            }
            auto nameIt = object.properties.find("name");
            if (nameIt != object.properties.end() && !nameIt->second.empty()) {
                guideNpc_.name = nameIt->second;
            }
        }

        if (object.kind == "trigger" && object.id == "starter_road_marker") {
            starterQuest_.targetPosition = Vector2 {
                object.bounds.x + (object.bounds.width * 0.5f),
                object.bounds.y + (object.bounds.height * 0.5f)
            };
            starterQuest_.targetRadius = std::max(object.bounds.width, object.bounds.height) * 0.5f;
            auto questIt = object.properties.find("quest");
            if (questIt != object.properties.end() && !questIt->second.empty()) {
                starterQuest_.description = questIt->second;
            }
            auto titleIt = object.properties.find("title");
            if (titleIt != object.properties.end() && !titleIt->second.empty()) {
                starterQuest_.title = titleIt->second;
            }
            auto descIt = object.properties.find("description");
            if (descIt != object.properties.end() && !descIt->second.empty()) {
                starterQuest_.description = descIt->second;
            }
        }
    }
}

std::string Game::SpriteForAvatar(const Avatar& avatar, bool localPlayer) const {
    (void)localPlayer;
    const std::string facing = FacingForVelocity(avatar.velocity);
    const std::string key = "player_sprite_" + facing;
    return world_.property(key).value_or("");
}

std::string Game::NameForPlayer(int playerId) const {
    if (playerId == localPlayerId_ && localPlayerId_ != 0) {
        return player_.name;
    }

    auto it = remotePlayers_.find(playerId);
    if (it != remotePlayers_.end() && !it->second.avatar.name.empty()) {
        return it->second.avatar.name;
    }

    return "Player " + std::to_string(playerId);
}

void Game::UpdateChatInput() {
    if (IsKeyPressed(KEY_ENTER)) {
        if (!chatFocused_) {
            chatFocused_ = true;
            return;
        }

        if (!chatInput_.empty()) {
            if (!network_.SendChatMessage(chatInput_)) {
                AddChatLine("[System] Chat send failed");
            }
            chatInput_.clear();
        }
        chatFocused_ = false;
    }

    if (!chatFocused_) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        chatFocused_ = false;
        chatInput_.clear();
        return;
    }

    int pressed = GetCharPressed();
    while (pressed > 0) {
        if (pressed >= 32 && pressed <= 126 && chatInput_.size() < 90) {
            chatInput_.push_back(static_cast<char>(pressed));
        }
        pressed = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !chatInput_.empty()) {
        chatInput_.pop_back();
    }
}

void Game::UpdateChatScroll(float dt) {
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const Rectangle panel {18.0f, screenHeight - 214.0f, 430.0f, 194.0f};
    const Rectangle inputBox {panel.x + 14.0f, panel.y + panel.height - 46.0f, panel.width - 28.0f, 30.0f};
    const Rectangle messageArea {
        panel.x + 16.0f,
        panel.y + 50.0f,
        panel.width - 32.0f,
        inputBox.y - (panel.y + 50.0f) - 10.0f
    };

    const int rowHeight = 18;
    const int visibleRows = std::max(1, static_cast<int>(messageArea.height) / rowHeight);
    const float maxScroll = static_cast<float>(std::max(0, static_cast<int>(BuildChatRows(messageArea.width, 15).size()) - visibleRows));

    if (CheckCollisionPointRec(GetMousePosition(), messageArea)) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            chatScrollTarget_ -= wheel * 2.0f;
        }
    }

    if (IsKeyPressed(KEY_PAGE_UP)) {
        chatScrollTarget_ += 4.0f;
    }
    if (IsKeyPressed(KEY_PAGE_DOWN)) {
        chatScrollTarget_ -= 4.0f;
    }

    chatScrollTarget_ = std::clamp(chatScrollTarget_, 0.0f, maxScroll);
    const float smoothing = std::clamp(dt * 12.0f, 0.0f, 1.0f);
    chatScrollCurrent_ = LerpValue(chatScrollCurrent_, chatScrollTarget_, smoothing);

    for (ChatEntry& entry : chatEntries_) {
        entry.appearTime = std::min(1.0f, entry.appearTime + dt * 6.0f);
    }
}

void Game::UpdateNpcAndQuest() {
    if (chatFocused_) return;

    if (IsKeyPressed(KEY_E)) {
        const WorldObject* nearest = nullptr;
        float nearestDistanceSq = 92.0f * 92.0f;
        for (const WorldObject& object : world_.objects()) {
            if (object.kind != "npc" && object.kind != "portal") continue;
            const Vector2 center = world_.objectCenter(object);
            const float dx = player_.position.x - center.x;
            const float dy = player_.position.y - center.y;
            const float distSq = dx * dx + dy * dy;
            if (distSq <= nearestDistanceSq) {
                nearest = &object;
                nearestDistanceSq = distSq;
            }
        }
        if (nearest) {
            if (!network_.SendObjectInteract(nearest->id)) {
                AddChatLine("[System] Failed to send interaction to server.");
            }
        } else {
            AddChatLine("[System] Nothing to interact with nearby. Move closer and press E again.");
        }
    }
}

void Game::UpdateRemoteSmoothing(float dt) {
    const float amount = std::clamp(dt * 10.0f, 0.0f, 1.0f);
    for (auto& [playerId, remote] : remotePlayers_) {
        (void)playerId;
        remote.avatar.position.x = LerpValue(remote.avatar.position.x, remote.targetPosition.x, amount);
        remote.avatar.position.y = LerpValue(remote.avatar.position.y, remote.targetPosition.y, amount);
    }
}

void Game::ApplyRemotePlayerState(int playerId, float x, float y) {
    RemoteAvatar& remote = remotePlayers_[playerId];
    if (remote.avatar.name.empty()) {
        remote.avatar.name = "Player " + std::to_string(playerId);
    }
    remote.targetPosition = Vector2 {x, y};
    if (remote.avatar.radius <= 0.0f) {
        remote.avatar.radius = 14.0f;
    }
    if (remote.avatar.bodyColor.a == 0) {
        remote.avatar.bodyColor = Color {
            static_cast<unsigned char>(90 + ((playerId * 37) % 130)),
            static_cast<unsigned char>(110 + ((playerId * 53) % 120)),
            static_cast<unsigned char>(120 + ((playerId * 71) % 100)),
            255
        };
    }
    if (remote.avatar.position.x == 0.0f && remote.avatar.position.y == 0.0f) {
        remote.avatar.position = remote.targetPosition;
    }
}

void Game::ApplyPlayerName(int playerId, const std::string& name) {
    if (playerId == localPlayerId_) {
        player_.name = name;
        return;
    }

    RemoteAvatar& remote = remotePlayers_[playerId];
    remote.avatar.name = name;
    if (remote.avatar.radius <= 0.0f) {
        remote.avatar.radius = 14.0f;
    }
}

bool Game::IsNearPosition(Vector2 a, Vector2 b, float distance) const {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy) <= distance * distance;
}

void Game::Draw() const {
    ClearBackground(BackgroundSky());

    if (uiMode_ == UiMode::Boot || uiMode_ == UiMode::RetryWait) {
        DrawBootScreen();
        return;
    }

    if (uiMode_ == UiMode::MainMenu || uiMode_ == UiMode::LoggingIn) {
        DrawLoginScreen();
        return;
    }

    DrawScene();
    DrawHud();
}

void Game::DrawLoginScreen() const {
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    DrawRectangleGradientV(0, 0, screenWidth, screenHeight, Color {209, 236, 227, 255}, Color {138, 180, 160, 255});
    DrawCircle(screenWidth - 180, 140, 150.0f, Fade(WHITE, 0.18f));
    DrawCircle(120, screenHeight - 120, 180.0f, Fade(Color {54, 110, 77, 255}, 0.16f));

    const Rectangle card {
        static_cast<float>(screenWidth) * 0.5f - 260.0f,
        static_cast<float>(screenHeight) * 0.5f - 240.0f,
        520.0f,
        480.0f
    };

    DrawRectangleRounded(card, 0.08f, 10, LoginCardColor());
    DrawText("nganu.game", static_cast<int>(card.x + 28.0f), static_cast<int>(card.y + 28.0f), 34, RAYWHITE);
    DrawText("Main menu and spawn gate", static_cast<int>(card.x + 30.0f), static_cast<int>(card.y + 68.0f), 20, Fade(RAYWHITE, 0.78f));

    const Rectangle nameBox {card.x + 28.0f, card.y + 120.0f, 464.0f, 44.0f};
    const Rectangle hostBox {card.x + 28.0f, card.y + 192.0f, 340.0f, 44.0f};
    const Rectangle portBox {card.x + 386.0f, card.y + 192.0f, 106.0f, 44.0f};
    const Rectangle buttonBox {card.x + 28.0f, card.y + 270.0f, 464.0f, 48.0f};

    auto drawField = [&](const Rectangle& box, const char* label, const std::string& value, bool active) {
        DrawText(label, static_cast<int>(box.x), static_cast<int>(box.y - 22.0f), 18, Fade(RAYWHITE, 0.72f));
        DrawRectangleRounded(box, 0.18f, 8, active ? Fade(AccentColor(), 0.18f) : Fade(WHITE, 0.07f));
        DrawRectangleRoundedLinesEx(box, 0.18f, 8, 2.0f, active ? AccentColor() : Fade(RAYWHITE, 0.20f));
        const std::string content = active ? (value.empty() ? "_" : value + "_") : value;
        const std::string clipped = EllipsizeText(content, 22, box.width - 28.0f);
        DrawText(clipped.c_str(), static_cast<int>(box.x + 14.0f), static_cast<int>(box.y + 12.0f), 22, RAYWHITE);
    };

    drawField(nameBox, "Player Name", loginName_, loginField_ == LoginField::Name);
    drawField(hostBox, "Server Host", loginHost_, loginField_ == LoginField::Host);
    drawField(portBox, "Port", loginPort_, loginField_ == LoginField::Port);

    DrawRectangleRounded(buttonBox, 0.22f, 8, AccentColor());
    DrawText(uiMode_ == UiMode::LoggingIn ? "Spawning..." : "Enter World",
             static_cast<int>(buttonBox.x + 154.0f),
             static_cast<int>(buttonBox.y + 13.0f),
             24,
             Color {15, 31, 25, 255});

    const Rectangle manifestBox {card.x + 28.0f, card.y + 328.0f, 464.0f, 74.0f};
    DrawRectangleRounded(manifestBox, 0.15f, 8, Fade(WHITE, 0.05f));
    const std::string serverLabel = manifest_.valid
        ? ("Server: " + (manifest_.serverName.empty() ? std::string("Unknown") : manifest_.serverName))
        : "Server: waiting for update check";
    const std::string worldLabel = manifest_.valid
        ? ("World: " + (manifest_.worldName.empty() ? std::string("Unknown") : manifest_.worldName))
        : "World: unavailable";
    const std::string revisionLabel = manifest_.valid
        ? ("Revision: " + (manifest_.revision.empty() ? std::string("n/a") : manifest_.revision))
        : "Revision: unavailable";
    DrawText(EllipsizeText(serverLabel, 17, manifestBox.width - 22.0f).c_str(), static_cast<int>(manifestBox.x + 12.0f), static_cast<int>(manifestBox.y + 10.0f), 17, Fade(RAYWHITE, 0.86f));
    DrawText(EllipsizeText(worldLabel, 17, manifestBox.width - 22.0f).c_str(), static_cast<int>(manifestBox.x + 12.0f), static_cast<int>(manifestBox.y + 30.0f), 17, Fade(RAYWHITE, 0.74f));
    DrawText(EllipsizeText(revisionLabel, 17, manifestBox.width - 22.0f).c_str(), static_cast<int>(manifestBox.x + 12.0f), static_cast<int>(manifestBox.y + 50.0f), 17, Fade(RAYWHITE, 0.74f));

    DrawWrappedText(loginStatus_, Rectangle {card.x + 28.0f, card.y + 412.0f, 464.0f, 42.0f}, 18, Fade(RAYWHITE, 0.82f), 2);
    DrawText("Tab to switch fields, Enter to login, F5 to re-check update", static_cast<int>(card.x + 28.0f), static_cast<int>(card.y + 438.0f), 16, Fade(RAYWHITE, 0.60f));
}

void Game::DrawBootScreen() const {
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    DrawRectangleGradientV(0, 0, screenWidth, screenHeight, Color {205, 232, 236, 255}, Color {118, 153, 171, 255});
    DrawCircle(screenWidth - 220, 120, 140.0f, Fade(WHITE, 0.16f));
    DrawCircle(160, screenHeight - 140, 170.0f, Fade(Color {35, 62, 84, 255}, 0.14f));

    const Rectangle card {
        static_cast<float>(screenWidth) * 0.5f - 250.0f,
        static_cast<float>(screenHeight) * 0.5f - 150.0f,
        500.0f,
        300.0f
    };

    DrawRectangleRounded(card, 0.08f, 10, LoginCardColor());
    DrawText("nganu.game", static_cast<int>(card.x + 30.0f), static_cast<int>(card.y + 28.0f), 34, RAYWHITE);
    DrawText("Boot update check", static_cast<int>(card.x + 30.0f), static_cast<int>(card.y + 70.0f), 22, Fade(RAYWHITE, 0.78f));

    const Rectangle infoBox {card.x + 30.0f, card.y + 112.0f, card.width - 60.0f, 92.0f};
    DrawRectangleRounded(infoBox, 0.16f, 8, Fade(WHITE, 0.06f));
    DrawWrappedText(loginStatus_, infoBox, 22, RAYWHITE, 3);

    std::string footer = "Trying " + loginHost_ + ":" + loginPort_;
    if (uiMode_ == UiMode::RetryWait) {
        footer = "Retrying in " + std::to_string(static_cast<int>(std::ceil(retryCountdown_))) + " seconds";
    }
    DrawText(EllipsizeText(footer, 18, card.width - 60.0f).c_str(), static_cast<int>(card.x + 30.0f), static_cast<int>(card.y + 224.0f), 18, AccentColor());
    DrawText("Content comes from the server manifest before menu access", static_cast<int>(card.x + 30.0f), static_cast<int>(card.y + 254.0f), 16, Fade(RAYWHITE, 0.58f));
}

void Game::DrawScene() const {
    BeginMode2D(camera_);
    const float margin = 96.0f;
    const Vector2 topLeft = GetScreenToWorld2D(Vector2 {-margin, -margin}, camera_);
    const Vector2 bottomRight = GetScreenToWorld2D(Vector2 {static_cast<float>(GetScreenWidth()) + margin,
                                                            static_cast<float>(GetScreenHeight()) + margin}, camera_);
    const Rectangle visibleArea {
        std::min(topLeft.x, bottomRight.x),
        std::min(topLeft.y, bottomRight.y),
        std::fabs(bottomRight.x - topLeft.x),
        std::fabs(bottomRight.y - topLeft.y)
    };
    world_.DrawGround(visibleArea);
    world_.DrawDecorations(visibleArea);

    for (const WorldObject& object : world_.objects()) {
        if (!CheckCollisionRecs(object.bounds, visibleArea)) {
            continue;
        }
        if ((object.kind == "prop" || object.kind == "portal") && world_.objectZLayer(object) <= 0) {
            world_.DrawObjectSprite(object);
            if (object.kind == "portal") {
                const Vector2 center = world_.objectCenter(object);
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), std::max(object.bounds.width, object.bounds.height) * 0.5f + 6.0f, Fade(AccentColor(), 0.75f));
                DrawText(world_.objectProperty(object, "title").value_or("Portal").c_str(),
                         static_cast<int>(center.x - 54.0f),
                         static_cast<int>(center.y - 44.0f),
                         16,
                         Fade(RAYWHITE, 0.88f));
                if (IsNearPosition(player_.position, center, 88.0f)) {
                    DrawText("E Travel", static_cast<int>(center.x - 28.0f), static_cast<int>(center.y + 24.0f), 16, AccentColor());
                }
            }
        }
    }

    for (const auto& [playerId, remote] : remotePlayers_) {
        (void)playerId;
        if (!CheckCollisionPointRec(remote.avatar.position, visibleArea)) {
            continue;
        }
        DrawAvatar(remote.avatar, false);
    }

    if (!guideNpc_.name.empty() || world_.findObjectById("luna") != nullptr) {
        DrawNpc(guideNpc_);
    }
    DrawAvatar(player_, true);

    for (const WorldObject& object : world_.objects()) {
        if (!CheckCollisionRecs(object.bounds, visibleArea)) {
            continue;
        }
        if ((object.kind == "prop" || object.kind == "portal") && world_.objectZLayer(object) > 0) {
            world_.DrawObjectSprite(object);
            if (object.kind == "portal") {
                const Vector2 center = world_.objectCenter(object);
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), std::max(object.bounds.width, object.bounds.height) * 0.5f + 6.0f, Fade(AccentColor(), 0.75f));
                DrawText(world_.objectProperty(object, "title").value_or("Portal").c_str(),
                         static_cast<int>(center.x - 54.0f),
                         static_cast<int>(center.y - 44.0f),
                         16,
                         Fade(RAYWHITE, 0.88f));
                if (IsNearPosition(player_.position, center, 88.0f)) {
                    DrawText("E Travel", static_cast<int>(center.x - 28.0f), static_cast<int>(center.y + 24.0f), 16, AccentColor());
                }
            }
        }
    }
    EndMode2D();
}

void Game::DrawAvatar(const Avatar& avatar, bool localPlayer) const {
    DrawEllipse(
        static_cast<int>(avatar.position.x),
        static_cast<int>(avatar.position.y + avatar.radius + 8.0f),
        avatar.radius * 1.2f,
        avatar.radius * 0.65f,
        Fade(BLACK, 0.22f)
    );

    Rectangle spriteDest {
        avatar.position.x - 16.0f,
        avatar.position.y - 24.0f,
        32.0f,
        32.0f
    };
    const bool drewSprite = world_.DrawSpriteRef(SpriteForAvatar(avatar, localPlayer), spriteDest, Vector2 {}, 0.0f, WHITE);
    if (!drewSprite) {
        DrawCircleV(avatar.position, avatar.radius, avatar.bodyColor);
        DrawCircleV(Vector2 {avatar.position.x, avatar.position.y - 4.0f}, avatar.radius * 0.5f, Color {251, 235, 205, 255});
    }
    const Color outline = localPlayer ? AccentColor() : Fade(RAYWHITE, 0.75f);
    if (!drewSprite) {
        DrawCircleLinesV(avatar.position, avatar.radius, outline);
    }
    DrawText(avatar.name.c_str(), static_cast<int>(avatar.position.x - 28.0f), static_cast<int>(avatar.position.y - 34.0f), 16, RAYWHITE);
}

void Game::DrawNpc(const Npc& npc) const {
    const WorldObject* guideObject = world_.findObjectById("luna");
    const Vector2 drawPosition = guideObject ? world_.objectCenter(*guideObject) : npc.position;
    const std::string drawName = guideObject ? world_.objectProperty(*guideObject, "name").value_or(npc.name) : npc.name;
    const std::string drawTitle = guideObject ? world_.objectProperty(*guideObject, "title").value_or(npc.title) : npc.title;

    DrawEllipse(
        static_cast<int>(drawPosition.x),
        static_cast<int>(drawPosition.y + npc.radius + 8.0f),
        npc.radius * 1.2f,
        npc.radius * 0.65f,
        Fade(BLACK, 0.22f)
    );

    const bool drewSprite = guideObject ? world_.DrawObjectSprite(*guideObject) : false;
    if (!drewSprite) {
        DrawCircleV(drawPosition, npc.radius, npc.bodyColor);
        DrawCircleV(Vector2 {drawPosition.x, drawPosition.y - 4.0f}, npc.radius * 0.5f, Color {251, 235, 205, 255});
        DrawCircleLinesV(drawPosition, npc.radius, Color {255, 244, 171, 255});
    }

    DrawText(drawName.c_str(), static_cast<int>(drawPosition.x - 24.0f), static_cast<int>(drawPosition.y - 40.0f), 16, Color {255, 244, 171, 255});
    DrawText(drawTitle.c_str(), static_cast<int>(drawPosition.x - 36.0f), static_cast<int>(drawPosition.y - 58.0f), 14, Fade(RAYWHITE, 0.72f));
    if (IsNearPosition(player_.position, drawPosition, 72.0f)) {
        DrawText("E", static_cast<int>(drawPosition.x - 4.0f), static_cast<int>(drawPosition.y - 84.0f), 20, AccentColor());
    }
}

void Game::DrawHud() const {
    DrawTopBar();
    DrawChatPanel();
    DrawPartyPanel();
    DrawQuestPanel();

    if (showDebug_) {
        DrawDebugPanel();
    }
}

void Game::DrawTopBar() const {
    const int screenWidth = GetScreenWidth();

    DrawRectangle(0, 0, screenWidth, 54, Fade(BLACK, 0.22f));
    DrawRectangle(0, 0, screenWidth, 54, Fade(Color {13, 21, 24, 255}, 0.82f));
    DrawText("nganu.game", 24, 16, 24, RAYWHITE);
    DrawText("Prototype MMORPG 2D Top-Down", 190, 18, 18, Fade(RAYWHITE, 0.8f));

    const Rectangle statusPanel {static_cast<float>(screenWidth - 470), 10.0f, 452.0f, 34.0f};
    DrawRectangleRounded(statusPanel, 0.35f, 8, Fade(WHITE, 0.06f));
    const WorldObject* activeRegion = world_.regionAt(player_.position);
    const std::string region = activeRegion
        ? world_.objectProperty(*activeRegion, "title").value_or(activeRegion->id)
        : (world_.worldName().empty() ? "Overworld" : world_.worldName());
    const std::string mapId = world_.mapId().empty() ? "unknown" : world_.mapId();
    const std::string climate = activeRegion
        ? world_.objectProperty(*activeRegion, "climate").value_or(world_.property("climate").value_or("temperate"))
        : world_.property("climate").value_or("temperate");
    DrawText(EllipsizeText("Region: " + region, 18, 132.0f).c_str(), static_cast<int>(statusPanel.x + 18.0f), 18, 18, AccentColor());
    DrawText(EllipsizeText("Map: " + mapId, 16, 114.0f).c_str(), static_cast<int>(statusPanel.x + 152.0f), 19, 16, Fade(RAYWHITE, 0.70f));
    DrawText(EllipsizeText(climate, 16, 72.0f).c_str(), static_cast<int>(statusPanel.x + 272.0f), 19, 16, Fade(RAYWHITE, 0.62f));
    const std::string statusText = EllipsizeText(network_.StatusText(), 18, 96.0f);
    DrawText(statusText.c_str(), static_cast<int>(statusPanel.x + 352.0f), 18, 18, Fade(RAYWHITE, 0.8f));
}

void Game::DrawChatPanel() const {
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const Rectangle panel {18.0f, screenHeight - 214.0f, 430.0f, 194.0f};
    DrawRectangleRounded(panel, 0.08f, 8, PanelColor());
    DrawText("World Chat", static_cast<int>(panel.x + 18.0f), static_cast<int>(panel.y + 16.0f), 20, RAYWHITE);

    const Rectangle inputBox {panel.x + 14.0f, panel.y + panel.height - 46.0f, panel.width - 28.0f, 30.0f};
    const Rectangle messageArea {
        panel.x + 16.0f,
        panel.y + 50.0f,
        panel.width - 32.0f,
        inputBox.y - (panel.y + 50.0f) - 10.0f
    };
    const int fontSize = 15;
    const int rowHeight = 18;
    const int visibleRows = std::max(1, static_cast<int>(messageArea.height) / rowHeight);
    const std::vector<ChatRow> rows = BuildChatRows(messageArea.width, fontSize);
    const int totalLines = static_cast<int>(rows.size());
    const float maxScroll = static_cast<float>(std::max(0, totalLines - visibleRows));
    const float scrollOffset = std::clamp(chatScrollCurrent_, 0.0f, maxScroll);
    const float contentHeight = static_cast<float>(totalLines * rowHeight);
    const float baseY = messageArea.y + messageArea.height - contentHeight + (scrollOffset * rowHeight);

    BeginScissorMode(static_cast<int>(messageArea.x), static_cast<int>(messageArea.y), static_cast<int>(messageArea.width), static_cast<int>(messageArea.height));
    for (size_t i = 0; i < rows.size(); ++i) {
        float lineY = baseY + static_cast<float>(i * rowHeight);
        const ChatEntry& entry = chatEntries_[rows[i].entryIndex];
        const float appear = std::clamp(entry.appearTime, 0.0f, 1.0f);
        if (scrollOffset < 0.5f && rows[i].entryIndex + 1 == chatEntries_.size()) {
            lineY += (1.0f - appear) * 14.0f;
        }

        if (lineY + rowHeight < messageArea.y || lineY > messageArea.y + messageArea.height) {
            continue;
        }

        Color color = Fade(RAYWHITE, 0.82f);
        if (scrollOffset < 0.5f && rows[i].entryIndex + 1 == chatEntries_.size()) {
            color.a = static_cast<unsigned char>(color.a * (0.55f + appear * 0.45f));
        }

        DrawText(rows[i].text.c_str(), static_cast<int>(messageArea.x), static_cast<int>(lineY), fontSize, color);
    }
    EndScissorMode();

    if (totalLines > visibleRows) {
        const float trackHeight = messageArea.height;
        const float thumbMinHeight = 18.0f;
        const float thumbHeight = std::max(thumbMinHeight, trackHeight * (static_cast<float>(visibleRows) / static_cast<float>(totalLines)));
        const float maxThumbTravel = std::max(0.0f, trackHeight - thumbHeight);
        const float scrollRatio = (maxScroll > 0.0f) ? (scrollOffset / maxScroll) : 0.0f;
        const Rectangle scrollTrack {messageArea.x + messageArea.width - 6.0f, messageArea.y, 4.0f, trackHeight};
        const Rectangle scrollThumb {scrollTrack.x, scrollTrack.y + maxThumbTravel * scrollRatio, 4.0f, thumbHeight};
        DrawRectangleRounded(scrollTrack, 0.8f, 6, Fade(WHITE, 0.10f));
        DrawRectangleRounded(scrollThumb, 0.8f, 6, Fade(AccentColor(), 0.85f));
        DrawText(scrollOffset > 0.5f ? "Scroll: history" : "Scroll: latest",
                 static_cast<int>(panel.x + panel.width - 116.0f),
                 static_cast<int>(panel.y + 18.0f),
                 12,
                 Fade(RAYWHITE, 0.5f));
    }

    DrawRectangleRounded(inputBox, 0.25f, 8, Fade(WHITE, 0.07f));
    const std::string prompt = chatFocused_ ? (chatInput_.empty() ? "_" : chatInput_ + "_") : "Press Enter to open chat";
    const std::string promptText = EllipsizeText(prompt, 15, inputBox.width - 24.0f);
    DrawText(promptText.c_str(), static_cast<int>(inputBox.x + 12.0f), static_cast<int>(inputBox.y + 7.0f), 15, chatFocused_ ? AccentColor() : Fade(RAYWHITE, 0.55f));
}

void Game::DrawPartyPanel() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const Rectangle panel {screenWidth - 242.0f, 82.0f, 222.0f, 176.0f};
    DrawRectangleRounded(panel, 0.1f, 8, PanelColor());
    DrawText("Online Players", static_cast<int>(panel.x + 22.0f), static_cast<int>(panel.y + 20.0f), 20, RAYWHITE);
    const std::string localName = EllipsizeText(player_.name, 18, panel.width - 44.0f);
    DrawText(localName.c_str(), static_cast<int>(panel.x + 22.0f), static_cast<int>(panel.y + 54.0f), 18, AccentColor());

    int lineY = static_cast<int>(panel.y + 80.0f);
    int shown = 0;
    for (const auto& [playerId, remote] : remotePlayers_) {
        if (shown >= 3) break;
        const std::string remoteName = EllipsizeText(remote.avatar.name, 18, panel.width - 44.0f);
        DrawText(remoteName.c_str(), static_cast<int>(panel.x + 22.0f), lineY, 18, Fade(RAYWHITE, 0.85f));
        lineY += 26;
        ++shown;
    }
}

void Game::DrawQuestPanel() const {
    const Rectangle panel {18.0f, 76.0f, 320.0f, 132.0f};
    DrawRectangleRounded(panel, 0.1f, 8, PanelColor());
    DrawText("Objective", 36, 96, 20, RAYWHITE);
    DrawText("Server Objective", 36, 126, 18, AccentColor());
    DrawWrappedText(currentObjective_.empty() ? "No active objective." : currentObjective_,
                    Rectangle {36.0f, 150.0f, 284.0f, 48.0f},
                    17,
                    Fade(RAYWHITE, 0.78f),
                    3);
}

void Game::DrawDebugPanel() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const Rectangle panel {screenWidth - 368.0f, screenHeight - 240.0f, 348.0f, 220.0f};
    DrawRectangleRounded(panel, 0.08f, 8, PanelColor());
    DrawText("Debug", static_cast<int>(panel.x + 20.0f), static_cast<int>(panel.y + 18.0f), 20, RAYWHITE);

    char line[192];
    std::snprintf(line, sizeof(line), "Id: %d  Remote: %zu", localPlayerId_, remotePlayers_.size());
    DrawText(line, static_cast<int>(panel.x + 20.0f), static_cast<int>(panel.y + 50.0f), 18, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Player: %.1f, %.1f", player_.position.x, player_.position.y);
    DrawText(line, static_cast<int>(panel.x + 20.0f), static_cast<int>(panel.y + 74.0f), 18, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Map: %s  Layers: %zu", world_.mapId().c_str(), world_.layers().size());
    DrawText(line, static_cast<int>(panel.x + 20.0f), static_cast<int>(panel.y + 98.0f), 18, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Objects: %zu  Music: %s", world_.objects().size(), world_.property("music").value_or("n/a").c_str());
    DrawText(line, static_cast<int>(panel.x + 20.0f), static_cast<int>(panel.y + 122.0f), 18, Fade(RAYWHITE, 0.82f));
    DrawText(EllipsizeText(("Revision: " + (manifest_.revision.empty() ? std::string("n/a") : manifest_.revision)), 18, panel.width - 40.0f).c_str(),
             static_cast<int>(panel.x + 20.0f),
             static_cast<int>(panel.y + 146.0f),
             18,
             Fade(RAYWHITE, 0.82f));
    DrawText(EllipsizeText(("Asset Source: " + lastMapAssetSource_), 18, panel.width - 40.0f).c_str(),
             static_cast<int>(panel.x + 20.0f),
             static_cast<int>(panel.y + 170.0f),
             18,
             Fade(RAYWHITE, 0.82f));
    if (!pendingMapId_.empty() && pendingMapId_ != world_.mapId()) {
        DrawText(EllipsizeText(("Pending Map: " + pendingMapId_), 18, panel.width - 40.0f).c_str(),
                 static_cast<int>(panel.x + 20.0f),
                 static_cast<int>(panel.y + 194.0f),
                 18,
                 AccentColor());
    } else {
        DrawText(EllipsizeText(("Applied Asset: " + (lastAppliedMapAssetKey_.empty() ? std::string("n/a") : lastAppliedMapAssetKey_)), 18, panel.width - 40.0f).c_str(),
                 static_cast<int>(panel.x + 20.0f),
                 static_cast<int>(panel.y + 194.0f),
                 18,
                 AccentColor());
    }
}

void Game::DrawWrappedText(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines) const {
    if (text.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return;
    }

    const int lineHeight = fontSize + 4;
    const int allowedLines = (maxLines > 0) ? maxLines : std::max(1, static_cast<int>(bounds.height) / lineHeight);
    std::istringstream stream(text);
    std::string word;
    std::string line;
    std::vector<std::string> lines;

    auto pushLine = [&](std::string value) {
        if (static_cast<int>(lines.size()) >= allowedLines) {
            return;
        }
        if (static_cast<int>(lines.size()) == allowedLines - 1) {
            value = EllipsizeText(value, fontSize, bounds.width);
        }
        lines.push_back(std::move(value));
    };

    while (stream >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (MeasureText(candidate.c_str(), fontSize) <= static_cast<int>(bounds.width)) {
            line = candidate;
            continue;
        }

        if (!line.empty()) {
            pushLine(line);
            if (static_cast<int>(lines.size()) >= allowedLines) {
                break;
            }
            line = word;
        } else {
            pushLine(EllipsizeText(word, fontSize, bounds.width));
            line.clear();
            if (static_cast<int>(lines.size()) >= allowedLines) {
                break;
            }
        }
    }

    if (!line.empty() && static_cast<int>(lines.size()) < allowedLines) {
        pushLine(line);
    }

    BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    for (size_t i = 0; i < lines.size(); ++i) {
        DrawText(lines[i].c_str(), static_cast<int>(bounds.x), static_cast<int>(bounds.y) + static_cast<int>(i) * lineHeight, fontSize, color);
    }
    EndScissorMode();
}

std::string Game::EllipsizeText(const std::string& text, int fontSize, float maxWidth) const {
    if (text.empty() || maxWidth <= 0.0f) {
        return "";
    }

    if (MeasureText(text.c_str(), fontSize) <= static_cast<int>(maxWidth)) {
        return text;
    }

    const std::string ellipsis = "...";
    std::string clipped = text;
    while (!clipped.empty() &&
           MeasureText((clipped + ellipsis).c_str(), fontSize) > static_cast<int>(maxWidth)) {
        clipped.pop_back();
    }

    return clipped.empty() ? ellipsis : clipped + ellipsis;
}

std::vector<ChatRow> Game::BuildChatRows(float maxWidth, int fontSize) const {
    std::vector<ChatRow> rows;
    const std::string continuationPrefix = "  ";

    for (size_t entryIndex = 0; entryIndex < chatEntries_.size(); ++entryIndex) {
        const ChatEntry& entry = chatEntries_[entryIndex];
        const std::string firstPrefix;
        std::istringstream stream(entry.text);
        std::string word;
        std::string current = firstPrefix;
        std::string secondLine = continuationPrefix;
        bool usingSecondLine = false;

        while (stream >> word) {
            std::string& target = usingSecondLine ? secondLine : current;
            const std::string candidate = (target == firstPrefix || target == continuationPrefix) ? target + word : target + " " + word;
            if (MeasureText(candidate.c_str(), fontSize) <= static_cast<int>(maxWidth)) {
                target = candidate;
                continue;
            }

            if (!usingSecondLine) {
                usingSecondLine = true;
                const std::string secondCandidate = secondLine + word;
                if (MeasureText(secondCandidate.c_str(), fontSize) <= static_cast<int>(maxWidth)) {
                    secondLine = secondCandidate;
                } else {
                    secondLine = EllipsizeText(secondCandidate, fontSize, maxWidth);
                    break;
                }
            } else {
                secondLine = EllipsizeText(secondLine + " " + word, fontSize, maxWidth);
                break;
            }
        }

        rows.push_back(ChatRow {EllipsizeText(current, fontSize, maxWidth), entryIndex, true});
        if (usingSecondLine && secondLine != continuationPrefix) {
            rows.push_back(ChatRow {EllipsizeText(secondLine, fontSize, maxWidth), entryIndex, false});
        }
    }

    return rows;
}
