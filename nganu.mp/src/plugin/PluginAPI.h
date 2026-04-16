#pragma once

#include <stddef.h>

#ifdef _WIN32
  #define PLUGIN_CALL __cdecl
#else
  #define PLUGIN_CALL
#endif

/* Forward declare the LuaJIT state so plugins can register C functions
   without including Lua headers through this public ABI. */
struct lua_State;
typedef int (*SCRIPT_FUNCTION)(lua_State* state);

typedef struct PluginAPI {
    void (PLUGIN_CALL *register_function)(const char* name, SCRIPT_FUNCTION fn);
    void (PLUGIN_CALL *on_player_connect)(void (PLUGIN_CALL *callback)(int playerid));
    void (PLUGIN_CALL *on_player_disconnect)(void (PLUGIN_CALL *callback)(int playerid, int reason));
    void (PLUGIN_CALL *send_packet)(void* peer, const void* data, size_t len, int channel);
    void (PLUGIN_CALL *log)(int level, const char* message);
    void (PLUGIN_CALL *on_receive)(void (PLUGIN_CALL *callback)(int playerid, const void* data, size_t len));
} PluginAPI;

/* Symbols every plugin must export (extern "C") */
#ifdef __cplusplus
extern "C" {
#endif
    typedef void        (*plugin_load_t)(PluginAPI* api);
    typedef void        (*plugin_unload_t)(void);
    typedef const char* (*plugin_name_t)(void);
    typedef int         (*plugin_version_t)(void);
#ifdef __cplusplus
}
#endif
