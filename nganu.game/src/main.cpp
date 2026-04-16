#include "Game.h"

#include "raylib.h"

int main() {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "nganu.game");
    SetTargetFPS(60);

    Game game;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        game.Update(dt);

        BeginDrawing();
        game.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
