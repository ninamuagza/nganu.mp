# nganu.mp — Game Server

A minimal, production-ready game server built from scratch in C/C++17.

## Features

- **ENet UDP networking** — reliable packet delivery over UDP with a simple opcode-based protocol.
- **Core MMO basics** — movement broadcast, global chat, and admin stdin commands (`list players`, `kick <id>`).
- **LuaJIT runtime** — load and execute Lua scripts with the LuaJIT 2.1 VM.
- **Dynamic plugin system** — load `.so`/`.dll` plugins at startup with a stable C ABI.
- **Configuration file** — simple `key=value` config (`server.cfg`).
- **Single-threaded** — deterministic tick loop at 100 Hz.

## Building

```bash
cd nganu.mp
pkg-config --modversion luajit   # should print the installed LuaJIT version
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Binaries are placed in `nganu.mp/bin/`.

## Running

```bash
cd nganu.mp
./bin/game-server          # start the server with server.cfg
./bin/game-server --test   # smoke test using server.test.cfg when present
```

Normal startup expects a valid Lua script at `scripts/main.lua` when `gamemode`
is set in `server.cfg`. Startup fails fast if the Lua file cannot be loaded or
if a required plugin fails to load.

## Verification

Use the cross-module checklist before merging:

- [`../VERIFICATION_CHECKLIST.md`](../VERIFICATION_CHECKLIST.md)

## Protocol Notes

For opcode and manifest/asset flow contracts:

- [`../PROTOCOL_ASSET_FLOW.md`](../PROTOCOL_ASSET_FLOW.md)

## Project Structure

```
nganu.mp/
├── src/                  # Server source code
│   ├── main.cpp
│   ├── core/             # Server, Runtime, Logger
│   ├── network/          # ENet wrapper, Packet protocol
│   ├── plugin/           # Plugin loader and ABI
│   └── script/           # LuaJIT runtime and built-in bindings
├── vendor/
│   └── enet/             # ENet networking library (vendored)
├── plugins/              # Plugin sources and built plugins
│   └── example_plugin/
├── scripts/              # Lua script sources
│   └── main.lua
├── assets/
│   └── maps/             # Server-side map source files (.map)
├── server.cfg            # Server configuration
├── server.test.cfg       # Minimal smoke-test configuration
└── CMakeLists.txt
```

## Configuration

Edit `server.cfg`:

```
port=7777
maxclients=100
tickrate=100
gamemode=main.lua
plugins=example_plugin
loglevel=info
maxpacketsize=1024
chatmaxlen=200
```

## License

See vendor directories and your installed LuaJIT package for third-party licenses.
