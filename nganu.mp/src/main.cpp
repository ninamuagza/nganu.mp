#include "core/Server.h"
#include <cstring>
#include <cstdlib>
#include <fstream>

int main(int argc, char* argv[]) {
    bool testMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0) {
            testMode = true;
        }
    }

    Server server;
    const char* cfgPath = "server.cfg";

    if (testMode) {
        std::ifstream testCfg("server.test.cfg");
        if (testCfg.good()) {
            cfgPath = "server.test.cfg";
        }
        return server.runTest(cfgPath);
    }

    if (!server.startup(cfgPath)) {
        return 1;
    }

    server.run();
    server.shutdown();
    return 0;
}
