# Weapon Controls Guide

## Controls
- **Left Mouse Button** - Shoot / throw the current weapon
- **Right Mouse Button** - Scope in with the sniper rifle (hold)
- **Mouse Wheel** - Previous / next weapon (zooming with the wheel is gone)
- **1 Key** - Switch to Pistol
- **2 Key** - Switch to Pump Shotgun
- **3 Key** - Switch to Sniper Rifle
- **4 Key** - Switch to Grenade
- **5 Key** - Switch to Flashbang
- **G Key** - Toggle between Grenade and Flashbang
- **Left Shift** - Sprint (hold, standing only)
- **Left Ctrl** - Sneak / crouch (hold)
- **C** - Lie down / get up (crawl while prone; toggle or hold via settings)
- **E** - Plant / defuse the bomb

Every shooting, scoping, weapon-slot, movement and stance binding above can be
changed in **Settings → Controls**.

## Weapon Types
1. **Pistol** - Fast firing, accurate, 150 u/s bullets
2. **Pump Shotgun** - 5 pellets per shot, 110 u/s, wide spread
3. **Sniper Rifle** - Bolt action, 400 u/s, 100 body damage, 10-round magazine;
   hold right mouse to look through the scope (mouse sensitivity is reduced
   while scoped). Switching to it racks the bolt first, and every shot must be
   cycled before the next round can fire.
4. **Grenade** - Thrown explosive (5 second lifetime)
5. **Flashbang** - Tactical flash (4 second lifetime)

## How to Test
1. Run `./run.sh`
2. Select "Play Game" from the menu
3. Use the mouse wheel or number keys 1-5 to switch weapons
4. Use LEFT MOUSE to shoot
5. Hold RIGHT MOUSE while holding the sniper (3) to scope
6. Press G to toggle between Grenade/Flashbang
7. Hold LEFT SHIFT to sprint, LEFT CTRL to sneak, and press C to lie down

## Notes
- Bullet hits are detected with swept (continuous) collision, so the high
  velocities do not make shots tunnel through players.
- The sniper's headshot multiplier is 2x (200 damage, accepted by the server
  up to 160); a body shot deals 100 and is already lethal.
