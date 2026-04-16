#include "plugin/PluginManager.h"
#include <cstring>

#ifndef _WIN32
  #include <dlfcn.h>
#endif

/* File-static pointer so the C-ABI trampolines can reach the current
   PluginManager instance.  Only one PluginManager exists at a time. */
static PluginManager* g_pm = nullptr;

/* ------------------------------------------------------------------ */
/* Static trampolines                                                 */
/* ------------------------------------------------------------------ */
void PLUGIN_CALL PluginManager::apiRegisterFunction(const char* name, SCRIPT_FUNCTION fn) {
    if (g_pm && g_pm->scriptRegisterFn_) g_pm->scriptRegisterFn_(name, fn);
}

void PLUGIN_CALL PluginManager::apiOnPlayerConnect(void (PLUGIN_CALL *cb)(int)) {
    if (g_pm) g_pm->connectCbs_.push_back(cb);
}

void PLUGIN_CALL PluginManager::apiOnPlayerDisconnect(void (PLUGIN_CALL *cb)(int, int)) {
    if (g_pm) g_pm->disconnectCbs_.push_back(cb);
}

void PLUGIN_CALL PluginManager::apiSendPacket(void* peer, const void* data, size_t len, int channel) {
    if (g_pm && g_pm->sendPacketFn_) g_pm->sendPacketFn_(peer, data, len, channel);
}

void PLUGIN_CALL PluginManager::apiLog(int level, const char* message) {
    if (!g_pm) return;
    LogLevel lv = LogLevel::INFO;
    switch (level) {
        case 0: lv = LogLevel::DEBUG; break;
        case 1: lv = LogLevel::INFO;  break;
        case 2: lv = LogLevel::WARN;  break;
        case 3: lv = LogLevel::ERR; break;
        default: break;
    }
    g_pm->logger_.log(lv, "Plugin", "%s", message);
}

void PLUGIN_CALL PluginManager::apiOnReceive(void (PLUGIN_CALL *cb)(int, const void*, size_t)) {
    if (g_pm) g_pm->receiveCbs_.push_back(cb);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
PluginManager::PluginManager(Logger& logger) : logger_(logger) {
    g_pm = this;
    buildAPI();
}

PluginManager::~PluginManager() {
    unloadAll();
    if (g_pm == this) g_pm = nullptr;
}

void PluginManager::buildAPI() {
    api_.register_function     = &apiRegisterFunction;
    api_.on_player_connect     = &apiOnPlayerConnect;
    api_.on_player_disconnect  = &apiOnPlayerDisconnect;
    api_.send_packet           = &apiSendPacket;
    api_.log                   = &apiLog;
    api_.on_receive            = &apiOnReceive;
}

void PluginManager::setScriptRegisterCallback(ScriptRegisterFn fn) {
    scriptRegisterFn_ = std::move(fn);
}

void PluginManager::setSendPacketFn(void (PLUGIN_CALL *fn)(void*, const void*, size_t, int)) {
    sendPacketFn_ = fn;
}

bool PluginManager::loadPlugin(const std::string& name, const std::string& directory) {
#ifdef _WIN32
    std::string path = directory + "/" + name + ".dll";
    PluginHandle h = LoadLibraryA(path.c_str());
    if (!h) {
        logger_.error("PluginManager", "Failed to load plugin: %s", path.c_str());
        return false;
    }
    auto fn_load    = (plugin_load_t)   GetProcAddress(h, "plugin_load");
    auto fn_unload  = (plugin_unload_t) GetProcAddress(h, "plugin_unload");
    auto fn_name    = (plugin_name_t)   GetProcAddress(h, "plugin_name");
    auto fn_version = (plugin_version_t)GetProcAddress(h, "plugin_version");
#else
    std::string path = directory + "/lib" + name + ".so";
    PluginHandle h = dlopen(path.c_str(), RTLD_NOW);
    if (!h) {
        /* Try without lib prefix */
        path = directory + "/" + name + ".so";
        h = dlopen(path.c_str(), RTLD_NOW);
    }
    if (!h) {
        logger_.error("PluginManager", "Failed to load plugin: %s (%s)", name.c_str(), dlerror());
        return false;
    }
    auto fn_load    = (plugin_load_t)   dlsym(h, "plugin_load");
    auto fn_unload  = (plugin_unload_t) dlsym(h, "plugin_unload");
    auto fn_name    = (plugin_name_t)   dlsym(h, "plugin_name");
    auto fn_version = (plugin_version_t)dlsym(h, "plugin_version");
#endif

    if (!fn_load || !fn_unload || !fn_name || !fn_version) {
        logger_.error("PluginManager", "Plugin '%s' missing required exports", name.c_str());
#ifdef _WIN32
        FreeLibrary(h);
#else
        dlclose(h);
#endif
        return false;
    }

    LoadedPlugin p;
    p.handle     = h;
    p.fn_load    = fn_load;
    p.fn_unload  = fn_unload;
    p.fn_name    = fn_name;
    p.fn_version = fn_version;
    p.filename   = name;

    p.fn_load(&api_);
    logger_.info("PluginManager", "Loaded plugin: %s v%d", p.fn_name(), p.fn_version());
    plugins_.push_back(std::move(p));
    return true;
}

void PluginManager::unloadAll() {
    /* Reverse order */
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        if (it->fn_unload) {
            it->fn_unload();
            logger_.info("PluginManager", "Unloaded plugin: %s", it->filename.c_str());
        }
        it->close();
    }
    plugins_.clear();
    connectCbs_.clear();
    disconnectCbs_.clear();
    receiveCbs_.clear();
}

void PluginManager::firePlayerConnect(int playerid) {
    for (auto& cb : connectCbs_) cb(playerid);
}

void PluginManager::firePlayerDisconnect(int playerid, int reason) {
    for (auto& cb : disconnectCbs_) cb(playerid, reason);
}

void PluginManager::fireReceive(int playerid, const void* data, size_t len) {
    for (auto& cb : receiveCbs_) cb(playerid, data, len);
}
