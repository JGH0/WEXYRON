// Discord "registered game" helpers are not used: the game talks to the
// Discord desktop client over the local IPC socket, which works without
// registering the executable system-wide. These no-ops keep discord_rpc.cpp
// linkable without shipping the platform-specific registry helpers.
#include "discord_register.h"

extern "C" void Discord_Register(const char* /*applicationId*/, const char* /*command*/) {}
extern "C" void Discord_RegisterSteamGame(const char* /*applicationId*/, const char* /*steamId*/) {}
