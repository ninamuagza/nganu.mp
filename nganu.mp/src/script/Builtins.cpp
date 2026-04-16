#include "script/Builtins.h"
#include "core/Server.h"
#include "network/Packet.h"
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

static Logger*  g_logger  = nullptr;
static Network* g_network = nullptr;
static Server*  g_server  = nullptr;
static auto g_startTime = std::chrono::steady_clock::now();

static int l_print(lua_State* state) {
    const int nargs = lua_gettop(state);
    std::string line;

    for (int i = 1; i <= nargs; ++i) {
        size_t len = 0;
        const char* text = lua_tolstring(state, i, &len);
        if (!text) {
            text = luaL_typename(state, i);
            len = std::strlen(text);
        }
        if (i > 1) line += "\t";
        if (text) line.append(text, len);
    }

    if (g_logger) g_logger->info("Script", "%s", line.c_str());
    return 0;
}

static int l_SendPlayerMessage(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    size_t msgLen = 0;
    const char* msg = luaL_checklstring(state, 2, &msgLen);

    if (g_network) {
        std::vector<uint8_t> pkt(2 + msgLen);
        pkt[0] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
        pkt[1] = static_cast<uint8_t>(GameStateType::SERVER_TEXT);
        std::memcpy(pkt.data() + 2, msg, msgLen);

        void* peer = g_network->peerForPlayer(playerid);
        if (peer) {
            g_network->sendPacket(peer, pkt.data(), pkt.size(), 0);
        }
    }

    return 0;
}

static int l_BroadcastMessage(lua_State* state) {
    size_t msgLen = 0;
    const char* msg = luaL_checklstring(state, 1, &msgLen);

    if (g_network && msgLen > 0) {
        std::vector<uint8_t> pkt(2 + msgLen);
        pkt[0] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
        pkt[1] = static_cast<uint8_t>(GameStateType::SERVER_TEXT);
        std::memcpy(pkt.data() + 2, msg, msgLen);
        g_network->broadcastPacket(pkt.data(), pkt.size(), 0);
    }

    return 0;
}

static int l_BroadcastMapMessage(lua_State* state) {
    size_t mapLen = 0;
    const char* mapId = luaL_checklstring(state, 1, &mapLen);
    size_t msgLen = 0;
    const char* msg = luaL_checklstring(state, 2, &msgLen);

    if (!g_server || !g_network || mapLen == 0 || msgLen == 0) {
        return 0;
    }

    std::vector<uint8_t> pkt(2 + msgLen);
    pkt[0] = static_cast<uint8_t>(PacketOpcode::GAME_STATE);
    pkt[1] = static_cast<uint8_t>(GameStateType::SERVER_TEXT);
    std::memcpy(pkt.data() + 2, msg, msgLen);

    const std::string targetMap(mapId, mapLen);
    for (int playerid : g_network->playerIds()) {
        if (g_server->playerMapId(playerid) != targetMap) continue;
        void* peer = g_network->peerForPlayer(playerid);
        if (!peer) continue;
        g_network->sendPacket(peer, pkt.data(), pkt.size(), 0);
    }

    return 0;
}

static int l_SetPlayerSpawnPosition(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    const float x = static_cast<float>(luaL_checknumber(state, 2));
    const float y = static_cast<float>(luaL_checknumber(state, 3));

    if (g_server) {
        g_server->setPlayerPosition(playerid, x, y);
    }

    return 0;
}

static int l_GetPlayerName(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    if (!g_server) {
        lua_pushliteral(state, "");
        return 1;
    }

    const std::string name = g_server->playerName(playerid);
    lua_pushlstring(state, name.c_str(), name.size());
    return 1;
}

static int l_GetPlayerPosition(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    if (!g_server) {
        lua_pushnumber(state, 0.0);
        lua_pushnumber(state, 0.0);
        return 2;
    }

    const auto pos = g_server->getPlayerPosition(playerid);
    lua_pushnumber(state, pos.x);
    lua_pushnumber(state, pos.y);
    return 2;
}

static int l_SetPlayerName(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    size_t len = 0;
    const char* name = luaL_checklstring(state, 2, &len);

    bool ok = false;
    if (g_server) {
        ok = g_server->setPlayerName(playerid, std::string(name, len), true);
    }

    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
}

static int l_GetLastPlayerText(lua_State* state) {
    if (!g_server) {
        lua_pushliteral(state, "");
        return 1;
    }

    const std::string& text = g_server->lastPlayerText();
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
}

static int l_GetTickCount(lua_State* state) {
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_startTime).count();
    lua_pushinteger(state, static_cast<lua_Integer>(ms));
    return 1;
}

static int l_GetMapId(lua_State* state) {
    if (!g_server) {
        lua_pushliteral(state, "");
        return 1;
    }
    const std::string& id = g_server->map().mapId();
    lua_pushlstring(state, id.c_str(), id.size());
    return 1;
}

static int l_GetPlayerMapId(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    if (!g_server) {
        lua_pushliteral(state, "");
        return 1;
    }
    const std::string mapId = g_server->playerMapId(playerid);
    lua_pushlstring(state, mapId.c_str(), mapId.size());
    return 1;
}

static int l_GetMapProperty(lua_State* state) {
    size_t len = 0;
    const char* key = luaL_checklstring(state, 1, &len);
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const auto value = g_server->map().property(std::string(key, len));
    if (!value.has_value()) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, value->c_str(), value->size());
    return 1;
}

static int l_GetPlayerMapProperty(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    size_t len = 0;
    const char* key = luaL_checklstring(state, 2, &len);
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const MapData* map = g_server->mapForPlayer(playerid);
    if (!map) {
        lua_pushnil(state);
        return 1;
    }
    const auto value = map->property(std::string(key, len));
    if (!value.has_value()) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, value->c_str(), value->size());
    return 1;
}

static int l_GetMapObjectId(lua_State* state) {
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 1));
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = g_server->map().objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, object->id.c_str(), object->id.size());
    return 1;
}

static int l_GetPlayerMapObjectId(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 2));
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const MapData* map = g_server->mapForPlayer(playerid);
    if (!map) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = map->objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, object->id.c_str(), object->id.size());
    return 1;
}

static int l_GetMapObjectKind(lua_State* state) {
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 1));
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = g_server->map().objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, object->kind.c_str(), object->kind.size());
    return 1;
}

static int l_GetPlayerMapObjectKind(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 2));
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const MapData* map = g_server->mapForPlayer(playerid);
    if (!map) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = map->objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, object->kind.c_str(), object->kind.size());
    return 1;
}

static int l_GetMapObjectProperty(lua_State* state) {
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 1));
    size_t len = 0;
    const char* key = luaL_checklstring(state, 2, &len);
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = g_server->map().objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    auto it = object->properties.find(std::string(key, len));
    if (it == object->properties.end()) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, it->second.c_str(), it->second.size());
    return 1;
}

static int l_GetPlayerMapObjectProperty(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    const int objectIndex = static_cast<int>(luaL_checkinteger(state, 2));
    size_t len = 0;
    const char* key = luaL_checklstring(state, 3, &len);
    if (!g_server) {
        lua_pushnil(state);
        return 1;
    }
    const MapData* map = g_server->mapForPlayer(playerid);
    if (!map) {
        lua_pushnil(state);
        return 1;
    }
    const auto* object = map->objectByIndex(objectIndex);
    if (!object) {
        lua_pushnil(state);
        return 1;
    }
    auto it = object->properties.find(std::string(key, len));
    if (it == object->properties.end()) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, it->second.c_str(), it->second.size());
    return 1;
}

static int l_SetPlayerObjective(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    size_t len = 0;
    const char* text = luaL_checklstring(state, 2, &len);
    if (g_server) {
        g_server->sendObjectiveText(playerid, std::string(text, len));
    }
    return 0;
}

static int l_TransferPlayerMap(lua_State* state) {
    const int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    size_t mapLen = 0;
    const char* mapId = luaL_checklstring(state, 2, &mapLen);
    const float x = static_cast<float>(luaL_optnumber(state, 3, 0.0));
    const float y = static_cast<float>(luaL_optnumber(state, 4, 0.0));
    bool ok = false;
    if (g_server) {
        std::optional<Server::PlayerPosition> pos;
        if (lua_gettop(state) >= 4) {
            pos = Server::PlayerPosition {x, y};
        }
        ok = g_server->transferPlayerToMap(playerid, std::string(mapId, mapLen), pos);
    }
    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
}

void RegisterBuiltinFunctions(LuaRuntime& runtime, Logger& logger, Network& network, Server& server) {
    g_logger = &logger;
    g_network = &network;
    g_server = &server;
    g_startTime = std::chrono::steady_clock::now();

    runtime.registerFunction("print", l_print);
    runtime.registerFunction("SendPlayerMessage", l_SendPlayerMessage);
    runtime.registerFunction("BroadcastMessage", l_BroadcastMessage);
    runtime.registerFunction("BroadcastMapMessage", l_BroadcastMapMessage);
    runtime.registerFunction("SetPlayerSpawnPosition", l_SetPlayerSpawnPosition);
    runtime.registerFunction("GetPlayerName", l_GetPlayerName);
    runtime.registerFunction("GetPlayerPosition", l_GetPlayerPosition);
    runtime.registerFunction("SetPlayerName", l_SetPlayerName);
    runtime.registerFunction("GetLastPlayerText", l_GetLastPlayerText);
    runtime.registerFunction("GetTickCount", l_GetTickCount);
    runtime.registerFunction("GetMapId", l_GetMapId);
    runtime.registerFunction("GetPlayerMapId", l_GetPlayerMapId);
    runtime.registerFunction("GetMapProperty", l_GetMapProperty);
    runtime.registerFunction("GetPlayerMapProperty", l_GetPlayerMapProperty);
    runtime.registerFunction("GetMapObjectId", l_GetMapObjectId);
    runtime.registerFunction("GetPlayerMapObjectId", l_GetPlayerMapObjectId);
    runtime.registerFunction("GetMapObjectKind", l_GetMapObjectKind);
    runtime.registerFunction("GetPlayerMapObjectKind", l_GetPlayerMapObjectKind);
    runtime.registerFunction("GetMapObjectProperty", l_GetMapObjectProperty);
    runtime.registerFunction("GetPlayerMapObjectProperty", l_GetPlayerMapObjectProperty);
    runtime.registerFunction("SetPlayerObjective", l_SetPlayerObjective);
    runtime.registerFunction("TransferPlayerMap", l_TransferPlayerMap);
}
