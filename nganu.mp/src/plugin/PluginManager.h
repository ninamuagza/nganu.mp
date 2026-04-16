#pragma once

#include <string>
#include <vector>
#include <functional>
#include "plugin/PluginAPI.h"
#include "core/Logger.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  typedef HMODULE PluginHandle;
#else
  #include <dlfcn.h>
  typedef void* PluginHandle;
#endif

struct LoadedPlugin {
    PluginHandle        handle = nullptr;
    plugin_load_t       fn_load = nullptr;
    plugin_unload_t     fn_unload = nullptr;
    plugin_name_t       fn_name = nullptr;
    plugin_version_t    fn_version = nullptr;
    std::string         filename;

    ~LoadedPlugin() { close(); }
    LoadedPlugin() = default;
    LoadedPlugin(LoadedPlugin&& o) noexcept
        : handle(o.handle), fn_load(o.fn_load), fn_unload(o.fn_unload),
          fn_name(o.fn_name), fn_version(o.fn_version), filename(std::move(o.filename))
    { o.handle = nullptr; }
    LoadedPlugin& operator=(LoadedPlugin&& o) noexcept {
        if (this != &o) { close(); handle = o.handle; fn_load = o.fn_load;
            fn_unload = o.fn_unload; fn_name = o.fn_name; fn_version = o.fn_version;
            filename = std::move(o.filename); o.handle = nullptr; }
        return *this;
    }
    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;

    void close() {
        if (!handle) return;
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        handle = nullptr;
    }
};

class PluginManager {
public:
    using ScriptRegisterFn       = std::function<void(const char*, SCRIPT_FUNCTION)>;
    using PlayerConnectCallback  = void (PLUGIN_CALL *)(int);
    using PlayerDisconnectCallback = void (PLUGIN_CALL *)(int, int);
    using ReceiveCallback        = void (PLUGIN_CALL *)(int, const void*, size_t);

    explicit PluginManager(Logger& logger);
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    void setScriptRegisterCallback(ScriptRegisterFn fn);
    void setSendPacketFn(void (PLUGIN_CALL *fn)(void*, const void*, size_t, int));

    bool loadPlugin(const std::string& name, const std::string& directory);
    void unloadAll();

    void firePlayerConnect(int playerid);
    void firePlayerDisconnect(int playerid, int reason);
    void fireReceive(int playerid, const void* data, size_t len);

    const std::vector<LoadedPlugin>& plugins() const { return plugins_; }

    std::vector<PlayerConnectCallback>&    connectCallbacks()    { return connectCbs_; }
    std::vector<PlayerDisconnectCallback>& disconnectCallbacks() { return disconnectCbs_; }
    std::vector<ReceiveCallback>&          receiveCallbacks()    { return receiveCbs_; }

private:
    Logger& logger_;
    std::vector<LoadedPlugin> plugins_;
    PluginAPI api_{};

    ScriptRegisterFn scriptRegisterFn_;
    void (PLUGIN_CALL *sendPacketFn_)(void*, const void*, size_t, int) = nullptr;

    std::vector<PlayerConnectCallback>    connectCbs_;
    std::vector<PlayerDisconnectCallback> disconnectCbs_;
    std::vector<ReceiveCallback>          receiveCbs_;

    void buildAPI();

    /* Static trampolines wired into PluginAPI — call through the singleton-like approach
       via a file-static pointer set in PluginManager.cpp. */
    static void PLUGIN_CALL apiRegisterFunction(const char* name, SCRIPT_FUNCTION fn);
    static void PLUGIN_CALL apiOnPlayerConnect(void (PLUGIN_CALL *cb)(int));
    static void PLUGIN_CALL apiOnPlayerDisconnect(void (PLUGIN_CALL *cb)(int, int));
    static void PLUGIN_CALL apiSendPacket(void* peer, const void* data, size_t len, int channel);
    static void PLUGIN_CALL apiLog(int level, const char* message);
    static void PLUGIN_CALL apiOnReceive(void (PLUGIN_CALL *cb)(int, const void*, size_t));
};
