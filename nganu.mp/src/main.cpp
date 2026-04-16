#include "core/Server.h"
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path resolveConfigPath(const char* fileName) {
    const std::filesystem::path fromCwd = std::filesystem::current_path() / fileName;
    if (std::filesystem::exists(fromCwd)) {
        return fromCwd;
    }

    const std::filesystem::path fromRepoRoot = std::filesystem::path(NGANU_SERVER_ROOT) / fileName;
    if (std::filesystem::exists(fromRepoRoot)) {
        return fromRepoRoot;
    }

    return fromCwd;
}
}

int main(int argc, char* argv[]) {
    bool testMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0) {
            testMode = true;
        }
    }

    Server server;
    std::filesystem::path cfgPath = resolveConfigPath("server.cfg");

    if (testMode) {
        const std::filesystem::path testCfgPath = resolveConfigPath("server.test.cfg");
        if (std::filesystem::exists(testCfgPath)) {
            cfgPath = testCfgPath;
        }
        if (cfgPath.has_parent_path()) {
            std::filesystem::current_path(cfgPath.parent_path());
        }
        return server.runTest(cfgPath.string());
    }

    if (cfgPath.has_parent_path()) {
        std::filesystem::current_path(cfgPath.parent_path());
    }
    if (!server.startup(cfgPath.string())) {
        return 1;
    }

    server.run();
    server.shutdown();
    return 0;
}
