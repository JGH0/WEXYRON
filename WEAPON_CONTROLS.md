# Weapon Controls Guide

## Controls
- **Left Mouse Button** - Shoot / swing / throw the current weapon
- **Right Mouse Button** - Scope in with the sniper rifle (hold)
- **Mouse Wheel** - Previous / next weapon (zooming with the wheel is gone)
- **1 Key** - Primary slot (cycles the owned shotgun / sniper)
- **2 Key** - Sidearm (pistol)
- **3 Key** - Knife (melee)
- **4 Key** - HE Grenade
- **5 Key** - Flashbang
- **B Key** - Buy menu (buy phase in Bomb Defusal; free items in other modes)
- **G Key** - Toggle between Grenade and Flashbang
- **I Key** - Inspect the held weapon (animation; cancelled by firing)
- **Left Shift** - Sprint (hold, standing only)
- **Left Ctrl** - Sneak / crouch (hold)
- **C** - Lie down / get up (crawl while prone; toggle or hold via settings)
- **E** - Plant / defuse the bomb

Every shooting, scoping, weapon-slot, buy-menu, movement and stance binding above
can be changed in **Settings → Controls**.

## Bomb Defusal Economy
- A round starts with a **freeze period** (server `gameplay.freezeTime`, default
  **10 s**; single-player also uses 10 s). Movement, jumping and shooting are
  blocked.
- After the freeze, the buy menu stays available for **20 more seconds** as
  long as the player has not moved. Moving ends the buy window for that round,
  and the window also expires after the 20 seconds.
- Shooting stays disabled while the buy window is open; move to leave the buy
  phase and enable your weapons.
- Your loadout carries over between rounds, even when you die: primary,
  grenades, armor and defuse kit are kept. Ammo is refilled each round.
- The very first round of a match hands out one HE grenade and one flashbang.
- Kills pay **$100**. Winning a round pays **$1500**; losing pays an escalating
  loss bonus (**$700**, then **+$250** per consecutive loss, up to **$1700**).
- Prices: kevlar **$650**, defuse kit **$400**, pump shotgun **$1500**, sniper
  **$4750**, HE grenade **$300**, flashbang **$200**. The pistol and knife are
  free.
- Ammo is refilled at the start of every round, and the infinite-ammo setting
  is disabled in the economic mode.
- Deathmatch, Team Deathmatch and every other mode show the same menu with every
  item free.

## Weapon Types
1. **Pistol** - Fast firing, accurate, 150 u/s bullets
2. **Pump Shotgun** - 5 pellets per shot, 110 u/s, wide spread
3. **Sniper Rifle** - Bolt action, 400 u/s, 100 body damage, 10-round magazine;
   hold right mouse to look through the scope (mouse sensitivity is reduced
   while scoped). Switching to it racks the bolt first, and every shot must be
   cycled before the next round can fire.
4. **Knife** - Plain melee starter weapon, 55 damage in a short forward arc
5. **Grenade** - Thrown explosive (5 second lifetime)
6. **Flashbang** - Tactical flash (4 second lifetime)

## How to Test
1. Run `./run.sh`
2. Select "Play Game" from the menu
3. Use the mouse wheel or number keys 1-5 to switch weapons (1 primary,
   2 sidearm, 3 knife, 4 grenade, 5 flashbang)
4. Use LEFT MOUSE to shoot / swing / throw
5. Hold RIGHT MOUSE while holding the sniper (in the primary slot) to scope
6. Press G to toggle between Grenade/Flashbang
7. Press B to open the buy menu (during the freeze and the 20 s post-freeze
   grace in Bomb Defusal; free items in the other modes)
8. Hold LEFT SHIFT to sprint, LEFT CTRL to sneak, and press C to lie down

## Notes
- Bullet hits are detected with swept (continuous) collision, so the high
  velocities do not make shots tunnel through players.
- The sniper's headshot multiplier is 2x (200 damage, accepted by the server
  up to 160); a body shot deals 100 and is already lethal.
