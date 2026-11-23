#ifndef WEAPON_HPP
#define WEAPON_HPP

#include <glm/glm.hpp>

// Forward declaration
class Game;

/**
 * @brief Base weapon class for all weapon types
 */
class Weapon {
public:
    enum WeaponType {
        PISTOL,
        PUMP_SHOTGUN,
        SNIPER,
        GRENADE,
        FLASHBANG,
        KNIFE
    };

protected:
    WeaponType type;
    float cooldown;
    int ammo;
    /// Ammo a fresh round / respawn starts with (see refill()).
    int initialAmmo;
    bool infiniteAmmo;
    float cooldownTimer;

public:
    Weapon(WeaponType type, float cooldown, int ammo, bool infiniteAmmo = false);
    virtual ~Weapon() = default;
    
    virtual bool canShoot() const;
    virtual void shoot(Game* game) = 0;
    virtual void updateCooldown(float deltaTime);
    virtual void reload() {}
    
    WeaponType getType() const { return type; }
    float getCooldown() const { return cooldown; }
    int getAmmo() const { return ammo; }
    int getInitialAmmo() const { return initialAmmo; }
    bool isInfiniteAmmo() const { return infiniteAmmo; }
    void setInfiniteAmmo(bool val) { infiniteAmmo = val; }
    /// Restore the ammo a fresh round starts with.
    void refill() { ammo = initialAmmo; }
    /// Replace the current count (buying / resetting an owned grenade).
    void setAmmo(int count) { ammo = count; }
    /// Force at least `seconds` of cycling before the next shot (bolt rack).
    void startCycle(float seconds) {
        if (seconds > cooldownTimer) cooldownTimer = seconds;
    }
};

/**
 * @brief Pistol weapon implementation
 */
class Pistol : public Weapon {
public:
    Pistol(bool infiniteAmmo = false);
    void shoot(Game* game) override;
};

/**
 * @brief Pump Shotgun weapon implementation  
 */
class PumpShotgun : public Weapon {
public:
    PumpShotgun(bool infiniteAmmo = false);
    void shoot(Game* game) override;
};

/**
 * @brief Bolt-action sniper rifle with a right-mouse scope (scope handled by Game)
 */
class Sniper : public Weapon {
public:
    Sniper(bool infiniteAmmo = false);
    void shoot(Game* game) override;
};

/**
 * @brief Grenade weapon implementation
 */
class Grenade : public Weapon {
public:
    Grenade(int count = 3, bool infiniteAmmo = false);
    void shoot(Game* game) override;
};

/**
 * @brief Flashbang weapon implementation
 */
class Flashbang : public Weapon {
public:
    Flashbang(int count = 2, bool infiniteAmmo = false);
    void shoot(Game* game) override;
};

/**
 * @brief Plain close-range knife (melee slot).
 *
 * The melee slot is meant to hold several purchasable melee weapons later;
 * this is the always-owned starter knife.
 */
class Knife : public Weapon {
public:
    Knife();
    bool canShoot() const override;
    void shoot(Game* game) override;
};

#endif
