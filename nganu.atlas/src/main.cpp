#include "AtlasEditor.h"

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 900, "nganu.atlas");
    SetTargetFPS(144);

    AtlasEditor editor;
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        editor.Update(dt);

        BeginDrawing();
        editor.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
