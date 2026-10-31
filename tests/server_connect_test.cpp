// Regression test for the connect-to-server screen: one client = one player.
//
// Bug: clicking CONNECT (or a discovered server) repeatedly created a new
// player on the server for every click ("Player 1..9 connected" from a single
// client) and the screen stayed on "CONNECTING..." because the session was not
// pumped while the menu was open.
//
// Needs a server on 127.0.0.1:32123 (the Makefile target starts it) and a GL
// context (xvfb-run). Run from a directory that contains assets/.

#include "core/game.hpp"
#include "ui/menu.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
const char* const kHost = "127.0.0.1";
const uint16_t kPort = 32123;

/// The status row's text on the connect page.
std::string statusValue(Game& game) {
    menu::Page* page = game.debugMenuPage();
    if (!page) return "";
    for (const menu::Row& row : page->rows) {
        if (row.label == "STATUS") return row.value;
    }
    return "";
}

/// The row with this label on the connect page (nullptr when absent).
menu::Row* findRow(Game& game, const std::string& label) {
    menu::Page* page = game.debugMenuPage();
    if (!page) return nullptr;
    for (menu::Row& row : page->rows) {
        if (row.label == label) return &row;
    }
    return nullptr;
}

/// Activate the row with `label` like a mouse click on it would.
void clickRow(Game& game, const std::string& label) {
    if (!game.debugMenuSelect(label)) {
        std::printf("FAIL: row '%s' not selectable\n", label.c_str());
        ++failures;
        return;
    }
    menu::processInput(*game.debugMenuView(), game.windowHandle(), menu::MouseState(),
                       [] {
                           menu::MenuInput in;   // ENTER only
                           in.pressed = [](int k) { return k == GLFW_KEY_ENTER || k == GLFW_KEY_KP_ENTER; };
                           in.held = [](int k) { return k == GLFW_KEY_ENTER || k == GLFW_KEY_KP_ENTER; };
                           in.isModifier = [](int) { return false; };
                           return in;
                       }());
}

/// Run real frames (network pump + menu input, as the game does) until `pred`
/// is true, or give up after ~10 s.
template <typename Pred>
bool pumpUntil(Game& game, Pred pred) {
    for (int i = 0; i < 600; ++i) {
        game.debugPumpFrame();
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return false;
}
} // namespace

int main() {
    Game game(800, 600);
    game.init();

    game.debugOpenMenu("server");
    game.serverAddressForTest(kHost, kPort);
    CHECK(statusValue(game) == "NOT CONNECTED");

    // ── First click: handshake starts and the screen shows CONNECTING ───────
    clickRow(game, "CONNECT");
    CHECK(game.isConnectingToServer());
    CHECK(statusValue(game) == "CONNECTING...");

    // While the handshake is in flight the CONNECT row is gone (only
    // DISCONNECT is offered), so a second click cannot register a player.
    CHECK(findRow(game, "CONNECT") == nullptr);
    CHECK(findRow(game, "DISCONNECT") != nullptr);
    // The address fields cannot be edited mid-handshake either.
    CHECK(findRow(game, "IP: " + std::string(kHost)) == nullptr ||
          !findRow(game, "IP: " + std::string(kHost))->enabled);

    // ── The session is pumped while the menu is open: it must turn CONNECTED
    //    by itself, without leaving/reopening the menu. ─────────────────────
    const bool connected = pumpUntil(game, [&] { return game.isConnectedToServer(); });
    CHECK(connected);
    if (!connected) {
        std::printf("STATUS = %s\n", statusValue(game).c_str());
    }
    CHECK(statusValue(game) == "CONNECTED");

    // ── Repeated "clicks" on the connected server must not create players ──
    for (int i = 0; i < 5; ++i) {
        // Same address: a retry of the existing session, not a new one.
        CHECK(game.connectToServer(kHost, kPort));
        game.updateNetwork();
        CHECK(game.isConnectedToServer());
    }
    // Another server is refused while this one is active.
    CHECK(!game.connectToServer(kHost, static_cast<uint16_t>(kPort + 1)));
    CHECK(game.isConnectedToServer());
    CHECK(!game.getServerConnectError().empty());
    CHECK(statusValue(game) == "CONNECTED");   // still on the first server

    // The server must only ever have seen this one player: a second player
    // would answer the broadcast state with JOIN/LEAVE traffic, but the
    // authoritative check is the server console (checked by the Makefile
    // target that greps its log). Here: still one session, one client id.
    CHECK(game.debugClientId() != 0);

    // ── DISCONNECT clears the session; the screen offers CONNECT again ──────
    clickRow(game, "DISCONNECT");
    CHECK(!game.isConnectedToServer());
    CHECK(!game.isConnectingToServer());
    CHECK(statusValue(game) == "NOT CONNECTED");
    CHECK(findRow(game, "CONNECT") != nullptr);

    std::printf(failures ? "SERVER CONNECT TEST: %d FAILURES\n" : "SERVER CONNECT TEST: ALL OK\n", failures);
    return failures ? 1 : 0;
}
