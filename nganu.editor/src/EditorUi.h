#pragma once

#include "raylib.h"

#include <string>
#include <vector>

namespace EditorUi {

enum class Mode {
    Atlas = 0,
    Map = 1
};

enum class MapSection {
    Tile = 0,
    Region = 1,
    Object = 2
};

enum class MapTool {
    Paint = 0,
    Erase = 1,
    Blocked = 2,
    Water = 3,
    Spawn = 4,
    Object = 5
};

struct Layout {
    Rectangle topBar {};
    Rectangle canvas {};
    Rectangle sidePanel {};
};

struct AtlasPanelState {
    std::string domain;
    std::string atlasName;
    std::string grid;
    std::string ref;
    std::string status;
    bool hasSelection = false;
    Texture2D texture {};
    Rectangle source {};
};

struct MapPanelState {
    std::string mapId;
    std::string worldName;
    std::string size;
    MapSection section = MapSection::Tile;
    MapTool tool = MapTool::Paint;
    std::vector<std::string> layers;
    int activeLayer = 0;
    std::string brushRef;
    int blockedCount = 0;
    int waterCount = 0;
    std::string spawn;
    std::string objectKind;
    std::vector<std::string> objects;
    int selectedObject = -1;
    std::string mapFile;
    std::string status;
};

Color BackgroundColor();
Color PanelColor();
Color AccentColor();
Color AccentSoft();
Color InkColor();
Color MapGridColor();
Color BlockedColor();
Color WaterColor();

int FontSize(int px);
Layout BuildLayout(int screenWidth, int screenHeight);
void DrawTopBar(const Font& font, const Layout& layout, Mode mode);
void DrawAtlasPanel(const Font& font, Rectangle bounds, const AtlasPanelState& state);
void DrawMapPanel(const Font& font, Rectangle bounds, const MapPanelState& state);
void DrawTextClipped(const Font& font, const std::string& text, Rectangle bounds, int size, Color color);
float DrawTextWrapped(const Font& font, const std::string& text, Rectangle bounds, int size, Color color, int maxLines = 0);

} // namespace EditorUi
