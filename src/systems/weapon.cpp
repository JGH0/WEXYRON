#include "weapon.hpp"
#include "../core/game.hpp"
#include <iostream>

namespace {

// Bullet speeds in world units per second. These are deliberately fast (and
// the projectile hit test is swept), so the shots behave like bullets instead
// of slow floating dots. Throwables keep their lobbed speeds.
constexpr float kPistolBulletSpeed   = 150.0f;
constexpr float kSniperBulletSpeed   = 400.0f;
constexpr float kShotgunPelletSpeed  = 110.0f;

} // namespace

Weapon::Weapon(WeaponType type, float cooldown, int ammo, bool infiniteAmmo)
    : type(type), cooldown(cooldown), ammo(ammo), infiniteAmmo(infiniteAmmo), cooldownTimer(0.0f) {
}

bool Weapon::canShoot() const {
    return cooldownTimer <= 0.0f && (infiniteAmmo || ammo > 0);
}

void Weapon::updateCooldown(float deltaTime) {
    if (cooldownTimer > 0.0f) {
        cooldownTimer -= deltaTime;
    }
}

// Pistol Implementation
Pistol::Pistol(bool infiniteAmmo) 
    : Weapon(PISTOL, 0.3f, infiniteAmmo ? 999 : 1000, infiniteAmmo) {
}

void Pistol::shoot(Game* game) {
    if (!canShoot()) { game->playDryFire(); return; }
    
    if (game->createProjectile(game->getCamera().Position, game->getCamera().Front * kPistolBulletSpeed, 2.0f, 0, 0)) {
        // Fired from the player's own position: 3D audio pans/attenuates it
        // like any other sound. Slight random pitch keeps shots from sounding
        // identical.
        game->playGunshot("pistol_fire", 1.0f);
        if (!infiniteAmmo) {
            ammo--;
        }
        cooldownTimer = cooldown;
        std::cout << "Pistol shot! Active: " << game->getActiveProjectiles() << " Ammo: " << (infiniteAmmo ? "∞" : std::to_string(ammo)) << std::endl;
    }
}

// Pump Shotgun Implementation
PumpShotgun::PumpShotgun(bool infiniteAmmo)
    : Weapon(PUMP_SHOTGUN, 0.8f, infiniteAmmo ? 999 : 25, infiniteAmmo) {
}

void PumpShotgun::shoot(Game* game) {
    if (!canShoot()) { game->playDryFire(); return; }

    // Shoot 5 projectiles in a spread pattern
    int pelletsShot = 0;
    for (int i = 0; i < 5; i++) {
        // Add spread to shotgun pellets
        glm::vec3 spread = glm::vec3(
            (rand() % 100 - 50) * 0.002f,
            (rand() % 100 - 50) * 0.002f,
            0.0f
        );

        if (game->createProjectile(game->getCamera().Position, (game->getCamera().Front + spread) * kShotgunPelletSpeed, 1.0f, 0, 3)) {
            pelletsShot++;
        }
    }

    if (pelletsShot > 0) {
        game->playGunshot("shotgun_fire", 0.9f);
        if (!infiniteAmmo) {
            ammo--;
        }
        cooldownTimer = cooldown;
        std::cout << "Shotgun blast! Pellets: " << pelletsShot << " Ammo: " << (infiniteAmmo ? "∞" : std::to_string(ammo)) << std::endl;
    } else {
        std::cout << "Shotgun failed to fire - max projectiles reached" << std::endl;
    }
}

// Sniper Implementation
Sniper::Sniper(bool infiniteAmmo)
    : Weapon(SNIPER, 1.45f, infiniteAmmo ? 999 : 10, infiniteAmmo) {
}

void Sniper::shoot(Game* game) {
    if (!canShoot()) { game->playDryFire(); return; }

    // Projectile type 4 = sniper round: high velocity, high damage, no spread.
    if (game->createProjectile(game->getCamera().Position,
                               game->getCamera().Front * kSniperBulletSpeed,
                               3.0f, 0, 4)) {
        game->playGunshot("sniper_fire_thunder", 1.15f);
        // Bolt-action: the rattle follows the shot, and the long cooldown
        // prevents another round until the bolt has been cycled.
        game->scheduleSniperBolt(0.42f);
        if (!infiniteAmmo) {
            ammo--;
        }
        cooldownTimer = cooldown;
        std::cout << "Sniper shot! Active: " << game->getActiveProjectiles()
                  << " Ammo: " << (infiniteAmmo ? "∞" : std::to_string(ammo)) << std::endl;
    }
}

// Grenade Implementation
Grenade::Grenade(int count, bool infiniteAmmo)
    : Weapon(GRENADE, 1.0f, count, infiniteAmmo) {
}

void Grenade::shoot(Game* game) {
    if (!canShoot()) return;
    
    if (game->createProjectile(game->getCamera().Position, game->getCamera().Front * 30.0f, 5.0f, 0, 1)) {
        game->playSoundEffect("grenade_throw", game->getCamera().Position);
        if (!infiniteAmmo) {
            ammo--;
        }
        cooldownTimer = cooldown;
        std::cout << "Threw grenade! Grenades left: " << (infiniteAmmo ? "∞" : std::to_string(ammo)) << std::endl;
    }
}

// Flashbang Implementation  
Flashbang::Flashbang(int count, bool infiniteAmmo)
    : Weapon(FLASHBANG, 1.0f, count, infiniteAmmo) {
}

void Flashbang::shoot(Game* game) {
    if (!canShoot()) return;
    
    if (game->createProjectile(game->getCamera().Position, game->getCamera().Front * 25.0f, 4.0f, 0, 2)) {
        game->playSoundEffect("flashbang_throw", game->getCamera().Position);
        if (!infiniteAmmo) {
            ammo--;
        }
        cooldownTimer = cooldown;
        std::cout << "Threw flashbang! Flashbangs left: " << (infiniteAmmo ? "∞" : std::to_string(ammo)) << std::endl;
    }
}
