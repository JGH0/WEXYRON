/**
 * @file game_server_menu.cpp
 * @brief "Connect to server" screen (menu framework view + text fields)
 *
 * Layout:
 *   MANUAL CONNECTION
 *     IP: <typed address>            (TYPING while it is being edited)
 *     PORT: <typed port>
 *   LAN SERVERS                      R rescans the network
 *     <server name>   <ip>:<port>    (one row per server found)
 *   CONNECT / DISCONNECT
 *   BACK
 *
 * Number keys and '.' edit the IP, digits the port; BACKSPACE deletes.
 * The LAN discovery socket lives in game_server.cpp; the rows are rebuilt from
 * its state by refreshServerRows().
 */

#include "game.hpp"
#include "network/network.hpp"
#include <algorithm>
#include <iostream>

namespace {

const char* const kDefaultAddress = "192.168.1.50";

std::string statusText(const Game& game) {
    if (game.isConnectedToServer()) return "CONNECTED";
    if (game.isConnectingToServer()) return "CONNECTING...";
    return "NOT CONNECTED";
}

} // namespace

void Game::openServerConnect() {
    enteringServerAddress = false;
    serverConnectSelection = 0;

    menu::Page blank("CONNECT TO SERVER", "LOCAL NETWORK PLAY");
    openMenuView(serverMenu_, std::move(blank));
    serverMenu_.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "CONNECT"},
                          {"R", "SCAN"}, {"ESC", "BACK"}};
    serverMenu_.onClose = [this]() { closeMenuToGame(); };
    refreshServerRows();
}

void Game::refreshServerRows() {
    menu::Page page("CONNECT TO SERVER", "LOCAL NETWORK PLAY");

    const std::string ipText = serverAddress.empty() ? std::string(kDefaultAddress) : serverAddress;

    // ── Manual connection ─────────────────────────────────────────────────
    page.add(menu::separator("MANUAL CONNECTION"));
    page.add(menu::action("IP: " + ipText, [this]() {
        serverConnectSelection = 0;
    }, "Type the address (0-9 and .)"));
    if (serverConnectSelection == 0) page.rows.back().value = "<";

    page.add(menu::action("PORT: " + std::to_string(serverPort), [this]() {
        serverConnectSelection = 1;
    }, "Type the port (0-9)"));
    if (serverConnectSelection == 1) page.rows.back().value = "<";

    // ── Discovered servers ────────────────────────────────────────────────
    page.add(menu::separator("LAN SERVERS"));
    if (discoveredServers.empty()) {
        page.add(menu::info("NO SERVERS FOUND", "", "Press R to scan the network again"));
    } else {
        const int count = std::min(4, static_cast<int>(discoveredServers.size()));
        for (int i = 0; i < count; ++i) {
            const DiscoveredServer& s = discoveredServers[i];
            const std::string name = s.name.empty() ? std::string("SERVER") : s.name;
            const std::string ip = s.ip;
            const uint16_t port = s.port;
            page.add(menu::action(name, [this, ip, port]() {
                serverAddress = ip;
                serverPort = port;
                connectToServer(ip, port);
                refreshServerRows();
            }, "Join this server"));
            page.rows.back().value = s.ip + ":" + std::to_string(s.port);
        }
    }

    // ── Actions ───────────────────────────────────────────────────────────
    page.add(menu::separator("ACTIONS"));
    if (isConnectedToServer()) {
        page.add(menu::action("DISCONNECT", [this]() {
            disconnectFromServer();
            refreshServerRows();
        }, "Leave the current server"));
    } else {
        page.add(menu::action("CONNECT", [this]() {
            const std::string address = serverAddress.empty() ? std::string(kDefaultAddress) : serverAddress;
            connectToServer(address, serverPort);
            refreshServerRows();
        }, "Connect with the address above"));
    }
    page.add(menu::action("BACK", [this]() { closeMenuToGame(); }, "Return to the main menu"));
    page.add(menu::info("STATUS", statusText(*this), ""));

    menu::replaceCurrent(serverMenu_, std::move(page));
}

void Game::renderServerConnect() {
    menu::draw(serverMenu_, renderer, window);
}
