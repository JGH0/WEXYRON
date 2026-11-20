# Discord Rich Presence

WEXYRON reports the current screen to Discord:

| Game state | Discord shows |
| --- | --- |
| Main menu / settings / map selection | In the main menu |
| Match | Playing Deathmatch / Team vs Team / Bomb Defusal + map |
| Dead | Eliminated - waiting to respawn + map |
| Map editor | Building a map + map name |

It uses the **local Discord IPC socket** and the public Application ID in
`src/core/brand.hpp`:

```cpp
inline constexpr const char* kDiscordApplicationId = "1573411705831161957";
```

## Security

The Application ID is a public client identifier (like a Steam App ID). It is
not a password. The game never contains a Discord bot token or OAuth client
secret, and the integration only writes the local user's activity.

## Developer Portal setup

1. Open <https://discord.com/developers/applications>.
2. Select application `1573411705831161957`.
3. Set its name to **WEXYRON**.
4. Open **Rich Presence → Art Assets** and upload:
   - `logo` — large image
   - `menu`, `editor`, `deathmatch`, `team`, `bomb` — small images
5. Restart the game.

Missing asset keys do not break presence; the image is simply empty.

## Disabling it

In the game: **Settings → INTERFACE → DISCORD STATUS**. The setting is saved
as `discord_presence=true/false` in `assets/settings.cfg`.

If Discord is not running, the game retries quietly in the background. An
actual RPC error is printed once as `[discord] unavailable`.

## Linux notes

- Discord must be running under the same user session.
- Native Discord reads the socket from `$XDG_RUNTIME_DIR/discord-ipc-0`.
- A Flatpak/Snap Discord may be sandboxed away from the game's socket. The
  official package usually works best.
