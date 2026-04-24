# Repository Guidelines

## Project Structure & Module Organization
This repository contains two CMake-based C++ projects:

- `nganu.mp/`: multiplayer server, Lua scripting, plugins, and shared game assets.
- `nganu.game/`: `raylib` client prototype.

Source files live under each module's `src/` directory. Server data is in `nganu.mp/assets/`, runtime Lua content in `nganu.mp/scripts/`, and the sample plugin in `nganu.mp/plugins/example_plugin/`. Map authoring now uses Tiled `.tmx` files under `nganu.mp/assets/maps/`. Treat `*/build/` and generated binaries in `nganu.mp/bin/` as local build output, not hand-edited source.

## Build, Test, and Development Commands
Run commands from the relevant module directory:

- `cmake -B build -DCMAKE_BUILD_TYPE=Release`: configure a target.
- `cmake --build build --parallel`: compile it.
- `./build/nganu-game`: run the client.
- `./bin/game-server`: start the multiplayer server.
- `./bin/game-server --test`: run the server smoke test with `server.test.cfg` when present.

The server requires Lua 5.5 development files. The client requires `raylib`.

## Coding Style & Naming Conventions
Use C++17 and keep existing style intact: 4-space indentation, opening braces on the same line, and standard library includes grouped near the top. Use `PascalCase` for types (`NetworkClient`), `camelCase` for functions and locals (`testMode`, `screenWidth`), and lowercase filenames matching the current module pattern. Prefer small, focused headers and source files instead of large mixed-purpose units.

## Testing Guidelines
There is no broad automated test suite yet. Validate changes with:

- `./bin/game-server --test` for server startup and config smoke coverage.
- Manual runs of `nganu.game` for gameplay changes.
- Validate touched `.tmx` maps in Tiled when the change affects map authoring data.
- Focused checks of touched assets, maps, scripts, and plugin loading paths.

When adding tests, keep them near the affected module and name them after the feature or subsystem being verified.

## Commit & Pull Request Guidelines
Git history is minimal (`Initial commit`), so use short imperative commit subjects such as `Add map spawn validation` or `Fix atlas selection bounds`. Keep commits scoped to one concern. Pull requests should describe the affected module, list build/test commands run, and include screenshots for UI or map changes when relevant. Link related issues or task notes when available.
