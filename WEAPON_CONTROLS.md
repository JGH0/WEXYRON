# Weapon Controls Guide

## Controls
- **Left Mouse Button** - Shoot / slash / throw the current weapon
- **Right Mouse Button** - Scope in with the sniper rifle (hold); heavy attack with melee
- **Mouse Wheel** - Previous / next weapon (zooming with the wheel is gone)
- **1 Key** - Primary slot (the carried shotgun or sniper — only one at a time)
- **2 Key** - Sidearm (pistol / pro pistol; press again to toggle)
- **3 Key** - Melee (equips the carried melee weapon: knife, axe, mace or lunge mine)
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
  **$4750**, pro pistol **$800**, HE grenade **$300**, flashbang **$200**, battle
  axe **$600**, mace **$800**, lunge mine **$1000**. The starter pistol and knife
  are free.
- The buy screen uses a Counter-Strike-style grid: GEAR, PRIMARY, SIDEARM, MELEE
  and GRENADES columns are all visible at once. Only one primary (shotgun or
  sniper) and one melee weapon are carried at a time — buying another replaces
  the current one, and key 1 / key 3 equip what you carry. Both sidearms are
  kept: key 2 toggles between the starter pistol and the bought pro pistol. The
  knife row re-equips the default knife for free.
- Ammo is refilled at the start of every round, and the infinite-ammo setting
  is disabled in the economic mode.
- Deathmatch, Team Deathmatch and every other mode show the same menu with every
  item free.

## Weapon Types
1. **Pistol** - Fast firing, accurate, 150 u/s bullets
2. **Pro Pistol** - Bought sidearm (**$800**) with the same bullets, damage and
   150 u/s speed as the pistol. Semi-automatic: holding the trigger fires
   nothing, each click fires one shot, and rapid clicking fires as fast as you
   can click.
3. **Pump Shotgun** - 5 pellets per shot, 110 u/s, wide spread
4. **Sniper Rifle** - Bolt action, 400 u/s, 100 body damage, 10-round magazine;
   hold right mouse to look through the scope (mouse sensitivity is reduced
   while scoped). Switching to it racks the bolt first, and every shot must be
   cycled before the next round can fire.
5. **Knife** - Plain melee starter weapon. Left mouse slashes (55 damage,
   short reach); right mouse performs a slower stab (90 damage, slightly longer
   reach). Bots and remote bodies use the same hit box.
6. **Battle Axe** - Bought melee weapon. A stick with an axe blade; light swing
   70 damage, heavy overhead 100 damage, slower than the knife.
7. **Mace** - Bought melee weapon. Light swing 65 damage, heavy 95 damage.
8. **Lunge Mine** - Bought melee weapon, **one use**. Detonates forwards in a
   short cone for 110 damage, hurts the user a little (20) and then the player
   falls back to the knife.
9. **Grenade** - Thrown explosive (5 second lifetime)
10. **Flashbang** - Tactical flash (4 second lifetime)

## How to Test
1. Run `./run.sh`
2. Select "Play Game" from the menu
3. Use the mouse wheel or number keys 1-5 to switch weapons (1 primary,
   2 sidearm, 3 knife, 4 grenade, 5 flashbang)
4. Use LEFT MOUSE to shoot / swing / throw
5. Hold RIGHT MOUSE while holding the sniper (in the primary slot) to scope,
   or to stab while holding the knife
6. Press G to toggle between Grenade/Flashbang
7. Press B to open the buy menu (during the freeze and the 20 s post-freeze
   grace in Bomb Defusal; free items in the other modes)
8. Hold LEFT SHIFT to sprint, LEFT CTRL to sneak, and press C to lie down

## Notes
- Bullet hits are detected with swept (continuous) collision, so the high
  velocities do not make shots tunnel through players.
- The sniper's headshot multiplier is 2x (200 damage, accepted by the server
  up to 160); a body shot deals 100 and is already lethal.
