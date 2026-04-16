#include "script/LuaRuntime.h"

LuaRuntime::LuaRuntime(Logger& logger) : logger_(logger) {}

LuaRuntime::~LuaRuntime() {
    unload();
}

void LuaRuntime::registerFunction(const char* name, lua_CFunction fn) {
    if (!name || !fn) return;

    for (auto& entry : functions_) {
        if (entry.name == name) {
            entry.fn = fn;
            if (state_) {
                lua_pushcfunction(state_, fn);
                lua_setglobal(state_, name);
            }
            return;
        }
    }

    functions_.push_back({name, fn});
    if (state_) {
        lua_pushcfunction(state_, fn);
        lua_setglobal(state_, name);
    }
}

bool LuaRuntime::load(const std::string& filepath) {
    unload();

    state_ = luaL_newstate();
    if (!state_) {
        logger_.error("LuaRuntime", "Failed to create Lua state");
        return false;
    }

    luaL_openlibs(state_);
    installRegisteredFunctions();

    if (luaL_loadfile(state_, filepath.c_str()) != 0) {
        const std::string context = "Failed to load " + filepath;
        logLuaError(context.c_str());
        unload();
        return false;
    }

    if (lua_pcall(state_, 0, 0, 0) != 0) {
        const std::string context = "Failed to execute " + filepath;
        logLuaError(context.c_str());
        unload();
        return false;
    }

    logger_.info("LuaRuntime", "Loaded Lua script: %s", filepath.c_str());
    return true;
}

void LuaRuntime::unload() {
    if (!state_) return;
    lua_close(state_);
    state_ = nullptr;
    logger_.info("LuaRuntime", "Lua state closed");
}

bool LuaRuntime::callFunction(const char* name) {
    return callFunctionImpl(name, nullptr, 0);
}

bool LuaRuntime::callFunction(const char* name, int arg1) {
    const int args[] = {arg1};
    return callFunctionImpl(name, args, 1);
}

bool LuaRuntime::callFunction(const char* name, int arg1, int arg2) {
    const int args[] = {arg1, arg2};
    return callFunctionImpl(name, args, 2);
}

bool LuaRuntime::callFunction(const char* name, int arg1, int arg2, int arg3) {
    const int args[] = {arg1, arg2, arg3};
    return callFunctionImpl(name, args, 3);
}

void LuaRuntime::installRegisteredFunctions() {
    if (!state_) return;
    for (const auto& entry : functions_) {
        lua_pushcfunction(state_, entry.fn);
        lua_setglobal(state_, entry.name.c_str());
    }
}

bool LuaRuntime::callFunctionImpl(const char* name, const int* args, int nargs) {
    if (!state_ || !name) return false;

    lua_getglobal(state_, name);
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 1);
        return false;
    }

    for (int i = 0; i < nargs; ++i) {
        lua_pushinteger(state_, args[i]);
    }

    if (lua_pcall(state_, nargs, 0, 0) != 0) {
        logLuaError(name);
        return false;
    }
    return true;
}

void LuaRuntime::logLuaError(const char* context) {
    const char* msg = state_ ? lua_tostring(state_, -1) : nullptr;
    logger_.error("LuaRuntime", "%s: %s", context, msg ? msg : "unknown Lua error");
    if (state_ && lua_gettop(state_) > 0) {
        lua_pop(state_, 1);
    }
}
