#include "EditorApp.h"

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 900, "nganu.editor");
    SetTargetFPS(144);

    EditorApp editor;
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
