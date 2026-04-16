#pragma once

#include <string>
#include <unordered_map>
#include "core/Logger.h"

/* Runtime reads and holds the server.cfg configuration. */
class Runtime {
public:
    explicit Runtime(Logger& logger);

    /* Parse the config file.  Returns false if the file can't be read. */
    bool loadConfig(const std::string& path);

    /* Getters with defaults */
    std::string getString(const std::string& key, const std::string& def = "") const;
    int         getInt(const std::string& key, int def = 0) const;

    const std::unordered_map<std::string, std::string>& all() const { return cfg_; }

private:
    Logger& logger_;
    std::unordered_map<std::string, std::string> cfg_;
};
