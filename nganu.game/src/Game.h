#pragma once

#include "NetworkClient.h"
#include "World.h"
#include "raylib.h"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Avatar {
    std::string name;
    Vector2 position {};
    Vector2 velocity {};
    Color bodyColor {};
    float radius = 14.0f;
};

struct RemoteAvatar {
    Avatar avatar;
    Vector2 targetPosition {};
};

struct Npc {
    std::string name;
    std::string title;
    Vector2 position {};
    float radius = 16.0f;
    Color bodyColor {};
};

struct QuestState {
    bool offered = false;
    bool accepted = false;
    bool completed = false;
    bool turnedIn = false;
    Vector2 targetPosition {};
    float targetRadius = 80.0f;
    std::string title;
    std::string description;
};

struct ChatEntry {
    std::string text;
    float appearTime = 0.0f;
};

struct ChatRow {
    std::string text;
    size_t entryIndex = 0;
    bool firstLine = false;
};

struct ContentManifest {
    bool valid = false;
    std::string serverName;
    std::string revision;
    std::string worldName;
    std::string mapId;
    std::vector<std::string> assets;
};

struct AssetBlob {
    std::string key;
    std::string kind;
    std::string revision;
    std::string encoding;
    std::string content;
};

class Game {
public:
    Game();

    void Update(float dt);
    void Draw() const;

private:
    enum class UiMode {
        Boot,
        RetryWait,
        MainMenu,
        LoggingIn,
        World
    };

    enum class LoginField {
        Name,
        Host,
        Port
    };

    World world_;
    NetworkClient network_;
    Avatar player_;
    std::unordered_map<int, RemoteAvatar> remotePlayers_;
    std::vector<ChatEntry> chatEntries_;
    Npc guideNpc_;
    QuestState starterQuest_;
    UiMode uiMode_ = UiMode::Boot;
    LoginField loginField_ = LoginField::Name;
    Camera2D camera_ {};
    bool showDebug_ = false;
    float worldTime_ = 0.0f;
    float sendAccumulator_ = 0.0f;
    Vector2 lastSentPosition_ {};
    int localPlayerId_ = 0;
    std::string chatInput_;
    bool chatFocused_ = false;
    float chatScrollTarget_ = 0.0f;
    float chatScrollCurrent_ = 0.0f;
    std::string loginName_;
    std::string loginHost_;
    std::string loginPort_;
    std::string loginStatus_;
    std::string currentObjective_;
    ContentManifest manifest_;
    float stateTimer_ = 0.0f;
    float retryCountdown_ = 0.0f;
    float manifestWait_ = 0.0f;
    bool bootStarted_ = false;
    bool bootstrapRequested_ = false;
    bool handshakeReady_ = false;
    bool mapReady_ = false;
    std::string pendingMapAssetKey_;
    std::string pendingMapId_;
    Vector2 pendingSpawnPosition_ {};
    bool hasPendingSpawnPosition_ = false;
    std::string lastMapAssetSource_ = "none";
    std::string lastAppliedMapAssetKey_;

    void BeginBootUpdateCheck();
    void BeginRetryWait(const std::string& reason);
    void UpdateLoginInput();
    void UpdateRetryWait(float dt);
    void UpdateLoggingIn(float dt);
    void StartConnection();
    void StartLogin();
    void UpdatePlayer(float dt);
    void UpdateCamera(float dt);
    void UpdateNetwork(float dt);
    void HandleNetworkEvent(const NetworkEvent& event);
    void AddChatLine(const std::string& line);
    void ApplyManifest(const std::string& rawManifest);
    std::optional<AssetBlob> ParseAssetBlob(const std::string& rawBlob) const;
    std::filesystem::path CacheDirectory() const;
    std::filesystem::path CachePathForAsset(const std::string& assetKey, const std::string& revision) const;
    std::filesystem::path ImageCachePathForAsset(const std::string& assetKey, const std::string& revision) const;
    bool SaveAssetToCache(const AssetBlob& asset) const;
    bool LoadCachedAsset(const std::string& assetKey, const std::string& revision, std::string& outContent) const;
    bool HasCachedImageAsset(const std::string& assetKey, const std::string& revision) const;
    void EnsureReferencedImagesRequested();
    void BeginMapBootstrap();
    void BeginMapBootstrapForAsset(const std::string& assetKey, const std::string& statusText);
    void ApplyMapAsset(const AssetBlob& asset);
    void ConfigureWorldDrivenGameplay();
    std::string NameForPlayer(int playerId) const;
    std::string SpriteForAvatar(const Avatar& avatar, bool localPlayer) const;
    void UpdateChatInput();
    void UpdateChatScroll(float dt);
    void UpdateNpcAndQuest();
    void UpdateRemoteSmoothing(float dt);
    void ApplyRemotePlayerState(int playerId, float x, float y);
    void ApplyPlayerName(int playerId, const std::string& name);
    bool IsNearPosition(Vector2 a, Vector2 b, float distance) const;

    void DrawScene() const;
    void DrawAvatar(const Avatar& avatar, bool localPlayer) const;
    void DrawNpc(const Npc& npc) const;
    void DrawHud() const;
    void DrawLoginScreen() const;
    void DrawBootScreen() const;
    void DrawTopBar() const;
    void DrawChatPanel() const;
    void DrawPartyPanel() const;
    void DrawQuestPanel() const;
    void DrawDebugPanel() const;
    void DrawWrappedText(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 0) const;
    std::string EllipsizeText(const std::string& text, int fontSize, float maxWidth) const;
    std::vector<ChatRow> BuildChatRows(float maxWidth, int fontSize) const;
};
