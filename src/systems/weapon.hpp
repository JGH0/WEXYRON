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
        KNIFE,           ///< melee slot: always-owned starter knife
        BATTLE_AXE,      ///< melee slot: bought heavy axe
        MACE,            ///< melee slot: bought heavy mace
        LUNGE_MINE,      ///< melee slot: one-use forward blast
        PRO_PISTOL       ///< sidearm: semi-automatic pistol (one shot per click)
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
    /// Secondary attack (right mouse while a melee weapon is held).
    virtual void altAttack(Game* game) {}
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
 * @brief Pro pistol: a bought sidearm with pistol bullets.
 *
 * Semi-automatic: every left click fires exactly one round. There is no
 * automatic fire while the button is held and no rate limit beyond the
 * player's clicking speed. Ammo, projectile type and damage match the
 * starter pistol.
 */
class ProPistol : public Weapon {
public:
    ProPistol(bool infiniteAmmo = false);
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
 * this is the always-owned starter knife. Left mouse slashes quickly, the
 * scope/right-mouse binding performs a slower, harder stab.
 */
class Knife : public Weapon {
public:
    /// Left-click slash: quick, moderate damage.
    static constexpr float kSlashCooldown = 0.55f;
    /// Right-click stab: slower recovery, much higher damage.
    static constexpr float kStabCooldown = 1.15f;

    Knife();
    bool canShoot() const override;
    void shoot(Game* game) override;
    /// Slower, higher-damage stab (right mouse while the knife is held).
    void altAttack(Game* game) override;
};

/// Heavy battle axe: slow swings, high damage.
class BattleAxe : public Weapon {
public:
    static constexpr float kSwingCooldown = 0.95f;
    static constexpr float kHeavyCooldown = 1.6f;

    BattleAxe();
    bool canShoot() const override;
    void shoot(Game* game) override;
    void altAttack(Game* game) override;
};

/// Heavy mace: slow blunt swings.
class Mace : public Weapon {
public:
    static constexpr float kSwingCooldown = 0.85f;
    static constexpr float kHeavyCooldown = 1.5f;

    Mace();
    bool canShoot() const override;
    void shoot(Game* game) override;
    void altAttack(Game* game) override;
};

/// One-use lunge mine: detonates forward and hurts the user a little.
class LungeMine : public Weapon {
public:
    LungeMine();
    bool canShoot() const override;
    void shoot(Game* game) override;
    void altAttack(Game* game) override;
};

#endif
