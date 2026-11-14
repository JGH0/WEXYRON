# Docker Setup for Cube Game Server

This guide explains how to run the Cube Game server using Docker and Docker Compose.

## Prerequisites

- Docker installed on your system
- Docker Compose installed (for docker-compose method)

## Quick Start

### Using Docker Compose (Recommended)

1. **Configure the server**:
   ```bash
   cp .env.example .env
   # Edit .env to customize your server settings
   ```

2. **Build and start the server**:
   ```bash
   docker-compose up -d
   ```

3. **View logs**:
   ```bash
   docker-compose logs -f
   ```

4. **Stop the server**:
   ```bash
   docker-compose down
   ```

### Using Docker directly

1. **Build the image**:
   ```bash
   docker build -t cube-game-server .
   ```

2. **Run the container**:
   ```bash
   docker run -d \
     -p 7777:7777/udp \
     -e SERVER_NAME="My Server" \
     -e MAX_PLAYERS=32 \
     -v $(pwd)/server_config.json:/app/server_config.json:ro \
     -v $(pwd)/assets:/app/assets:ro \
     --name cube-game-server \
     cube-game-server
   ```

## Configuration

### Environment Variables

The server can be configured using environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `SERVER_NAME` | Server display name | "Cube Game Server" |
| `SERVER_PORT` | Server port | 7777 |
| `MAX_PLAYERS` | Maximum connected players | 16 |
| `MAX_PLAYERS_PER_MACHINE` | Players allowed from one machine (same PC). `-1` = unlimited, `0` = nobody, `N` = up to N instances per PC | from `server_config.json` |
| `SERVER_REGION` | Server region | "US-East" |
| `SERVER_DESCRIPTION` | Server description | "Custom game server" |
| `DEFAULT_MAP` | Default map | "simple.map" |
| `ALLOW_MAP_CHANGE` | Allow players to change maps | true |
| `FRIENDLY_FIRE` | Enable friendly fire | false |
| `RESPAWN_TIME` | Respawn time in seconds | 5.0 |
| `BOTS_COUNT` | Number of server-controlled bots (0 disables) | from `bots.count` |
| `BOTS_ACCURACY` | Bot hit chance, `0`..`1` | from `bots.accuracy` |
| `TICK_RATE` | Server tick rate | 60 |
| `TIMEOUT` | Connection timeout in seconds | 30.0 |
| `MAX_PING` | Maximum allowed ping in ms | 500 |
| `LOG_LEVEL` | Logging level (debug, info, warn, error) | info |

Settings are resolved in this order (highest first): command line, environment
variable, `server_config.json`, built-in default. The server prints where each
value came from at startup. `server_config.json` is read from the working
directory, or from `SERVER_CONFIG` when set.

### Multiple players per PC (`maxPlayersPerMachine`)

Several game instances can run on one PC and join the same server. The server
decides how many are allowed from a single machine:

```json
{
  "server": {
    "maxPlayers": 16,
    "maxPlayersPerMachine": 2
  }
}
```

| Value | Meaning |
|-------|---------|
| `-1`  | Unlimited (default) - any number of instances per PC |
| `0`   | Nobody may join |
| `N`   | Up to `N` players from one PC; the next one is refused |

A "machine" is identified by the PC's machine id (derived from
`/etc/machine-id` on Linux, `MachineGuid` on Windows, `gethostuuid` on macOS and
cached as `assets/machine_id`), so instances on the same PC share a slot pool even
when they connect from different addresses. Clients that cannot provide an id are
grouped by IP address instead. A refused client shows
`FAILED - Server limit: N player(s) per machine already connected` on the
connect screen.

### Using .env file

1. Copy the example file:
   ```bash
   cp .env.example .env
   ```

2. Edit the `.env` file with your desired settings

3. Start the server:
   ```bash
   docker-compose up -d
   ```

## Server List Configuration

The game client uses `server_list.json` to discover online servers. To add your server to the list:

1. Edit `server_list.json`:
   ```json
   {
     "servers": [
       {
         "name": "My Server",
         "address": "your-server-ip.com",
         "port": 7777,
         "region": "US-East",
         "description": "My custom server"
       }
     ]
   }
   ```

2. The game will attempt to connect to servers in this list before falling back to local discovery

## Network Configuration

### Firewall

Make sure to allow UDP traffic on the server port:

```bash
# Linux (ufw)
sudo ufw allow 7777/udp

# Linux (iptables)
sudo iptables -A INPUT -p udp --dport 7777 -j ACCEPT

# Windows Firewall
# Add UDP port 7777 inbound rule
```

### Port Forwarding (for public servers)

If you want your server to be accessible from the internet:

1. Forward UDP port 7777 from your router to the server's local IP
2. Use your public IP address in `server_list.json`
3. Consider using a dynamic DNS service if you have a dynamic IP

## Monitoring

### Check server status:
```bash
docker-compose ps
```

### View real-time logs:
```bash
docker-compose logs -f cube-game-server
```

### Check resource usage:
```bash
docker stats cube-game-server
```

## Troubleshooting

### Server not accessible from outside

1. Check firewall rules
2. Verify port forwarding (if behind NAT)
3. Check Docker container is running: `docker-compose ps`
4. Test connectivity: `nc -zv your-server-ip 7777`

### Server crashes on startup

1. Check logs: `docker-compose logs`
2. Verify `server_config.json` exists and is valid
3. Ensure assets directory is mounted correctly

### Players cannot connect

1. Verify server is running
2. Check player limit (MAX_PLAYERS)
3. Ensure clients are using correct IP and port
4. Check network connectivity between client and server

## Updating the Server

To update to a new version:

```bash
docker-compose down
docker-compose pull  # if using a pre-built image
docker-compose build  # if building from source
docker-compose up -d
```

## Performance Tips

- Use a host with good CPU and network bandwidth
- Set `TICK_RATE` based on your server capacity (higher = more CPU usage)
- Limit `MAX_PLAYERS` based on your server's capabilities
- Use wired Ethernet for the server host
- Monitor server resources with `docker stats`

## Security Considerations

- Only expose the necessary UDP port
- Consider using a VPN for private games
- Regularly update the Docker image
- Monitor logs for suspicious activity
- Consider implementing rate limiting for connections
