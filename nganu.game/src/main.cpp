#include "Game.h"

#include "raylib.h"

int main() {
#if defined(PLATFORM_ANDROID)
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "nganu.game");
#else
    const int screenWidth = 1280;
    const int screenHeight = 720;

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "nganu.game");
    const int monitor = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
    SetWindowPosition(0, 0);
    ToggleFullscreen();
    SetTargetFPS(60);
#endif

    Game game;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        game.Update(dt);

        BeginDrawing();
        game.Draw();
        EndDrawing();
    }

    game.Shutdown();
    CloseWindow();
    return 0;
}
