#include "plugin/PluginAPI.h"
#include <lua.hpp>
#include <cstdio>

static PluginAPI* g_api = nullptr;

/* ------------------------------------------------------------------ */
/* Lua function: GivePlayerMoney(playerid, amount)                    */
/* ------------------------------------------------------------------ */
static int l_GivePlayerMoney(lua_State* state) {
    int playerid = static_cast<int>(luaL_checkinteger(state, 1));
    int amount   = static_cast<int>(luaL_checkinteger(state, 2));
    char buf[256];
    snprintf(buf, sizeof(buf), "[ExamplePlugin] GivePlayerMoney(%d, %d)", playerid, amount);
    if (g_api && g_api->log) g_api->log(1, buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Player connect callback                                            */
/* ------------------------------------------------------------------ */
static void PLUGIN_CALL onPlayerConnect(int playerid) {
    char buf[128];
    snprintf(buf, sizeof(buf), "[ExamplePlugin] Player %d connected", playerid);
    if (g_api && g_api->log) g_api->log(1, buf);
}

/* ------------------------------------------------------------------ */
/* Exported functions                                                 */
/* ------------------------------------------------------------------ */
extern "C" {

void plugin_load(PluginAPI* api) {
    g_api = api;
    if (api->log) api->log(1, "[ExamplePlugin] loaded");
    if (api->register_function) {
        api->register_function("GivePlayerMoney", l_GivePlayerMoney);
    }
    if (api->on_player_connect) api->on_player_connect(onPlayerConnect);
}

void plugin_unload() {
    if (g_api && g_api->log) g_api->log(1, "[ExamplePlugin] unloaded");
    g_api = nullptr;
}

const char* plugin_name() {
    return "ExamplePlugin";
}

int plugin_version() {
    return 1;
}

} /* extern "C" */
