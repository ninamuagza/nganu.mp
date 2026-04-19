#include "EditorUi.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace EditorUi {
namespace {

constexpr float kUiFontScale = 0.7f;
constexpr float kPanelPad = 18.0f;

struct ScissorGuard {
    explicit ScissorGuard(Rectangle bounds) {
        BeginScissorMode(static_cast<int>(std::floor(bounds.x)),
            static_cast<int>(std::floor(bounds.y)),
            std::max(0, static_cast<int>(std::ceil(bounds.width))),
            std::max(0, static_cast<int>(std::ceil(bounds.height))));
    }

    ~ScissorGuard() {
        EndScissorMode();
    }
};

float TextWidth(const Font& font, const std::string& text, int size) {
    return MeasureTextEx(font, text.c_str(), static_cast<float>(size), 0.0f).x;
}

std::string Ellipsize(const Font& font, const std::string& text, float maxWidth, int size) {
    if (text.empty() || TextWidth(font, text, size) <= maxWidth) {
        return text;
    }

    const std::string suffix = "...";
    if (TextWidth(font, suffix, size) > maxWidth) {
        return "";
    }

    std::string result = text;
    while (!result.empty() && TextWidth(font, result + suffix, size) > maxWidth) {
        result.pop_back();
    }
    return result + suffix;
}

std::vector<std::string> WrapLines(const Font& font, const std::string& text, float maxWidth, int size, int maxLines) {
    std::vector<std::string> lines;
    std::string word;
    std::string line;
    std::istringstream stream(text);

    auto pushWord = [&](const std::string& token) {
        if (token.empty()) {
            return;
        }

        std::string next = line.empty() ? token : line + " " + token;
        if (TextWidth(font, next, size) <= maxWidth) {
            line = next;
            return;
        }

        if (!line.empty()) {
            lines.push_back(line);
            line.clear();
        }

        std::string chunk;
        for (char ch : token) {
            std::string test = chunk + ch;
            if (!chunk.empty() && TextWidth(font, test, size) > maxWidth) {
                lines.push_back(chunk);
                chunk.clear();
            }
            chunk.push_back(ch);
        }
        line = chunk;
    };

    while (stream >> word) {
        pushWord(word);
        if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
            break;
        }
    }

    if (!line.empty() && (maxLines <= 0 || static_cast<int>(lines.size()) < maxLines)) {
        lines.push_back(line);
    }

    if (maxLines > 0 && static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<size_t>(maxLines));
    }
    if (maxLines > 0 && static_cast<int>(lines.size()) == maxLines && TextWidth(font, lines.back(), size) > maxWidth) {
        lines.back() = Ellipsize(font, lines.back(), maxWidth, size);
    }

    return lines;
}

void DrawLabel(const Font& font, const std::string& text, float x, float& y, float width) {
    DrawTextClipped(font, text, Rectangle {x, y, width, 18.0f}, FontSize(17), Fade(RAYWHITE, 0.68f));
    y += 22.0f;
}

void DrawValue(const Font& font, const std::string& text, float x, float& y, float width, int px = 18, Color color = RAYWHITE) {
    DrawTextClipped(font, text, Rectangle {x, y, width, static_cast<float>(FontSize(px) + 4)}, FontSize(px), color);
    y += static_cast<float>(FontSize(px) + 10);
}

void DrawRow(const Font& font, Rectangle row, const std::string& label, bool active) {
    DrawRectangleRounded(row, 0.15f, 6, active ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.055f));
    DrawTextClipped(font, label, Rectangle {row.x + 8.0f, row.y + 5.0f, row.width - 16.0f, row.height - 6.0f}, FontSize(16), RAYWHITE);
}

void DrawSectionTabs(const Font& font, Rectangle bounds, MapSection active) {
    const char* labels[] = {"Tile", "Region", "Object"};
    const float tabWidth = (bounds.width - 12.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        Rectangle tab {bounds.x + (i * (tabWidth + 6.0f)), bounds.y, tabWidth, bounds.height};
        DrawRectangleRounded(tab, 0.18f, 6, i == static_cast<int>(active) ? Fade(AccentColor(), 0.24f) : Fade(WHITE, 0.06f));
        DrawTextClipped(font, labels[i], Rectangle {tab.x + 6.0f, tab.y + 7.0f, tab.width - 12.0f, tab.height - 8.0f}, FontSize(15), RAYWHITE);
    }
}

void DrawFooter(const Font& font, Rectangle content, const std::string& mapFile, const std::string& status) {
    const float footerHeight = 132.0f;
    const float footerTop = content.y + content.height - footerHeight;
    DrawRectangleRec(Rectangle {content.x, footerTop - 12.0f, content.width, 1.0f}, Fade(WHITE, 0.08f));
    float y = footerTop;
    DrawLabel(font, "Map File", content.x, y, content.width);
    DrawValue(font, mapFile.empty() ? "(unsaved)" : mapFile, content.x, y, content.width, 16);
    DrawTextWrapped(font, "Tab section | Ctrl+S save | Ctrl+N new", Rectangle {content.x, y, content.width, 34.0f}, FontSize(14), RAYWHITE, 2);
    y += 38.0f;
    DrawTextWrapped(font, "PgUp/PgDn map | F1 atlas | F2 map", Rectangle {content.x, y, content.width, 34.0f}, FontSize(14), RAYWHITE, 2);
    DrawTextClipped(font, status, Rectangle {content.x, content.y + content.height - 24.0f, content.width, 22.0f}, FontSize(16), Fade(RAYWHITE, 0.62f));
}

} // namespace

Color BackgroundColor() { return Color {238, 232, 220, 255}; }
Color PanelColor() { return Color {34, 43, 47, 255}; }
Color AccentColor() { return Color {219, 142, 71, 255}; }
Color AccentSoft() { return Color {241, 191, 140, 255}; }
Color InkColor() { return Color {20, 24, 26, 255}; }
Color MapGridColor() { return Color {40, 51, 58, 70}; }
Color BlockedColor() { return Color {184, 83, 67, 180}; }
Color WaterColor() { return Color {64, 139, 196, 170}; }

int FontSize(int px) {
    return std::max(10, static_cast<int>(std::round(static_cast<float>(px) * kUiFontScale)));
}

Layout BuildLayout(int screenWidth, int screenHeight) {
    const float width = static_cast<float>(std::max(640, screenWidth));
    const float height = static_cast<float>(std::max(420, screenHeight));
    const float topHeight = 60.0f;
    const float margin = 18.0f;
    const float gap = 16.0f;
    const float sideWidth = std::clamp(width * 0.245f, 292.0f, 360.0f);
    const float contentTop = topHeight + 16.0f;
    const float contentHeight = std::max(160.0f, height - contentTop - margin);
    const float canvasWidth = std::max(180.0f, width - margin - sideWidth - gap - margin);

    return Layout {
        Rectangle {0.0f, 0.0f, width, topHeight},
        Rectangle {margin, contentTop, canvasWidth, contentHeight},
        Rectangle {margin + canvasWidth + gap, contentTop, sideWidth, contentHeight}
    };
}

void DrawTextClipped(const Font& font, const std::string& text, Rectangle bounds, int size, Color color) {
    if (bounds.width <= 0.0f || bounds.height <= 0.0f || text.empty()) {
        return;
    }

    ScissorGuard clip(bounds);
    const std::string fitted = Ellipsize(font, text, bounds.width, size);
    DrawTextEx(font, fitted.c_str(), Vector2 {bounds.x, bounds.y}, static_cast<float>(size), 0.0f, color);
}

float DrawTextWrapped(const Font& font, const std::string& text, Rectangle bounds, int size, Color color, int maxLines) {
    if (bounds.width <= 0.0f || bounds.height <= 0.0f || text.empty()) {
        return 0.0f;
    }

    const float lineHeight = static_cast<float>(size) + 4.0f;
    const int visibleLines = maxLines > 0 ? maxLines : std::max(1, static_cast<int>(std::floor(bounds.height / lineHeight)));
    std::vector<std::string> lines = WrapLines(font, text, bounds.width, size, visibleLines);

    ScissorGuard clip(bounds);
    float y = bounds.y;
    for (std::string& line : lines) {
        if (y + lineHeight > bounds.y + bounds.height + 0.1f) {
            break;
        }
        DrawTextEx(font, line.c_str(), Vector2 {bounds.x, y}, static_cast<float>(size), 0.0f, color);
        y += lineHeight;
    }
    return y - bounds.y;
}

void DrawTopBar(const Font& font, const Layout& layout, Mode mode) {
    DrawRectangleRec(layout.topBar, PanelColor());
    ScissorGuard clip(layout.topBar);

    const float x = 18.0f;
    DrawTextClipped(font, "nganu.editor", Rectangle {x, 14.0f, 150.0f, 34.0f}, FontSize(28), RAYWHITE);
    DrawTextClipped(font, "F1 Atlas | F2 Map", Rectangle {190.0f, 20.0f, 140.0f, 24.0f}, FontSize(18), AccentSoft());
    DrawTextClipped(font, mode == Mode::Atlas ? "Atlas Picker" : "Map Editor", Rectangle {340.0f, 20.0f, layout.topBar.width - 358.0f, 24.0f}, FontSize(18), Fade(RAYWHITE, 0.8f));
}

void DrawAtlasPanel(const Font& font, Rectangle bounds, const AtlasPanelState& state) {
    DrawRectangleRounded(bounds, 0.04f, 8, PanelColor());
    Rectangle content {bounds.x + kPanelPad, bounds.y + kPanelPad, bounds.width - (kPanelPad * 2.0f), bounds.height - (kPanelPad * 2.0f)};
    ScissorGuard panelClip(bounds);

    float y = content.y;
    DrawLabel(font, "Domain", content.x, y, content.width);
    DrawValue(font, state.domain, content.x, y, content.width, 28, AccentSoft());
    DrawLabel(font, "Atlas", content.x, y, content.width);
    DrawValue(font, state.atlasName.empty() ? "(none)" : state.atlasName, content.x, y, content.width, 18);
    DrawLabel(font, "Grid", content.x, y, content.width);
    DrawValue(font, state.grid, content.x, y, content.width, 18);

    const float previewHeight = std::clamp(bounds.height * 0.23f, 120.0f, 170.0f);
    Rectangle preview {content.x, y + 8.0f, content.width, previewHeight};
    DrawRectangleRounded(preview, 0.07f, 8, Fade(WHITE, 0.06f));
    DrawTextClipped(font, "Selection", Rectangle {preview.x + 12.0f, preview.y + 10.0f, preview.width - 24.0f, 22.0f}, FontSize(18), Fade(RAYWHITE, 0.75f));
    if (state.hasSelection && state.texture.id > 0 && state.source.width > 0.0f && state.source.height > 0.0f) {
        const float scale = std::min((preview.width - 24.0f) / state.source.width, (preview.height - 52.0f) / state.source.height);
        const Rectangle dest {
            preview.x + (preview.width * 0.5f) - ((state.source.width * scale) * 0.5f),
            preview.y + 34.0f + ((preview.height - 52.0f) * 0.5f) - ((state.source.height * scale) * 0.5f),
            state.source.width * scale,
            state.source.height * scale
        };
        DrawTexturePro(state.texture, state.source, dest, Vector2 {}, 0.0f, WHITE);
        DrawRectangleLinesEx(dest, 2.0f, Fade(AccentSoft(), 0.9f));
    }
    y = preview.y + preview.height + 18.0f;

    DrawLabel(font, "Atlas Ref", content.x, y, content.width);
    Rectangle refBox {content.x, y, content.width, 64.0f};
    DrawRectangleRounded(refBox, 0.05f, 8, Fade(WHITE, 0.06f));
    DrawTextWrapped(font, state.ref, Rectangle {refBox.x + 10.0f, refBox.y + 10.0f, refBox.width - 20.0f, refBox.height - 16.0f}, FontSize(16), AccentSoft(), 2);
    y += 82.0f;

    DrawLabel(font, "Controls", content.x, y, content.width);
    const char* controls[] = {
        "Tab domain | Q/E atlas",
        "Wheel zoom | RMB/MMB drag pan",
        "WASD pan | Arrows move select",
        "Shift+Arrows resize | C copy",
        "Switch to Map mode with F2"
    };
    for (const char* line : controls) {
        if (y > content.y + content.height - 56.0f) {
            break;
        }
        DrawTextWrapped(font, line, Rectangle {content.x, y, content.width, 22.0f}, FontSize(15), line == controls[4] ? AccentSoft() : RAYWHITE, 1);
        y += 22.0f;
    }

    DrawTextClipped(font, state.status, Rectangle {content.x, content.y + content.height - 24.0f, content.width, 22.0f}, FontSize(16), Fade(RAYWHITE, 0.62f));
}

void DrawMapPanel(const Font& font, Rectangle bounds, const MapPanelState& state) {
    DrawRectangleRounded(bounds, 0.04f, 8, PanelColor());
    Rectangle content {bounds.x + kPanelPad, bounds.y + kPanelPad, bounds.width - (kPanelPad * 2.0f), bounds.height - (kPanelPad * 2.0f)};
    ScissorGuard panelClip(bounds);

    float y = content.y;
    DrawLabel(font, "Map", content.x, y, content.width);
    DrawValue(font, state.mapId, content.x, y, content.width, 27, AccentSoft());
    DrawValue(font, state.worldName, content.x, y, content.width, 17);
    DrawValue(font, state.size, content.x, y, content.width, 17);

    DrawLabel(font, "Section", content.x, y, content.width);
    DrawSectionTabs(font, Rectangle {content.x, y, content.width, 28.0f}, state.section);
    y += 40.0f;

    DrawLabel(font, "Tool", content.x, y, content.width);
    std::vector<std::pair<std::string, bool>> rows;
    if (state.section == MapSection::Tile) {
        rows = {{"Paint", state.tool == MapTool::Paint}, {"Erase", state.tool == MapTool::Erase}};
    } else if (state.section == MapSection::Region) {
        rows = {{"Blocked", state.tool == MapTool::Blocked}, {"Water", state.tool == MapTool::Water}, {"Spawn", state.tool == MapTool::Spawn}};
    } else {
        rows = {
            {"Prop", state.objectKind == "prop"},
            {"NPC", state.objectKind == "npc"},
            {"Portal", state.objectKind == "portal"},
            {"Trigger", state.objectKind == "trigger"},
            {"Region", state.objectKind == "region"}
        };
    }
    for (const auto& row : rows) {
        DrawRow(font, Rectangle {content.x, y, content.width, 24.0f}, row.first, row.second);
        y += 28.0f;
    }
    y += 8.0f;

    const float footerTop = content.y + content.height - 144.0f;
    const float bodyBottom = std::max(y, footerTop - 10.0f);
    Rectangle body {content.x, y, content.width, std::max(0.0f, bodyBottom - y)};
    ScissorGuard bodyClip(body);

    if (state.section == MapSection::Tile) {
        DrawLabel(font, "Layers", content.x, y, content.width);
        for (int i = 0; i < static_cast<int>(state.layers.size()) && y + 24.0f <= body.y + body.height; ++i) {
            DrawRow(font, Rectangle {content.x, y, content.width, 22.0f}, state.layers[static_cast<size_t>(i)], i == state.activeLayer);
            y += 26.0f;
        }
        y += 8.0f;
        if (y + 92.0f <= body.y + body.height) {
            DrawLabel(font, "Brush", content.x, y, content.width);
            Rectangle brush {content.x, y, content.width, 64.0f};
            DrawRectangleRounded(brush, 0.05f, 8, Fade(WHITE, 0.06f));
            DrawTextWrapped(font, state.brushRef.empty() ? "(pick in Atlas mode)" : state.brushRef,
                Rectangle {brush.x + 10.0f, brush.y + 10.0f, brush.width - 20.0f, brush.height - 16.0f},
                FontSize(16), AccentSoft(), 2);
            y += 78.0f;
            DrawTextWrapped(font, "Paint memakai atlas brush aktif.", Rectangle {content.x, y, content.width, 34.0f}, FontSize(14), Fade(RAYWHITE, 0.72f), 2);
        }
    } else if (state.section == MapSection::Region) {
        DrawLabel(font, "Region Tools", content.x, y, content.width);
        DrawValue(font, "Blocked cells: " + std::to_string(state.blockedCount), content.x, y, content.width, 16);
        DrawValue(font, "Water cells: " + std::to_string(state.waterCount), content.x, y, content.width, 16);
        DrawValue(font, "Spawn: " + state.spawn, content.x, y, content.width, 16);
        DrawTextWrapped(font, "Use B/V/G and click on map.", Rectangle {content.x, y, content.width, 34.0f}, FontSize(15), AccentSoft(), 2);
    } else {
        DrawLabel(font, "Object Kind", content.x, y, content.width);
        DrawValue(font, state.objectKind, content.x, y, content.width, 18, AccentSoft());
        DrawLabel(font, "Brush", content.x, y, content.width);
        DrawTextWrapped(font, state.brushRef.empty() ? "(pick in Atlas mode)" : state.brushRef,
            Rectangle {content.x, y, content.width, 48.0f}, FontSize(16), AccentSoft(), 2);
        y += 54.0f;
        DrawLabel(font, "Objects", content.x, y, content.width);
        for (int i = 0; i < static_cast<int>(state.objects.size()) && y + 22.0f <= body.y + body.height; ++i) {
            DrawRow(font, Rectangle {content.x, y, content.width, 20.0f}, state.objects[static_cast<size_t>(i)], i == state.selectedObject);
            y += 24.0f;
        }
    }

    DrawFooter(font, content, state.mapFile, state.status);
}

} // namespace EditorUi
