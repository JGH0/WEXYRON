# Local Multiplayer Setup Guide

## Overview
This OpenGL game supports local multiplayer through LAN (Local Area Network) connections. Multiple players can connect and play together on the same network.

## Connection Types Supported

### 1. Local Network (LAN) - Primary Method
- **WiFi Network**: All devices connected to the same WiFi router
- **Ethernet Network**: Devices connected via network cables to the same router/switch
- **Direct Ethernet**: Cable-to-cable connection between two devices (crossover cable or modern auto-MDIX)

### 2. Additional Methods
- **Mobile Hotspot**: One device creates a WiFi hotspot, others connect to it
- **Network Switch**: Multiple devices connected through a network switch

## Quick Start

### Method 1: Automatic LAN Discovery (Recommended)
1. **Start Server**:
   ```bash
   cd OpenGl-Game
   ./build/bin/server [port]  # Default port: 7777
   ```

2. **Start Client**:
   ```bash
   cd OpenGl-Game
   ./build/bin/game
   ```

3. **In Game Client**:
   - Launch the game
   - Navigate to "Connect to Server" in the main menu
   - Press 'R' to scan for local servers automatically
   - Select discovered server from the list
   - Press ENTER to connect

### Method 2: Manual Connection
1. **Find Server IP**:
   - On server machine, run: `hostname -I` (Linux) or `ipconfig` (Windows)
   - Note the local IP address (e.g., 192.168.1.100)

2. **Connect Manually**:
   - In client game, go to "Connect to Server"
   - Enter server IP address (e.g., 192.168.1.100)
   - Enter port number (default: 7777)
   - Press ENTER to connect

## Network Setup Examples

### Home WiFi Network
```
Router (192.168.1.1)
  |
  +-- Computer A (Server) - 192.168.1.100
  |
  +-- Computer B (Client) - 192.168.1.101
  |
  +-- Computer C (Client) - 192.168.1.102
```

### Direct Cable Connection
```
Computer A (Server) ---- Ethernet Cable ---- Computer B (Client)
IP: 192.168.1.100                         IP: 192.168.1.101
```

### Mobile Hotspot
```
Phone A (Hotspot) - 192.168.43.1
  |
  +-- Computer A (Server) - 192.168.43.100
  |
  +-- Computer B (Client) - 192.168.43.101
```

## Firewall Configuration

If connection fails, check firewall settings:

### Linux (ufw)
```bash
sudo ufw allow 7777/udp
# Or temporarily disable for testing:
sudo ufw disable
```

### Windows Firewall
1. Open Windows Defender Firewall
2. Allow "Cube Game" through firewall
3. Or add UDP port 7777 manually

## Troubleshooting

### Connection Issues
1. **Check Network Connectivity**:
   ```bash
   ping [server_ip]  # From client to server
   ```

2. **Verify Server is Running**:
   - Server should show "Server started on port 7777"
   - Server shows "Player X connected" when client joins

3. **Check Port Availability**:
   ```bash
   netstat -uln | grep 7777  # Linux
   ```

### Performance Tips
- Use wired Ethernet for best performance
- WiFi works but may have latency
- Avoid using VPNs during gameplay

## Game Controls in Multiplayer

### Movement
- **W/A/S/D**: Move forward/left/backward/right
- **Mouse**: Look around
- **Space**: Jump
- **Left Mouse**: Shoot

### Weapons
- **1**: Pistol
- **2**: Pump Shotgun
- **3**: Sniper Rifle
- **4**: Grenade
- **5**: Flashbang
- **G**: Toggle between Grenade/Flashbang
- **Right Mouse (hold)**: Scope with the sniper rifle

### Other
- **ESC**: Menu/Disconnect
- **U**: Unstuck (move to safe position)

## Server Commands

The server runs in terminal mode and supports:
- **Ctrl+C**: Graceful shutdown
- **Port Selection**: `./build/bin/server 7778` for different port

## Server bots

The dedicated server can fill a match with bots. They are configured in
`server_config.json` (read by `./runServer.sh`):

```json
{
  "bots": {
    "count": 4,
    "accuracy": 0.7
  }
}
```

- `count` – number of bots to spawn (`0` disables them). The server caps it at
  8, because clients keep up to 16 remote bodies and real players need slots too.
- `accuracy` – hit chance from `0.0` (almost never hits) to `1.0` (very
  accurate). It also affects how much a bot misses grenade/flashbang throws.

Bots are full server-controlled players: they walk and collide with the map,
fight with pistols and shotguns, throw grenades and flashbangs, and respawn in
deathmatch. In round-based modes (team vs team / bomb defusal) a dead bot stays
dead until the next round starts, exactly like a human player. They always
follow the world and game mode of the players (bots are spread over the
connected players, so two occupied maps both get bots). A flashbang blinds
nearby bots; while blind they wander randomly and cannot aim or shoot. Bots
fight in deathmatch, team vs team, and bomb defusal; in the round modes they
push a bomb site, plant the bomb when they get there, and defenders go to a
planted bomb and defuse it.

Bomb planting, defusing and the fuse are authoritative on the dedicated server
(`ServerBombManager`), not just in the client. Clients send an intent when E is
pressed; the server validates it, advances the timers, broadcasts the bomb
state to every player on the map, and applies the explosion damage.

Round results are server-authoritative and CS-style:

- **Attackers win** when the bomb detonates, when every defender is eliminated,
  or when the fuse burns out without a defuse.
- **Defenders win** when they defuse the bomb, when every attacker is eliminated
  before a plant, or when the plant-time clock (`ROUND_TIME`, default 115 s)
  expires without a plant.
- After a plant, killing all attackers does **not** end the round: the
  defenders still have to defuse the bomb. The plant-time clock stops once the
  bomb is planted and the fuse takes over.

Overrides, highest precedence first: command line (`./build/bin/server 7777
name -1 6 0.8`), environment (`BOTS_COUNT`, `BOTS_ACCURACY`),
`server_config.json`, built-in default (`0` bots).

## Multiple players on one PC

Several game instances can run on the same PC and join the same server at the
same time. How many are allowed is a server setting in `server_config.json`:

```json
{ "server": { "maxPlayersPerMachine": 2 } }
```

`-1` = unlimited (default), `0` = nobody, `N` = up to `N` instances per PC.
All instances on one PC share the PC's machine id, so they are counted together
even if they connect from different addresses. A client that is over the limit
shows `FAILED` on the connect screen with the reason; the server logs
`Connection rejected: machine limit reached`.

## Advanced Configuration

### Custom Ports
Both server and client support custom ports:
```bash
# Server
./build/bin/server 8888

# Client (in menu, enter port 8888)
```

### Multiple Servers
You can run multiple servers on different ports for different games:
- Server 1: Port 7777 (Deathmatch)
- Server 2: Port 7778 (Team Play)

## Security Notes

- Only works on local networks (not over internet)
- No authentication required for LAN play
- UDP protocol for low latency
- Firewall may block connections

## Supported Platforms

- **Linux**: Full support (tested on Ubuntu, Debian, Arch)
- **Windows**: Should work (may need firewall configuration)
- **macOS**: Should work (may need firewall configuration)

## Performance Requirements

- **Minimum**: 100 Mbps LAN connection
- **Recommended**: 1 Gbps Ethernet
- **WiFi**: 802.11n or better recommended
- **Latency**: < 50ms for best experience
