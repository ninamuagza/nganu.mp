#include "core/Runtime.h"
#include <fstream>
#include <algorithm>

Runtime::Runtime(Logger& logger) : logger_(logger) {}

bool Runtime::loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        logger_.error("Runtime", "Cannot open config file: %s", path.c_str());
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        /* Trim leading whitespace */
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        /* Skip comments */
        if (line[0] == '#') continue;

        /* Find '=' */
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            logger_.warn("Runtime", "Ignoring malformed line %d in %s", lineNum, path.c_str());
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        /* Trim key and value */
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);

        cfg_[key] = val;
        logger_.debug("Runtime", "Config: %s = %s", key.c_str(), val.c_str());
    }

    logger_.info("Runtime", "Loaded config from %s (%zu entries)", path.c_str(), cfg_.size());
    return true;
}

std::string Runtime::getString(const std::string& key, const std::string& def) const {
    auto it = cfg_.find(key);
    return (it != cfg_.end()) ? it->second : def;
}

int Runtime::getInt(const std::string& key, int def) const {
    auto it = cfg_.find(key);
    if (it == cfg_.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}
