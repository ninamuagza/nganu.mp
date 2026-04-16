#pragma once

#include <string>
#include <vector>
#include <lua.hpp>
#include "core/Logger.h"

class LuaRuntime {
public:
    explicit LuaRuntime(Logger& logger);
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    void registerFunction(const char* name, lua_CFunction fn);

    bool load(const std::string& filepath);
    void unload();

    bool callFunction(const char* name);
    bool callFunction(const char* name, int arg1);
    bool callFunction(const char* name, int arg1, int arg2);
    bool callFunction(const char* name, int arg1, int arg2, int arg3);

    bool loaded() const { return state_ != nullptr; }
    lua_State* state() { return state_; }

private:
    struct RegisteredFunction {
        std::string  name;
        lua_CFunction fn = nullptr;
    };

    Logger& logger_;
    lua_State* state_ = nullptr;
    std::vector<RegisteredFunction> functions_;

    void installRegisteredFunctions();
    bool callFunctionImpl(const char* name, const int* args, int nargs);
    void logLuaError(const char* context);
};
