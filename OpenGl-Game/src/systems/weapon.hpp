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
        GRENADE,
        FLASHBANG
    };

protected:
    WeaponType type;
    float cooldown;
    int ammo;
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
    bool isInfiniteAmmo() const { return infiniteAmmo; }
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
 * @brief Grenade weapon implementation
 */
class Grenade : public Weapon {
public:
    Grenade(int count = 3);
    void shoot(Game* game) override;
};

/**
 * @brief Flashbang weapon implementation
 */
class Flashbang : public Weapon {
public:
    Flashbang(int count = 2);
    void shoot(Game* game) override;
};

#endif
