#pragma once

#include "script/LuaRuntime.h"
#include "core/Logger.h"
#include "network/Network.h"

class Server;

void RegisterBuiltinFunctions(LuaRuntime& runtime, Logger& logger, class Network& network, Server& server);
