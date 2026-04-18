#include "Game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>

#if defined(PLATFORM_ANDROID)
#include "raymob.h"
#endif

namespace {
Color BackgroundSky() { return Color {197, 226, 233, 255}; }
Color BackgroundHorizon() { return Color {168, 208, 214, 255}; }
Color BackgroundDeep() { return Color {105, 138, 155, 255}; }
Color LocalColor() { return Color {255, 205, 96, 255}; }
Color PanelColor() { return Color {20, 29, 34, 215}; }
Color AccentColor() { return Color {111, 219, 159, 255}; }
Color LoginCardColor() { return Color {18, 28, 33, 228}; }
Color OutlineColor() { return Color {231, 241, 236, 46}; }
Color SoftCardColor() { return Color {24, 37, 42, 204}; }
Font g_uiFont {};
bool g_ownsUiFont = false;

#if defined(PLATFORM_ANDROID)
bool g_androidSoftKeyboardVisible = false;
#endif

struct HudLayout {
    Rectangle safeFrame {};
    float topBarHeight = 56.0f;
    float margin = 16.0f;
    float gap = 12.0f;
    Rectangle contentFrame {};
    Rectangle questPanel {};
    Rectangle partyPanel {};
    Rectangle chatPanel {};
    Rectangle debugPanel {};
    bool compact = false;
    bool singleColumn = false;
    bool shortScreen = false;
    int titleFont = 20;
    int bodyFont = 18;
    int smallFont = 15;
    int chatFont = 15;
};

float UiScaleForScreen(float width, float height) {
    const float widthScale = width / 1280.0f;
    const float heightScale = height / 720.0f;
    return std::clamp(std::min(widthScale, heightScale), 0.62f, 1.20f);
}

Rectangle SafeFrameForScreen(float screenWidth, float screenHeight, float widthCapFactor, float widthCapMin, float horizontalFill = 0.92f) {
    const float aspect = (screenHeight > 0.0f) ? (screenWidth / screenHeight) : (16.0f / 9.0f);
    const float sideInset = std::clamp(screenWidth * (aspect >= 2.4f ? 0.042f : aspect >= 2.0f ? 0.032f : 0.022f), 12.0f, 72.0f);
    const float verticalInset = std::clamp(screenHeight * 0.024f, 12.0f, 28.0f);
    const float availableWidth = std::max(320.0f, screenWidth - sideInset * 2.0f);
    const float cappedWidth = std::max(widthCapMin, std::min(availableWidth, screenHeight * widthCapFactor));
    const float width = std::min(availableWidth, std::max(availableWidth * horizontalFill, cappedWidth));

    return Rectangle {
        std::round((screenWidth - width) * 0.5f),
        verticalInset,
        std::round(width),
        std::max(240.0f, screenHeight - verticalInset * 2.0f)
    };
}

float FitPanelHeight(float available, float minimum, float preferred) {
    if (available <= 0.0f) {
        return 0.0f;
    }
    if (available < minimum) {
        return std::max(44.0f, available);
    }
    return std::min(preferred, available);
}

Rectangle GameplayViewport(float screenWidth, float screenHeight) {
    return Rectangle {0.0f, 0.0f, screenWidth, screenHeight};
}

Rectangle CenteredCardInFrame(const Rectangle& frame, float preferredWidth, float maxWidth, float preferredHeight, float minHeight);
Vector2 NormalizeOrZero(Vector2 value);
int MeasureUiText(const std::string& text, int fontSize);
void DrawUiText(const char* text, float x, float y, int fontSize, Color color);

struct LoginHitRects {
    Rectangle nameBox {};
    Rectangle hostBox {};
    Rectangle portBox {};
    Rectangle buttonBox {};
};

LoginHitRects ComputeLoginHitRects(float screenWidth, float screenHeight) {
    const Rectangle safeFrame = SafeFrameForScreen(screenWidth, screenHeight, 1.24f, 520.0f, 0.88f);
    const bool compact = safeFrame.width < 760.0f;
    const bool tiny = safeFrame.width < 620.0f || safeFrame.height < 560.0f;
    const float cardPadding = tiny ? 18.0f : 28.0f;
    const float fieldHeight = tiny ? 40.0f : 44.0f;
    const float cardWidth = std::min(tiny ? 520.0f : 620.0f, safeFrame.width);
    const float cardHeight = std::min(tiny ? 470.0f : (compact ? 540.0f : 520.0f), safeFrame.height);
    const Rectangle card = CenteredCardInFrame(safeFrame, cardWidth, 620.0f, cardHeight, tiny ? 420.0f : 460.0f);
    const float innerX = card.x + cardPadding;
    const float innerWidth = card.width - cardPadding * 2.0f;
    const float hostWidth = (compact || tiny) ? innerWidth : innerWidth - 120.0f;

    LoginHitRects rects;
    rects.nameBox = Rectangle {innerX, card.y + cardPadding + 104.0f, innerWidth, fieldHeight};
    rects.hostBox = Rectangle {innerX, rects.nameBox.y + fieldHeight + (tiny ? 30.0f : 28.0f), hostWidth, fieldHeight};
    rects.portBox = Rectangle {
        compact || tiny ? innerX : rects.hostBox.x + rects.hostBox.width + 14.0f,
        compact || tiny ? rects.hostBox.y + fieldHeight + 28.0f : rects.hostBox.y,
        compact || tiny ? innerWidth : 106.0f,
        fieldHeight
    };
    rects.buttonBox = Rectangle {innerX, rects.portBox.y + fieldHeight + (tiny ? 24.0f : 34.0f), innerWidth, tiny ? 46.0f : 50.0f};
    return rects;
}

#if defined(PLATFORM_ANDROID)
void ShowAndroidKeyboard() {
    if (!g_androidSoftKeyboardVisible) {
        ShowSoftKeyboard();
        g_androidSoftKeyboardVisible = true;
    }
}

void HideAndroidKeyboard() {
    if (g_androidSoftKeyboardVisible) {
        HideSoftKeyboard();
        g_androidSoftKeyboardVisible = false;
    }
}

bool AndroidConsumeSoftChar(std::string& target, size_t maxLen, bool numericOnly) {
    const int code = GetLastSoftKeyCode();
    const int unicode = GetLastSoftKeyUnicode();
    const char ch = GetLastSoftKeyChar();
    if (code == 0 && unicode == 0 && ch == '\0') {
        return false;
    }

    bool changed = false;
    if ((code == KEY_BACKSPACE || code == 67) && !target.empty()) {
        target.pop_back();
        changed = true;
    } else {
        const int value = unicode != 0 ? unicode : static_cast<unsigned char>(ch);
        if (value >= 32 && value <= 126 && target.size() < maxLen) {
            if (!numericOnly || std::isdigit(value)) {
                target.push_back(static_cast<char>(value));
                changed = true;
            }
        }
    }

    ClearLastSoftKey();
    return changed;
}

bool AndroidPointPressedIn(Rectangle rect, bool& wasDown) {
    bool down = false;
    for (int i = 0; i < GetTouchPointCount(); ++i) {
        if (CheckCollisionPointRec(GetTouchPosition(i), rect)) {
            down = true;
            break;
        }
    }
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

Rectangle AndroidJoystickBounds() {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float radius = std::clamp(std::min(screenWidth, screenHeight) * 0.115f, 58.0f, 92.0f);
    return Rectangle {screenWidth * 0.055f, screenHeight - (radius * 2.25f), radius * 2.0f, radius * 2.0f};
}

Vector2 AndroidMovementInput() {
    const Rectangle joy = AndroidJoystickBounds();
    const Vector2 center {joy.x + joy.width * 0.5f, joy.y + joy.height * 0.5f};
    const float radius = joy.width * 0.5f;
    for (int i = 0; i < GetTouchPointCount(); ++i) {
        const Vector2 touch = GetTouchPosition(i);
        const float dx = touch.x - center.x;
        const float dy = touch.y - center.y;
        const float distanceSq = dx * dx + dy * dy;
        const float captureRadius = radius * 1.45f;
        if (distanceSq <= captureRadius * captureRadius) {
            return NormalizeOrZero(Vector2 {dx / radius, dy / radius});
        }
    }
    return Vector2 {};
}

Rectangle AndroidActionButtonRect(float indexFromRight) {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float size = std::clamp(std::min(screenWidth, screenHeight) * 0.105f, 52.0f, 82.0f);
    const float gap = size * 0.22f;
    return Rectangle {
        screenWidth - ((indexFromRight + 1.0f) * size) - (indexFromRight + 1.0f) * gap,
        screenHeight - size - gap,
        size,
        size
    };
}

bool AndroidInteractPressed() {
    static bool wasDown = false;
    return AndroidPointPressedIn(AndroidActionButtonRect(0.0f), wasDown);
}

bool AndroidInventoryPressed() {
    static bool wasDown = false;
    return AndroidPointPressedIn(AndroidActionButtonRect(1.0f), wasDown);
}

bool AndroidChatPressed() {
    static bool wasDown = false;
    return AndroidPointPressedIn(AndroidActionButtonRect(2.0f), wasDown);
}

void DrawAndroidTouchControls(bool chatFocused) {
    const Rectangle joy = AndroidJoystickBounds();
    const Vector2 center {joy.x + joy.width * 0.5f, joy.y + joy.height * 0.5f};
    const float radius = joy.width * 0.5f;
    DrawCircleV(center, radius, Fade(Color {8, 18, 20, 255}, 0.34f));
    DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), radius, Fade(RAYWHITE, 0.34f));

    Vector2 knob = AndroidMovementInput();
    knob = Vector2 {center.x + knob.x * radius * 0.55f, center.y + knob.y * radius * 0.55f};
    DrawCircleV(knob, radius * 0.34f, Fade(RAYWHITE, 0.30f));

    auto drawButton = [](Rectangle rect, const char* label, Color color) {
        DrawCircle(static_cast<int>(rect.x + rect.width * 0.5f),
                   static_cast<int>(rect.y + rect.height * 0.5f),
                   rect.width * 0.5f,
                   Fade(color, 0.62f));
        DrawCircleLines(static_cast<int>(rect.x + rect.width * 0.5f),
                        static_cast<int>(rect.y + rect.height * 0.5f),
                        rect.width * 0.5f,
                        Fade(RAYWHITE, 0.42f));
        const int fontSize = static_cast<int>(std::clamp(rect.width * 0.28f, 14.0f, 22.0f));
        DrawUiText(label,
                   rect.x + (rect.width - static_cast<float>(MeasureUiText(label, fontSize))) * 0.5f,
                   rect.y + rect.height * 0.5f - fontSize * 0.5f,
                   fontSize,
                   RAYWHITE);
    };

    drawButton(AndroidActionButtonRect(0.0f), "E", AccentColor());
    drawButton(AndroidActionButtonRect(1.0f), "Bag", Color {84, 132, 216, 255});
    drawButton(AndroidActionButtonRect(2.0f), chatFocused ? "Send" : "Chat", Color {214, 154, 76, 255});
}
#else
void HideAndroidKeyboard() {}
#endif

HudLayout ComputeHudLayout(float screenWidth, float screenHeight, bool includeDebug) {
    HudLayout layout;
    const float scale = UiScaleForScreen(screenWidth, screenHeight);
    const Rectangle viewport = GameplayViewport(screenWidth, screenHeight);
    const Rectangle safeFrame = SafeFrameForScreen(viewport.width, viewport.height, 1.76f, 760.0f, 0.90f);

    layout.safeFrame = safeFrame;
    layout.compact = safeFrame.width < 1080.0f || safeFrame.height < 720.0f;
    layout.singleColumn = safeFrame.width < 860.0f || safeFrame.height < 570.0f;
    layout.shortScreen = safeFrame.height < 640.0f;
    const bool tiny = safeFrame.width < 700.0f || safeFrame.height < 520.0f;
    layout.margin = std::clamp(18.0f * scale, 10.0f, 22.0f);
    layout.gap = tiny ? 8.0f : std::clamp(12.0f * scale, 8.0f, 14.0f);
    layout.topBarHeight = layout.singleColumn ? (tiny ? 84.0f : 88.0f) : (layout.compact ? (layout.shortScreen ? 64.0f : 70.0f) : 56.0f);
    layout.titleFont = tiny ? 18 : (layout.compact ? 19 : 20);
    layout.bodyFont = tiny ? 16 : 18;
    layout.smallFont = tiny ? 13 : (layout.compact ? 14 : 15);
    layout.chatFont = tiny ? 13 : 15;

    layout.contentFrame = Rectangle {
        safeFrame.x,
        safeFrame.y + layout.topBarHeight + layout.margin,
        safeFrame.width,
        std::max(0.0f, safeFrame.height - layout.topBarHeight - layout.margin)
    };
    const float contentTop = layout.contentFrame.y;
    const float contentBottom = layout.contentFrame.y + layout.contentFrame.height;
    const float contentLeft = layout.contentFrame.x;
    const float contentRight = layout.contentFrame.x + layout.contentFrame.width;
    const float contentHeight = layout.contentFrame.height;

    if (layout.singleColumn) {
        const float panelWidth = layout.contentFrame.width;
        const float availableHeight = std::max(160.0f, contentHeight);
        const float questHeight = FitPanelHeight(availableHeight * 0.21f, 82.0f, layout.shortScreen ? 92.0f : 110.0f);
        const float partyHeight = FitPanelHeight((availableHeight - questHeight - layout.gap) * 0.19f, 70.0f, layout.shortScreen ? 84.0f : 96.0f);
        float remainingHeight = contentBottom - (contentTop + questHeight + layout.gap + partyHeight + layout.gap);
        float debugHeight = 0.0f;
        if (includeDebug) {
            debugHeight = FitPanelHeight(remainingHeight * 0.26f, 90.0f, layout.shortScreen ? 102.0f : 120.0f);
            remainingHeight -= debugHeight + layout.gap;
        }
        const float chatHeight = FitPanelHeight(remainingHeight, 108.0f, layout.shortScreen ? 160.0f : 210.0f);
        const float chatY = contentBottom - chatHeight;
        const float debugY = includeDebug ? (chatY - layout.gap - debugHeight) : chatY;

        layout.questPanel = Rectangle {contentLeft, contentTop, panelWidth, questHeight};
        layout.partyPanel = Rectangle {contentLeft, layout.questPanel.y + layout.questPanel.height + layout.gap, panelWidth, partyHeight};
        layout.chatPanel = Rectangle {contentLeft, chatY, panelWidth, chatHeight};
        layout.debugPanel = includeDebug
            ? Rectangle {contentLeft, debugY, panelWidth, debugHeight}
            : Rectangle {};
        return layout;
    }

    const float partyWidth = std::clamp(layout.contentFrame.width * (layout.compact ? 0.25f : 0.22f), 220.0f, 320.0f);
    const float questWidth = std::clamp(layout.contentFrame.width * (layout.compact ? 0.52f : 0.48f), 340.0f, 580.0f);
    const float questHeight = FitPanelHeight(contentHeight * 0.20f, 106.0f, layout.compact ? 122.0f : 140.0f);
    const float partyHeight = FitPanelHeight(contentHeight * 0.24f, 132.0f, layout.compact ? 150.0f : 182.0f);
    layout.questPanel = Rectangle {contentLeft, contentTop, questWidth, questHeight};
    layout.partyPanel = Rectangle {contentRight - partyWidth, contentTop, partyWidth, partyHeight};

    const bool sideDebug = includeDebug && layout.contentFrame.width >= 1040.0f && contentHeight >= 560.0f;
    const float chatWidth = std::clamp(layout.contentFrame.width * (sideDebug ? 0.58f : 0.62f), 400.0f, 610.0f);
    const float debugWidth = sideDebug
        ? std::clamp(layout.contentFrame.width - chatWidth - layout.gap, 280.0f, 360.0f)
        : std::min(320.0f, partyWidth);
    const float debugHeight = includeDebug ? FitPanelHeight(contentHeight * (sideDebug ? 0.30f : 0.22f), 118.0f, sideDebug ? 210.0f : 156.0f) : 0.0f;
    const float availableChatHeight = contentBottom - (layout.questPanel.y + layout.questPanel.height) - layout.gap;
    const float chatHeight = FitPanelHeight(availableChatHeight, layout.shortScreen ? 132.0f : 164.0f, layout.compact ? 208.0f : 248.0f);
    layout.chatPanel = Rectangle {contentLeft, contentBottom - chatHeight, chatWidth, chatHeight};

    if (sideDebug && includeDebug) {
        layout.debugPanel = Rectangle {
            contentRight - debugWidth,
            contentBottom - debugHeight,
            debugWidth,
            debugHeight
        };
    } else if (includeDebug) {
        layout.debugPanel = Rectangle {
            contentRight - debugWidth,
            layout.partyPanel.y + layout.partyPanel.height + layout.gap,
            debugWidth,
            debugHeight
        };
        const float maxDebugY = layout.chatPanel.y - layout.gap - debugHeight;
        layout.debugPanel.y = std::min(layout.debugPanel.y, maxDebugY);
    }
    return layout;
}

Rectangle ChatInputRect(const Rectangle& panel) {
    const float inputHeight = panel.height < 180.0f ? 28.0f : 30.0f;
    const float inset = panel.width < 340.0f ? 12.0f : 14.0f;
    return Rectangle {panel.x + inset, panel.y + panel.height - inputHeight - 14.0f, panel.width - inset * 2.0f, inputHeight};
}

Rectangle ChatMessageAreaRect(const Rectangle& panel) {
    const Rectangle inputBox = ChatInputRect(panel);
    const float inset = panel.width < 340.0f ? 14.0f : 16.0f;
    const float topOffset = panel.height < 170.0f ? 42.0f : 50.0f;
    return Rectangle {
        panel.x + inset,
        panel.y + topOffset,
        panel.width - inset * 2.0f,
        inputBox.y - (panel.y + topOffset) - 8.0f
    };
}

Rectangle CenteredCardInFrame(const Rectangle& frame, float preferredWidth, float maxWidth, float preferredHeight, float minHeight) {
    const float width = std::clamp(preferredWidth, std::min(300.0f, frame.width), std::min(maxWidth, frame.width));
    const float height = std::clamp(preferredHeight, minHeight, frame.height);
    return Rectangle {
        std::round(frame.x + (frame.width - width) * 0.5f),
        std::round(frame.y + (frame.height - height) * 0.5f),
        std::round(width),
        std::round(height)
    };
}

void DrawCardSurface(const Rectangle& rect, float roundness = 0.08f) {
    DrawRectangleRounded(rect, roundness, 10, LoginCardColor());
    DrawRectangleRoundedLinesEx(rect, roundness, 10, 1.5f, OutlineColor());
}

void DrawPanelSurface(const Rectangle& rect, float roundness = 0.10f) {
    DrawRectangleRounded(rect, roundness, 10, SoftCardColor());
    DrawRectangleRoundedLinesEx(rect, roundness, 10, 1.5f, OutlineColor());
}

void LoadGameUiFont() {
    if (g_ownsUiFont || g_uiFont.texture.id > 0) {
        return;
    }

#if defined(PLATFORM_ANDROID)
    const std::vector<std::string> candidates {
        "fonts/Rubik.ttf",
        "fonts/OpenSans-Regular.ttf",
        "fonts/OpenSans-SemiBold.ttf",
        "fonts/RedHatDisplay-Regular.otf"
    };

    for (const std::string& path : candidates) {
        g_uiFont = LoadFontEx(path.c_str(), 64, nullptr, 0);
        g_ownsUiFont = g_uiFont.texture.id > 0;
        if (g_ownsUiFont) {
            GenTextureMipmaps(&g_uiFont.texture);
            SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_TRILINEAR);
            TraceLog(LOG_INFO, "UI FONT: Loaded Android UI font from %s", path.c_str());
            return;
        }
    }
#else
    const std::filesystem::path repoRoot = std::filesystem::path(NGANU_REPO_ROOT).lexically_normal();
    const std::vector<std::filesystem::path> candidates {
        repoRoot / "nganu.game" / "assets" / "fonts" / "Rubik.ttf",
        repoRoot / "nganu.game" / "assets" / "fonts" / "OpenSans-Regular.ttf",
        repoRoot / "nganu.game" / "assets" / "fonts" / "OpenSans-SemiBold.ttf",
        repoRoot / "nganu.game" / "assets" / "fonts" / "RedHatDisplay-Regular.otf"
    };

    for (const auto& path : candidates) {
        if (!std::filesystem::exists(path)) {
            continue;
        }

        g_uiFont = LoadFontEx(path.string().c_str(), 64, nullptr, 0);
        g_ownsUiFont = g_uiFont.texture.id > 0;
        if (g_ownsUiFont) {
            GenTextureMipmaps(&g_uiFont.texture);
            SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_TRILINEAR);
            TraceLog(LOG_INFO, "UI FONT: Loaded custom UI font from %s", path.string().c_str());
            return;
        }
    }
#endif

    g_uiFont = GetFontDefault();
    SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_BILINEAR);
    TraceLog(LOG_WARNING, "UI FONT: Falling back to raylib default font");
}

void UnloadGameUiFont() {
    if (g_ownsUiFont && g_uiFont.texture.id > 0) {
        UnloadFont(g_uiFont);
    }
    g_uiFont = Font {};
    g_ownsUiFont = false;
}

const Font& UiFontRef() {
    if (g_uiFont.texture.id <= 0) {
        LoadGameUiFont();
    }
    static Font defaultFont = GetFontDefault();
    return g_uiFont.texture.id > 0 ? g_uiFont : defaultFont;
}

int MeasureUiText(const std::string& text, int fontSize) {
    return static_cast<int>(std::round(MeasureTextEx(UiFontRef(), text.c_str(), static_cast<float>(fontSize), 0.0f).x));
}

void DrawUiText(const std::string& text, float x, float y, int fontSize, Color color) {
    DrawTextEx(UiFontRef(), text.c_str(), Vector2 {x, y}, static_cast<float>(fontSize), 0.0f, color);
}

void DrawUiText(const char* text, float x, float y, int fontSize, Color color) {
    DrawTextEx(UiFontRef(), text, Vector2 {x, y}, static_cast<float>(fontSize), 0.0f, color);
}

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
    LoadGameUiFont();
    player_.name = "Fanorisky";
    player_.position = Vector2 {160.0f, 160.0f};
    player_.bodyColor = LocalColor();
    player_.radius = 15.0f;
    loginName_ = player_.name;
#if defined(PLATFORM_ANDROID)
    loginHost_ = "10.0.2.2";
#else
    loginHost_ = "127.0.0.1";
#endif
    loginPort_ = "7777";
    loginStatus_ = "Checking server update...";

    camera_.offset = Vector2 {640.0f, 360.0f};
    camera_.target = player_.position;
    camera_.rotation = 0.0f;
    camera_.zoom = 1.0f;
    lastSentPosition_ = player_.position;

    inventory_.resize(20);
    InventoryNetCallbacks inventoryCallbacks;
    inventoryCallbacks.sendOpen = [this]() { network_.SendInventoryOpen(); };
    inventoryCallbacks.sendClose = [this]() { network_.SendInventoryClose(); };
    inventoryCallbacks.sendUseItem = [this](int slot) { network_.SendUseItem(slot); };
    inventoryCallbacks.sendMoveItem = [this](int from, int to) { network_.SendMoveItem(from, to); };
    inventoryCallbacks.sendDropItem = [this](int slot) { network_.SendDropItem(slot); };
    auto inventoryUi = std::make_unique<InventoryUi>(&inventory_, &itemDefs_, &uiAssets_, &uiTheme_, inventoryCallbacks);
    inventoryUi_ = inventoryUi.get();
    uiSystem_.Add(std::move(inventoryUi));

    auto objectiveUi = std::make_unique<ObjectiveUi>(&currentObjective_, &uiTheme_, &uiAssets_);
    objectiveUi_ = objectiveUi.get();
    uiSystem_.Add(std::move(objectiveUi));

    auto modalUi = std::make_unique<ModalDialogUi>(&uiTheme_, &uiAssets_);
    modalDialogUi_ = modalUi.get();
    uiSystem_.Add(std::move(modalUi));

    AddChatLine("[System] Booting nganu.game client");
}

Game::~Game() {
    UnloadGameUiFont();
}

void Game::Shutdown() {
    network_.Disconnect();
    uiAssets_.UnloadAll();
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
    UpdateUi(dt);
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
    itemDefs_.Clear();
    uiData_.Clear();
    uiTheme_.Clear();
    uiAssets_.UnloadAll();
    inventory_.resize(20);
    inventory_.open = false;
    inventory_.revision = 0;
    inventoryReady_ = false;
    inventoryLayoutReady_ = false;
    if (inventoryUi_ != nullptr && inventoryUi_->IsOpen()) {
        inventoryUi_->Close();
    }
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
#if defined(PLATFORM_ANDROID)
    const LoginHitRects hitRects = ComputeLoginHitRects(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()));
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 point = GetMousePosition();
        if (CheckCollisionPointRec(point, hitRects.nameBox)) {
            loginField_ = LoginField::Name;
            ShowAndroidKeyboard();
        } else if (CheckCollisionPointRec(point, hitRects.hostBox)) {
            loginField_ = LoginField::Host;
            ShowAndroidKeyboard();
        } else if (CheckCollisionPointRec(point, hitRects.portBox)) {
            loginField_ = LoginField::Port;
            ShowAndroidKeyboard();
        } else if (CheckCollisionPointRec(point, hitRects.buttonBox)) {
            HideAndroidKeyboard();
            StartLogin();
            return;
        }
    }

    switch (loginField_) {
    case LoginField::Name:
        AndroidConsumeSoftChar(loginName_, 24, false);
        break;
    case LoginField::Host:
        AndroidConsumeSoftChar(loginHost_, 64, false);
        break;
    case LoginField::Port:
        AndroidConsumeSoftChar(loginPort_, 5, true);
        break;
    }
#endif

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
    HideAndroidKeyboard();
    if (loginName_.empty()) {
        loginStatus_ = "Player name is required";
        return;
    }
    if (!manifest_.valid) {
#if defined(PLATFORM_ANDROID)
        BeginBootUpdateCheck();
#else
        loginStatus_ = "Client content is not ready. Press F5 to re-check.";
#endif
        return;
    }
    if (!mapReady_) {
#if defined(PLATFORM_ANDROID)
        BeginBootUpdateCheck();
#else
        loginStatus_ = "Map content is still loading. Press F5 if needed.";
#endif
        return;
    }
    if (!network_.IsConnected() || !handshakeReady_) {
#if defined(PLATFORM_ANDROID)
        BeginBootUpdateCheck();
#else
        loginStatus_ = "Server session is offline. Press F5 to reconnect.";
#endif
        return;
    }

    player_.name = loginName_;
    player_.position = world_.spawnPoint();
    lastSentPosition_ = player_.position;
    camera_.target = player_.position;
    chatFocused_ = false;
    chatInput_.clear();
    sendAccumulator_ = 0.0f;
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
    if (chatFocused_ || uiInputBlockingWorld_) {
        player_.velocity = Vector2 {};
        return;
    }

    Vector2 input {};
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) input.x -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) input.x += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) input.y -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) input.y += 1.0f;
#if defined(PLATFORM_ANDROID)
    const Vector2 touchInput = AndroidMovementInput();
    if (std::fabs(touchInput.x) > 0.01f || std::fabs(touchInput.y) > 0.01f) {
        input = touchInput;
    }
#endif

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
    const Rectangle viewport = GameplayViewport(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()));
    camera_.offset = Vector2 {
        viewport.x + viewport.width * 0.5f,
        viewport.y + viewport.height * 0.5f
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
    const float sendInterval = movedEnough ? 0.10f : 1.0f;

    if (network_.IsConnected() && sendAccumulator_ >= sendInterval) {
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
        inventory_.open = false;
        inventory_.revision = 0;
        inventoryReady_ = false;
        if (inventoryUi_ != nullptr && inventoryUi_->IsOpen()) {
            inventoryUi_->Close();
        }
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
        EnsureUiDataAssetsRequested();
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
        if (blob->kind == "image" && blob->key.rfind("ui_image:", 0) == 0) {
            LoadUiTextureFromCache(blob->key);
        }
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
                    player_.position = keepPosition;
                    lastSentPosition_ = keepPosition;
                }
            }
        } else if (blob->kind == "data") {
            ApplyDataAsset(*blob);
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
    case NetworkEvent::Type::PlayerPosition:
        player_.position = Vector2 {event.x, event.y};
        lastSentPosition_ = player_.position;
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
    case NetworkEvent::Type::InventoryFullState: {
        const std::vector<uint8_t>& payload = event.rawBytes;
        if (payload.size() < 3) {
            break;
        }
        inventory_.open = true;
        inventory_.container_id = static_cast<int>(payload[1]);
        const int slotCount = static_cast<int>(payload[2]);
        inventory_.resize(slotCount);
        size_t offset = 3;
        for (int i = 0; i < slotCount; ++i) {
            if (offset + 7 > payload.size()) break;
            ClientSlot* slot = inventory_.slotAt(static_cast<int>(payload[offset]));
            if (!slot) {
                offset += 7;
                continue;
            }
            slot->occupied = payload[offset + 1] != 0;
            slot->item_def_id = static_cast<int>(payload[offset + 2] | (payload[offset + 3] << 8));
            slot->amount = static_cast<int>(payload[offset + 4] | (payload[offset + 5] << 8));
            slot->flags = payload[offset + 6];
            offset += 7;
        }
        ++inventory_.revision;
        inventoryReady_ = true;
        if (inventoryUi_ != nullptr) {
            inventoryUi_->ApplyFullState(inventory_);
        }
        break;
    }
    case NetworkEvent::Type::InventorySlotUpdate: {
        ClientSlot* slot = inventory_.slotAt(event.slotIndex);
        if (!slot) break;
        slot->occupied = event.occupied;
        slot->item_def_id = event.itemDefId;
        slot->amount = event.amount;
        slot->flags = event.flags;
        ++inventory_.revision;
        inventoryReady_ = true;
        if (inventoryUi_ != nullptr) {
            inventoryUi_->ApplySlotUpdate(event.slotIndex, event.occupied, event.itemDefId, event.amount, event.flags);
        }
        break;
    }
    case NetworkEvent::Type::InventoryError:
        AddChatLine("[Inventory] Action rejected by server (" + std::to_string(event.errCode) + ").");
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
#if defined(PLATFORM_ANDROID)
    char* cacheDir = GetCacheDir();
    if (cacheDir != nullptr) {
        const std::filesystem::path path(cacheDir);
        MemFree(cacheDir);
        return path / "nganu";
    }
#endif
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
    } else if (filename.rfind("ui_image:", 0) == 0) {
        filename = filename.substr(9);
        bucket = "ui";
    } else if (filename.rfind("ui_meta:", 0) == 0) {
        filename = filename.substr(8);
        bucket = "ui";
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

void Game::EnsureUiDataAssetsRequested() {
    if (!network_.IsConnected() || manifest_.revision.empty()) {
        return;
    }

    for (const std::string& assetKey : manifest_.assets) {
        if (assetKey.rfind("data:", 0) != 0) {
            continue;
        }

        std::string cached;
        if (LoadCachedAsset(assetKey, manifest_.revision, cached)) {
            AssetBlob cachedBlob;
            cachedBlob.key = assetKey;
            cachedBlob.kind = "data";
            cachedBlob.revision = manifest_.revision;
            cachedBlob.content = cached;
            ApplyDataAsset(cachedBlob);
            continue;
        }

        network_.RequestAsset(assetKey);
    }
}

void Game::EnsureUiThemeAssetsRequested() {
    if (!network_.IsConnected()) {
        return;
    }

    for (const std::string& textureKey : uiTheme_.ReferencedTextureKeys()) {
        if (textureKey.rfind("ui:", 0) != 0) {
            continue;
        }
        const std::string assetKey = "ui_image:" + textureKey.substr(3);
        if (HasCachedImageAsset(assetKey, manifest_.revision)) {
            LoadUiTextureFromCache(assetKey);
            continue;
        }
        network_.RequestAsset(assetKey);
    }
}

void Game::ApplyDataAsset(const AssetBlob& asset) {
    if (asset.kind != "data") {
        return;
    }

    if (asset.key == "data:item_defs.json") {
        itemDefs_.LoadFromJson(asset.content);
        AddChatLine("[System] Item definitions ready: " + std::to_string(itemDefs_.Count()));
        return;
    }

    if (asset.key.rfind("data:ui/", 0) == 0) {
        uiData_.Store(asset.key, asset.content);
        if (asset.key == "data:ui/theme_default.json") {
            if (uiTheme_.LoadFromJson(asset.content)) {
                EnsureUiThemeAssetsRequested();
                AddChatLine("[System] UI theme ready: theme_default");
            }
        }
        const Ui::DataDocument* document = uiData_.FindByAssetKey(asset.key);
        if (document && document->windowConfig.has_value()) {
            Ui::Widget* widget = uiSystem_.Find(document->windowConfig->windowId);
            if (widget != nullptr) {
                widget->ApplyWindowConfig(*document->windowConfig);
            }
            if (document->windowConfig->windowId == "inventory_main") {
                inventoryLayoutReady_ = true;
            }
        }
        AddChatLine("[System] UI document loaded: " + asset.key);
    }
}

void Game::LoadUiTextureFromCache(const std::string& assetKey) {
    if (assetKey.rfind("ui_image:", 0) != 0) {
        return;
    }
    const std::filesystem::path path = ImageCachePathForAsset(assetKey, manifest_.revision);
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return;
    }
    uiAssets_.LoadFromBytes("ui:" + assetKey.substr(9), bytes);
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
#if defined(PLATFORM_ANDROID)
    if (AndroidChatPressed()) {
        if (!chatFocused_) {
            chatFocused_ = true;
            ShowAndroidKeyboard();
            return;
        }

        if (!chatInput_.empty()) {
            if (!network_.SendChatMessage(chatInput_)) {
                AddChatLine("[System] Chat send failed");
            }
            chatInput_.clear();
        }
        chatFocused_ = false;
        HideAndroidKeyboard();
        return;
    }
#endif

    if (IsKeyPressed(KEY_ENTER)) {
        if (!chatFocused_) {
            chatFocused_ = true;
#if defined(PLATFORM_ANDROID)
            ShowAndroidKeyboard();
#endif
            return;
        }

        if (!chatInput_.empty()) {
            if (!network_.SendChatMessage(chatInput_)) {
                AddChatLine("[System] Chat send failed");
            }
            chatInput_.clear();
        }
        chatFocused_ = false;
        HideAndroidKeyboard();
    }

    if (!chatFocused_) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        chatFocused_ = false;
        chatInput_.clear();
        HideAndroidKeyboard();
        return;
    }

#if defined(PLATFORM_ANDROID)
    AndroidConsumeSoftChar(chatInput_, 90, false);
#endif

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
    const HudLayout layout = ComputeHudLayout(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), showDebug_);
    const Rectangle panel = layout.chatPanel;
    const Rectangle messageArea = ChatMessageAreaRect(panel);

    const int rowHeight = layout.chatFont + 4;
    const int visibleRows = std::max(1, static_cast<int>(messageArea.height) / rowHeight);
    const float maxScroll = static_cast<float>(std::max(0, static_cast<int>(BuildChatRows(messageArea.width, layout.chatFont).size()) - visibleRows));

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

void Game::UpdateUi(float dt) {
    if (uiMode_ != UiMode::World) {
        uiInputBlockingWorld_ = false;
        return;
    }

    if (IsKeyPressed(KEY_I) && inventoryUi_ != nullptr) {
        inventoryUi_->Toggle();
    }
#if defined(PLATFORM_ANDROID)
    if (AndroidInventoryPressed() && inventoryUi_ != nullptr) {
        inventoryUi_->Toggle();
    }
#endif
    if (IsKeyPressed(KEY_J) && objectiveUi_ != nullptr) {
        objectiveUi_->Toggle();
    }
    if (IsKeyPressed(KEY_M) && modalDialogUi_ != nullptr) {
        const std::string worldName = world_.worldName().empty() ? world_.mapId() : world_.worldName();
        modalDialogUi_->Show("Layer Test",
                             "This modal sits above window-layer UI.\n\n"
                             "World: " + worldName + "\n"
                             "Windows below should not receive world input while this is open.");
    }

    uiSystem_.Update(dt);
    uiInputBlockingWorld_ = uiSystem_.ConsumedInput() || uiSystem_.HasVisibleWidget();
}

void Game::UpdateNpcAndQuest() {
    if (chatFocused_ || uiInputBlockingWorld_) return;

    bool interactPressed = IsKeyPressed(KEY_E);
#if defined(PLATFORM_ANDROID)
    interactPressed = interactPressed || AndroidInteractPressed();
#endif

    if (interactPressed) {
        const WorldObject* nearest = nullptr;
        float nearestDistanceSq = std::numeric_limits<float>::max();
        for (const WorldObject& object : world_.objects()) {
            if (!IsInteractableObject(object)) continue;
            const Vector2 center = world_.objectCenter(object);
            const float dx = player_.position.x - center.x;
            const float dy = player_.position.y - center.y;
            const float distSq = dx * dx + dy * dy;
            const float maxDistance = InteractionRangeForObject(object);
            if (distSq > maxDistance * maxDistance) continue;
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

bool Game::IsInteractableObject(const WorldObject& object) const {
    const std::optional<std::string> interact = world_.objectProperty(object, "interact");
    if (interact.has_value()) {
        return *interact != "false" && *interact != "0" && !interact->empty();
    }
    return object.kind == "npc" || object.kind == "portal";
}

float Game::InteractionRangeForObject(const WorldObject& object) const {
    const std::optional<std::string> value = world_.objectProperty(object, "interact_radius");
    if (value.has_value()) {
        try {
            return std::max(24.0f, std::stof(*value));
        } catch (...) {
        }
    }
    return std::clamp(std::max(object.bounds.width, object.bounds.height) * 0.5f + 40.0f, 56.0f, 120.0f);
}

std::string Game::InteractionPromptForObject(const WorldObject& object) const {
    const std::optional<std::string> prompt = world_.objectProperty(object, "prompt");
    if (prompt.has_value() && !prompt->empty()) {
        return "E " + *prompt;
    }
    if (object.kind == "portal") {
        return "E Travel";
    }
    if (object.kind == "npc") {
        return "E Talk";
    }
    return "E Interact";
}

std::string Game::DisplayNameForObject(const WorldObject& object) const {
    const std::optional<std::string> title = world_.objectProperty(object, "title");
    if (title.has_value() && !title->empty()) {
        return *title;
    }
    const std::optional<std::string> name = world_.objectProperty(object, "name");
    if (name.has_value() && !name->empty()) {
        return *name;
    }
    if (!object.id.empty()) {
        return object.id;
    }
    return object.kind;
}

void Game::Draw() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    ClearBackground(BackgroundDeep());

    if (uiMode_ == UiMode::Boot || uiMode_ == UiMode::RetryWait) {
        DrawBootScreen();
        return;
    }

    if (uiMode_ == UiMode::MainMenu || uiMode_ == UiMode::LoggingIn) {
        DrawLoginScreen();
        return;
    }

    DrawRectangleGradientV(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), BackgroundSky(), BackgroundHorizon());
    DrawCircleGradient(static_cast<int>(screenWidth * 0.18f),
                       static_cast<int>(screenHeight * 0.10f),
                       std::max(screenWidth, screenHeight) * 0.28f,
                       Fade(WHITE, 0.18f),
                       Fade(WHITE, 0.0f));
    DrawCircleGradient(static_cast<int>(screenWidth * 0.84f),
                       static_cast<int>(screenHeight * 0.18f),
                       std::max(screenWidth, screenHeight) * 0.20f,
                       Fade(Color {229, 244, 240, 120}, 0.22f),
                       Fade(WHITE, 0.0f));
    DrawScene();
    DrawHud();
#if defined(PLATFORM_ANDROID)
    DrawAndroidTouchControls(chatFocused_);
#endif
}

void Game::DrawLoginScreen() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float scale = UiScaleForScreen(screenWidth, screenHeight);
    const Rectangle safeFrame = SafeFrameForScreen(screenWidth, screenHeight, 1.24f, 520.0f, 0.88f);
    const bool compact = safeFrame.width < 760.0f;
    const bool tiny = safeFrame.width < 620.0f || safeFrame.height < 560.0f;
    const float cardPadding = tiny ? 18.0f : 28.0f;
    const float fieldHeight = tiny ? 40.0f : 44.0f;
    const float labelOffset = tiny ? 18.0f : 22.0f;
    const int titleFont = tiny ? 26 : (compact ? 30 : 34);
    const int subtitleFont = tiny ? 16 : (compact ? 18 : 20);
    const int bodyFont = tiny ? 15 : 16;
    const int fieldFont = tiny ? 19 : 22;
    const int buttonFont = tiny ? 21 : 24;
    const int infoFont = tiny ? 15 : 17;
    const float cardWidth = std::min(tiny ? 520.0f : 620.0f, safeFrame.width);
    const float cardHeight = std::min(tiny ? 470.0f : (compact ? 540.0f : 520.0f), safeFrame.height);

    DrawRectangleGradientV(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), Color {209, 236, 227, 255}, Color {138, 180, 160, 255});
    DrawCircle(static_cast<int>(screenWidth - 180.0f), 140, 150.0f, Fade(WHITE, 0.18f));
    DrawCircle(120, static_cast<int>(screenHeight - 120.0f), 180.0f, Fade(Color {54, 110, 77, 255}, 0.16f));
    DrawCircleGradient(static_cast<int>(screenWidth * 0.5f), static_cast<int>(screenHeight * 0.22f), 180.0f * scale, Fade(Color {255, 255, 255, 40}, 0.55f), Fade(WHITE, 0.0f));

    const Rectangle card = CenteredCardInFrame(safeFrame, cardWidth, 620.0f, cardHeight, tiny ? 420.0f : 460.0f);

    DrawCardSurface(card);
    DrawRectangleRounded(Rectangle {card.x + cardPadding - 10.0f, card.y + cardPadding - 10.0f, card.width - (cardPadding * 2.0f) + 20.0f, tiny ? 78.0f : 90.0f}, 0.14f, 10, Fade(WHITE, 0.04f));
    DrawUiText("nganu.game", card.x + cardPadding, card.y + cardPadding - 2.0f, titleFont, RAYWHITE);
    DrawUiText("Minimal client, live content bootstrap", card.x + cardPadding + 2.0f, card.y + cardPadding + 34.0f, subtitleFont, Fade(RAYWHITE, 0.78f));
    DrawUiText("Login only opens after server manifest and map are ready.", card.x + cardPadding + 2.0f, card.y + cardPadding + (tiny ? 56.0f : 62.0f), bodyFont, Fade(RAYWHITE, 0.56f));

    const float innerX = card.x + cardPadding;
    const float innerWidth = card.width - cardPadding * 2.0f;
    const float hostWidth = (compact || tiny) ? innerWidth : innerWidth - 120.0f;
    const Rectangle nameBox {innerX, card.y + cardPadding + 104.0f, innerWidth, fieldHeight};
    const Rectangle hostBox {innerX, nameBox.y + fieldHeight + (tiny ? 30.0f : 28.0f), hostWidth, fieldHeight};
    const Rectangle portBox {compact || tiny ? innerX : hostBox.x + hostBox.width + 14.0f, compact || tiny ? hostBox.y + fieldHeight + 28.0f : hostBox.y, compact || tiny ? innerWidth : 106.0f, fieldHeight};
    const Rectangle buttonBox {innerX, portBox.y + fieldHeight + (tiny ? 24.0f : 34.0f), innerWidth, tiny ? 46.0f : 50.0f};

    auto drawField = [&](const Rectangle& box, const char* label, const std::string& value, bool active) {
        DrawUiText(label, box.x, box.y - labelOffset, tiny ? 16 : 18, Fade(RAYWHITE, 0.72f));
        DrawRectangleRounded(box, 0.18f, 8, active ? Fade(AccentColor(), 0.18f) : Fade(WHITE, 0.07f));
        DrawRectangleRoundedLinesEx(box, 0.18f, 8, 2.0f, active ? AccentColor() : Fade(RAYWHITE, 0.20f));
        const std::string content = active ? (value.empty() ? "_" : value + "_") : value;
        const std::string clipped = EllipsizeText(content, fieldFont, box.width - 28.0f);
        DrawUiText(clipped, box.x + 14.0f, box.y + (tiny ? 10.0f : 12.0f), fieldFont, RAYWHITE);
    };

    drawField(nameBox, "Player Name", loginName_, loginField_ == LoginField::Name);
    drawField(hostBox, "Server Host", loginHost_, loginField_ == LoginField::Host);
    drawField(portBox, "Port", loginPort_, loginField_ == LoginField::Port);

    DrawRectangleRounded(buttonBox, 0.22f, 8, AccentColor());
    const std::string buttonText = uiMode_ == UiMode::LoggingIn ? "Spawning..." : "Enter World";
    DrawUiText(buttonText,
               buttonBox.x + (buttonBox.width - static_cast<float>(MeasureUiText(buttonText, buttonFont))) * 0.5f,
               buttonBox.y + (tiny ? 11.0f : 13.0f),
               buttonFont,
               Color {15, 31, 25, 255});

    const Rectangle manifestBox {innerX, buttonBox.y + buttonBox.height + (tiny ? 16.0f : 12.0f), innerWidth, tiny ? 78.0f : (compact ? 84.0f : 74.0f)};
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
    DrawUiText(EllipsizeText(serverLabel, infoFont, manifestBox.width - 22.0f), manifestBox.x + 12.0f, manifestBox.y + 10.0f, infoFont, Fade(RAYWHITE, 0.86f));
    DrawUiText(EllipsizeText(worldLabel, infoFont, manifestBox.width - 22.0f), manifestBox.x + 12.0f, manifestBox.y + 10.0f + static_cast<float>(infoFont + 3), infoFont, Fade(RAYWHITE, 0.74f));
    DrawUiText(EllipsizeText(revisionLabel, infoFont, manifestBox.width - 22.0f), manifestBox.x + 12.0f, manifestBox.y + 10.0f + static_cast<float>((infoFont + 3) * 2), infoFont, Fade(RAYWHITE, 0.74f));

    DrawWrappedText(loginStatus_, Rectangle {innerX, card.y + card.height - (tiny ? 88.0f : 72.0f), innerWidth, tiny ? 52.0f : 40.0f}, tiny ? 16 : 18, Fade(RAYWHITE, 0.82f), tiny ? 3 : 2);
    DrawUiText(compact ? "Tab switch, Enter login, F5 re-check" : "Tab to switch fields, Enter to login, F5 to re-check update",
               innerX,
               card.y + card.height - (tiny ? 28.0f : 34.0f),
               tiny ? 14 : 16,
               Fade(RAYWHITE, 0.60f));
}

void Game::DrawBootScreen() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float scale = UiScaleForScreen(screenWidth, screenHeight);
    const Rectangle safeFrame = SafeFrameForScreen(screenWidth, screenHeight, 1.18f, 480.0f, 0.86f);
    const bool compact = safeFrame.width < 720.0f || safeFrame.height < 620.0f;
    const bool tiny = safeFrame.width < 580.0f || safeFrame.height < 520.0f;
    const float cardPadding = tiny ? 18.0f : 30.0f;
    const float cardWidth = std::min(tiny ? 500.0f : 560.0f, safeFrame.width);
    const float cardHeight = std::min(tiny ? 300.0f : 332.0f, safeFrame.height);
    DrawRectangleGradientV(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), Color {205, 232, 236, 255}, Color {118, 153, 171, 255});
    DrawCircle(static_cast<int>(screenWidth - 220.0f), 120, 140.0f, Fade(WHITE, 0.16f));
    DrawCircle(160, static_cast<int>(screenHeight - 140.0f), 170.0f, Fade(Color {35, 62, 84, 255}, 0.14f));
    DrawCircleGradient(static_cast<int>(screenWidth * 0.34f), static_cast<int>(screenHeight * 0.30f), 210.0f * scale, Fade(Color {255, 255, 255, 34}, 0.55f), Fade(WHITE, 0.0f));

    const Rectangle card = CenteredCardInFrame(safeFrame, cardWidth, 560.0f, cardHeight, tiny ? 280.0f : 300.0f);

    DrawCardSurface(card);
    DrawUiText("nganu.game", card.x + cardPadding, card.y + cardPadding - 2.0f, tiny ? 26 : 34, RAYWHITE);
    DrawUiText("Boot update check", card.x + cardPadding, card.y + cardPadding + (tiny ? 26.0f : 40.0f), compact ? 18 : 22, Fade(RAYWHITE, 0.78f));

    const Rectangle infoBox {card.x + cardPadding, card.y + cardPadding + (tiny ? 70.0f : 82.0f), card.width - cardPadding * 2.0f, tiny ? 80.0f : 92.0f};
    DrawRectangleRounded(infoBox, 0.16f, 8, Fade(WHITE, 0.06f));
    DrawWrappedText(loginStatus_, infoBox, tiny ? 17 : 22, RAYWHITE, compact ? 4 : 3);

    std::string footer = "Trying " + loginHost_ + ":" + loginPort_;
    if (uiMode_ == UiMode::RetryWait) {
        footer = "Retrying in " + std::to_string(static_cast<int>(std::ceil(retryCountdown_))) + " seconds";
    }
    DrawUiText(EllipsizeText(footer, compact ? 16 : 18, card.width - cardPadding * 2.0f), card.x + cardPadding, card.y + card.height - (tiny ? 70.0f : 96.0f), compact ? 16 : 18, AccentColor());
    DrawUiText("Content comes from the server manifest before menu access", card.x + cardPadding, card.y + card.height - (tiny ? 42.0f : 66.0f), tiny ? 14 : 16, Fade(RAYWHITE, 0.58f));
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
            if (IsInteractableObject(object)) {
                const Vector2 center = world_.objectCenter(object);
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), std::max(object.bounds.width, object.bounds.height) * 0.5f + 6.0f, Fade(AccentColor(), 0.75f));
                DrawUiText(DisplayNameForObject(object),
                           center.x - 54.0f,
                           center.y - 44.0f,
                           16,
                           Fade(RAYWHITE, 0.88f));
                if (IsNearPosition(player_.position, center, InteractionRangeForObject(object))) {
                    DrawUiText(InteractionPromptForObject(object), center.x - 28.0f, center.y + 24.0f, 16, AccentColor());
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

    for (const WorldObject& object : world_.objects()) {
        if (object.kind != "npc") {
            continue;
        }
        if (!CheckCollisionRecs(object.bounds, visibleArea)) {
            continue;
        }
        DrawNpc(object);
    }
    DrawAvatar(player_, true);

    for (const WorldObject& object : world_.objects()) {
        if (!CheckCollisionRecs(object.bounds, visibleArea)) {
            continue;
        }
        if ((object.kind == "prop" || object.kind == "portal") && world_.objectZLayer(object) > 0) {
            world_.DrawObjectSprite(object);
            if (IsInteractableObject(object)) {
                const Vector2 center = world_.objectCenter(object);
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), std::max(object.bounds.width, object.bounds.height) * 0.5f + 6.0f, Fade(AccentColor(), 0.75f));
                DrawUiText(DisplayNameForObject(object),
                           center.x - 54.0f,
                           center.y - 44.0f,
                           16,
                           Fade(RAYWHITE, 0.88f));
                if (IsNearPosition(player_.position, center, InteractionRangeForObject(object))) {
                    DrawUiText(InteractionPromptForObject(object), center.x - 28.0f, center.y + 24.0f, 16, AccentColor());
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
    DrawUiText(avatar.name, avatar.position.x - 28.0f, avatar.position.y - 34.0f, 16, RAYWHITE);
}

void Game::DrawNpc(const WorldObject& object) const {
    const Vector2 drawPosition = world_.objectCenter(object);
    const std::string drawName = world_.objectProperty(object, "name").value_or(DisplayNameForObject(object));
    const std::string drawTitle = world_.objectProperty(object, "title").value_or("");
    const float radius = std::max(12.0f, std::min(object.bounds.width, object.bounds.height) * 0.32f);

    DrawEllipse(
        static_cast<int>(drawPosition.x),
        static_cast<int>(drawPosition.y + radius + 8.0f),
        radius * 1.2f,
        radius * 0.65f,
        Fade(BLACK, 0.22f)
    );

    const bool drewSprite = world_.DrawObjectSprite(object);
    if (!drewSprite) {
        DrawCircleV(drawPosition, radius, Color {120, 210, 255, 255});
        DrawCircleV(Vector2 {drawPosition.x, drawPosition.y - 4.0f}, radius * 0.5f, Color {251, 235, 205, 255});
        DrawCircleLinesV(drawPosition, radius, Color {255, 244, 171, 255});
    }

    DrawUiText(drawName, drawPosition.x - 24.0f, drawPosition.y - 40.0f, 16, Color {255, 244, 171, 255});
    if (!drawTitle.empty()) {
        DrawUiText(drawTitle, drawPosition.x - 36.0f, drawPosition.y - 58.0f, 14, Fade(RAYWHITE, 0.72f));
    }
    if (IsNearPosition(player_.position, drawPosition, InteractionRangeForObject(object))) {
        DrawUiText(InteractionPromptForObject(object), drawPosition.x - 20.0f, drawPosition.y - 84.0f, 16, AccentColor());
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

    uiSystem_.Draw();
}

void Game::DrawTopBar() const {
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const HudLayout layout = ComputeHudLayout(screenWidth, screenHeight, showDebug_);
    const bool compact = layout.compact;
    const bool singleColumn = layout.singleColumn;
    const float statusWidth = singleColumn ? layout.contentFrame.width
                                           : std::clamp(layout.contentFrame.width * (compact ? 0.52f : 0.44f), 360.0f, 470.0f);
    const Rectangle statusPanel {
        singleColumn ? layout.contentFrame.x : (layout.contentFrame.x + layout.contentFrame.width - statusWidth),
        layout.safeFrame.y + (singleColumn ? (layout.topBarHeight - 42.0f) : (compact ? 36.0f : 10.0f)),
        statusWidth,
        singleColumn ? 32.0f : (compact ? 30.0f : 34.0f)
    };

    DrawRectangle(0, 0, static_cast<int>(screenWidth), static_cast<int>(layout.safeFrame.y + layout.topBarHeight), Fade(BLACK, 0.16f));
    DrawRectangleGradientV(0,
                           0,
                           static_cast<int>(screenWidth),
                           static_cast<int>(layout.safeFrame.y + layout.topBarHeight),
                           Fade(Color {13, 21, 24, 255}, 0.88f),
                           Fade(Color {13, 21, 24, 255}, 0.74f));
    DrawUiText("nganu.game", layout.safeFrame.x, layout.safeFrame.y + (singleColumn ? 10.0f : 12.0f), singleColumn ? 22 : 24, RAYWHITE);
    const float subtitleX = layout.safeFrame.x + 166.0f;
    const float subtitleMaxWidth = statusPanel.x - subtitleX - 18.0f;
    if (!compact && subtitleMaxWidth > 120.0f) {
        DrawUiText(EllipsizeText("Prototype MMORPG 2D Top-Down", 18, subtitleMaxWidth), subtitleX, layout.safeFrame.y + 16.0f, 18, Fade(RAYWHITE, 0.8f));
    } else if (!singleColumn) {
        DrawUiText("Dynamic world client", layout.safeFrame.x + 2.0f, layout.safeFrame.y + 40.0f, 16, Fade(RAYWHITE, 0.72f));
    }

    DrawRectangleRounded(statusPanel, 0.35f, 8, Fade(WHITE, 0.06f));
    const WorldObject* activeRegion = world_.regionAt(player_.position);
    const std::string region = activeRegion
        ? world_.objectProperty(*activeRegion, "title").value_or(activeRegion->id)
        : (world_.worldName().empty() ? "Overworld" : world_.worldName());
    const std::string mapId = world_.mapId().empty() ? "unknown" : world_.mapId();
    const std::string climate = activeRegion
        ? world_.objectProperty(*activeRegion, "climate").value_or(world_.property("climate").value_or("temperate"))
        : world_.property("climate").value_or("temperate");
    if (singleColumn) {
        const int statusFont = 14;
        const std::string line1 = EllipsizeText("Region: " + region, statusFont, statusPanel.width - 116.0f);
        const std::string line2 = EllipsizeText("Map: " + mapId + "  " + climate, 13, statusPanel.width - 22.0f);
        const std::string statusText = EllipsizeText(network_.StatusText(), statusFont, 88.0f);
        DrawUiText(line1, statusPanel.x + 10.0f, statusPanel.y + 4.0f, statusFont, AccentColor());
        DrawUiText(statusText, statusPanel.x + statusPanel.width - 10.0f - static_cast<float>(MeasureUiText(statusText, statusFont)), statusPanel.y + 4.0f, statusFont, Fade(RAYWHITE, 0.80f));
        DrawUiText(line2, layout.safeFrame.x, layout.safeFrame.y + 38.0f, 13, Fade(RAYWHITE, 0.66f));
    } else {
        const int statusFont = compact ? 15 : 18;
        DrawUiText(EllipsizeText("Region: " + region, statusFont, statusPanel.width * 0.34f), statusPanel.x + 14.0f, statusPanel.y + 8.0f, statusFont, AccentColor());
        DrawUiText(EllipsizeText("Map: " + mapId, compact ? 14 : 16, statusPanel.width * 0.24f), statusPanel.x + statusPanel.width * 0.36f, statusPanel.y + 9.0f, compact ? 14 : 16, Fade(RAYWHITE, 0.70f));
        DrawUiText(EllipsizeText(climate, compact ? 14 : 16, statusPanel.width * 0.18f), statusPanel.x + statusPanel.width * 0.63f, statusPanel.y + 9.0f, compact ? 14 : 16, Fade(RAYWHITE, 0.62f));
        const std::string statusText = EllipsizeText(network_.StatusText(), statusFont, statusPanel.width * 0.16f);
        DrawUiText(statusText, statusPanel.x + statusPanel.width * 0.82f, statusPanel.y + 8.0f, statusFont, Fade(RAYWHITE, 0.8f));
    }
}

void Game::DrawChatPanel() const {
    const HudLayout layout = ComputeHudLayout(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), showDebug_);
    const Rectangle panel = layout.chatPanel;
    const float headerPadding = layout.singleColumn ? 14.0f : 18.0f;
    DrawPanelSurface(panel, 0.08f);
    DrawUiText("World Chat", panel.x + headerPadding, panel.y + (layout.singleColumn ? 12.0f : 16.0f), layout.titleFont, RAYWHITE);

    const Rectangle inputBox = ChatInputRect(panel);
    const Rectangle messageArea = ChatMessageAreaRect(panel);
    const int fontSize = layout.chatFont;
    const int rowHeight = fontSize + 4;
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

        DrawUiText(rows[i].text, messageArea.x, lineY, fontSize, color);
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
        if (panel.width > 280.0f) {
            DrawUiText(scrollOffset > 0.5f ? "Scroll: history" : "Scroll: latest",
                       panel.x + panel.width - 116.0f,
                       panel.y + (layout.singleColumn ? 14.0f : 18.0f),
                       12,
                       Fade(RAYWHITE, 0.5f));
        }
    }

    DrawRectangleRounded(inputBox, 0.25f, 8, Fade(WHITE, 0.07f));
    const std::string prompt = chatFocused_ ? (chatInput_.empty() ? "_" : chatInput_ + "_") : (layout.singleColumn ? "Enter chat" : "Press Enter to open chat");
    const std::string promptText = EllipsizeText(prompt, layout.smallFont, inputBox.width - 24.0f);
    DrawUiText(promptText, inputBox.x + 12.0f, inputBox.y + (layout.singleColumn ? 8.0f : 7.0f), layout.smallFont, chatFocused_ ? AccentColor() : Fade(RAYWHITE, 0.55f));
}

void Game::DrawPartyPanel() const {
    const HudLayout layout = ComputeHudLayout(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), showDebug_);
    const Rectangle panel = layout.partyPanel;
    DrawPanelSurface(panel);
    DrawUiText("Online Players", panel.x + 18.0f, panel.y + 16.0f, layout.titleFont, RAYWHITE);
    const std::string localName = EllipsizeText(player_.name, layout.bodyFont, panel.width - 36.0f);
    DrawUiText(localName, panel.x + 18.0f, panel.y + 16.0f + static_cast<float>(layout.titleFont + 10), layout.bodyFont, AccentColor());

    int lineY = static_cast<int>(panel.y + 16.0f + layout.titleFont + layout.bodyFont + 22.0f);
    int shown = 0;
    for (const auto& [playerId, remote] : remotePlayers_) {
        if (shown >= 3) break;
        const std::string remoteName = EllipsizeText(remote.avatar.name, layout.bodyFont, panel.width - 36.0f);
        DrawUiText(remoteName, panel.x + 18.0f, static_cast<float>(lineY), layout.bodyFont, Fade(RAYWHITE, 0.85f));
        lineY += layout.bodyFont + 8;
        ++shown;
    }
}

void Game::DrawQuestPanel() const {
    const HudLayout layout = ComputeHudLayout(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), showDebug_);
    const Rectangle panel = layout.questPanel;
    DrawPanelSurface(panel);
    DrawUiText("Objective", panel.x + 18.0f, panel.y + 16.0f, layout.titleFont, RAYWHITE);
    DrawUiText("Server Objective", panel.x + 18.0f, panel.y + 16.0f + static_cast<float>(layout.titleFont + 8), layout.bodyFont, AccentColor());
    DrawWrappedText(currentObjective_.empty() ? "No active objective." : currentObjective_,
                    Rectangle {panel.x + 18.0f, panel.y + 16.0f + static_cast<float>(layout.titleFont + layout.bodyFont + 22), panel.width - 36.0f, panel.height - (layout.titleFont + layout.bodyFont + 42.0f)},
                    layout.bodyFont - 1,
                    Fade(RAYWHITE, 0.78f),
                    layout.singleColumn ? 4 : 3);
}

void Game::DrawDebugPanel() const {
    const HudLayout layout = ComputeHudLayout(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), showDebug_);
    const Rectangle panel = layout.debugPanel;
    DrawPanelSurface(panel, 0.08f);
    DrawUiText("Debug", panel.x + 18.0f, panel.y + 14.0f, layout.titleFont, RAYWHITE);

    char line[192];
    std::snprintf(line, sizeof(line), "Id: %d  Remote: %zu", localPlayerId_, remotePlayers_.size());
    DrawUiText(line, panel.x + 18.0f, panel.y + 14.0f + static_cast<float>(layout.titleFont + 12), layout.bodyFont, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Player: %.1f, %.1f", player_.position.x, player_.position.y);
    DrawUiText(line, panel.x + 18.0f, panel.y + 14.0f + static_cast<float>(layout.titleFont + layout.bodyFont + 18), layout.bodyFont, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Map: %s  Layers: %zu", world_.mapId().c_str(), world_.layers().size());
    DrawUiText(line, panel.x + 18.0f, panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 2), layout.bodyFont, Fade(RAYWHITE, 0.82f));
    std::snprintf(line, sizeof(line), "Objects: %zu  Music: %s", world_.objects().size(), world_.property("music").value_or("n/a").c_str());
    DrawUiText(line, panel.x + 18.0f, panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 3), layout.bodyFont, Fade(RAYWHITE, 0.82f));
    DrawUiText(EllipsizeText(("Revision: " + (manifest_.revision.empty() ? std::string("n/a") : manifest_.revision)), layout.bodyFont, panel.width - 36.0f),
               panel.x + 18.0f,
               panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 4),
               layout.bodyFont,
               Fade(RAYWHITE, 0.82f));
    DrawUiText(EllipsizeText(("Asset Source: " + lastMapAssetSource_), layout.bodyFont, panel.width - 36.0f),
               panel.x + 18.0f,
               panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 5),
               layout.bodyFont,
               Fade(RAYWHITE, 0.82f));
    if (!pendingMapId_.empty() && pendingMapId_ != world_.mapId()) {
        DrawUiText(EllipsizeText(("Pending Map: " + pendingMapId_), layout.bodyFont, panel.width - 36.0f),
                   panel.x + 18.0f,
                   panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 6),
                   layout.bodyFont,
                   AccentColor());
    } else {
        DrawUiText(EllipsizeText(("Applied Asset: " + (lastAppliedMapAssetKey_.empty() ? std::string("n/a") : lastAppliedMapAssetKey_)), layout.bodyFont, panel.width - 36.0f),
                   panel.x + 18.0f,
                   panel.y + 14.0f + static_cast<float>(layout.titleFont + (layout.bodyFont + 6) * 6),
                   layout.bodyFont,
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
        if (MeasureUiText(candidate, fontSize) <= static_cast<int>(bounds.width)) {
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
        DrawUiText(lines[i], bounds.x, bounds.y + static_cast<float>(i * lineHeight), fontSize, color);
    }
    EndScissorMode();
}

std::string Game::EllipsizeText(const std::string& text, int fontSize, float maxWidth) const {
    if (text.empty() || maxWidth <= 0.0f) {
        return "";
    }

    if (MeasureUiText(text, fontSize) <= static_cast<int>(maxWidth)) {
        return text;
    }

    const std::string ellipsis = "...";
    std::string clipped = text;
    while (!clipped.empty() &&
           MeasureUiText(clipped + ellipsis, fontSize) > static_cast<int>(maxWidth)) {
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
            if (MeasureUiText(candidate, fontSize) <= static_cast<int>(maxWidth)) {
                target = candidate;
                continue;
            }

            if (!usingSecondLine) {
                usingSecondLine = true;
                const std::string secondCandidate = secondLine + word;
                if (MeasureUiText(secondCandidate, fontSize) <= static_cast<int>(maxWidth)) {
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
