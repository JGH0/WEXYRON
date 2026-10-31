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
 *
 * Connection lifecycle (one session per client):
 *   NOT CONNECTED  -> CONNECT            sends the handshake
 *   CONNECTING...  -> CONNECT is disabled (the handshake is retried in the
 *                    background by NetworkClient; clicking again must not open
 *                    a second player on the server) - DISCONNECT cancels
 *   CONNECTED      -> CONNECT becomes DISCONNECT; another server can only be
 *                    joined after leaving the current one
 * The status row is driven by the real session state, and failed attempts or
 * a dropped server are reported on the STATUS row.
 */

#include "game.hpp"
#include "network/network.hpp"
#include <algorithm>
#include <iostream>
#include <vector>

namespace {

const char* const kDefaultAddress = "192.168.1.50";

/// "ip:port" of the current session, or an empty string when there is none.
std::string sessionLabel(const Game& game) {
    if (!game.isConnectedToServer() && !game.isConnectingToServer()) return std::string();
    return game.getServerAddress() + ":" + std::to_string(game.getServerPort());
}

std::string statusText(const Game& game) {
    if (game.isConnectedToServer()) return "CONNECTED";
    if (game.isConnectingToServer()) return "CONNECTING...";
    if (!game.getServerConnectError().empty()) return "FAILED";
    return "NOT CONNECTED";
}

std::string statusHelp(const Game& game) {
    if (game.isConnectedToServer()) return "Connected to " + sessionLabel(game);
    if (game.isConnectingToServer()) return "Waiting for the server's answer...";
    if (!game.getServerConnectError().empty()) return game.getServerConnectError();
    return "Connect to a server or scan the network";
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
    const int previousSelection = serverMenu_.stack.selection();

    // Address fields are only editable while no session exists: changing them
    // mid-handshake would leave the screen describing the wrong server.
    const bool sessionActive = hasServerSession();

    // ── Manual connection ─────────────────────────────────────────────────
    const std::string ipText = serverAddress.empty() ? std::string(kDefaultAddress) : serverAddress;

    page.add(menu::separator("MANUAL CONNECTION"));
    page.add(menu::action("IP: " + ipText, [this]() {
        serverConnectSelection = 0;
    }, "Type the address (0-9 and .)"));
    if (serverConnectSelection == 0) page.rows.back().value = "<";
    page.rows.back().enabled = !sessionActive;

    page.add(menu::action("PORT: " + std::to_string(serverPort), [this]() {
        serverConnectSelection = 1;
    }, "Type the port (0-9)"));
    if (serverConnectSelection == 1) page.rows.back().value = "<";
    page.rows.back().enabled = !sessionActive;

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
            // A server we are already on/connecting to cannot be joined again.
            const bool isCurrent = sessionActive && ip == getServerAddress() && port == getServerPort();
            const bool connectedHere = isCurrent && isConnectedToServer();
            page.add(menu::action(name, [this, ip, port]() {
                serverAddress = ip;
                serverPort = port;
                connectToServer(ip, port);
                refreshServerRows();
            }, connectedHere ? "Already connected to this server"
                             : (isCurrent ? "Connecting to this server" : "Join this server")));
            page.rows.back().value = s.ip + ":" + std::to_string(s.port);
            page.rows.back().enabled = !sessionActive;
        }
    }

    // ── Actions ───────────────────────────────────────────────────────────
    page.add(menu::separator("ACTIONS"));
    if (sessionActive) {
        // One connection at a time: the only action while connected/connecting
        // is to leave (or cancel the attempt).
        page.add(menu::action("DISCONNECT", [this]() {
            disconnectFromServer();
            serverConnectError_.clear();
            refreshServerRows();
        }, isConnectingToServer() ? "Cancel the connection attempt" : "Leave the current server"));
    } else {
        page.add(menu::action("CONNECT", [this]() {
            const std::string address = serverAddress.empty() ? std::string(kDefaultAddress) : serverAddress;
            connectToServer(address, serverPort);
            refreshServerRows();
        }, "Connect with the address above"));
    }
    page.add(menu::action("BACK", [this]() { closeMenuToGame(); }, "Return to the main menu"));
    page.add(menu::info("STATUS", statusText(*this), statusHelp(*this)));

    menu::replaceCurrent(serverMenu_, std::move(page));
    // Replacing the page re-picks the first row; keep the keyboard selection
    // where it was so a periodic refresh does not move it under the player.
    if (previousSelection >= 0) serverMenu_.stack.setSelection(previousSelection);
}

void Game::renderServerConnect() {
    menu::draw(serverMenu_, renderer, window);
}
