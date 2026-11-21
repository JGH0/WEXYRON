// Humanoid hit-zone test: body 1x, head 2x, arms/legs/feet 0.5x.
//
// The body is upright at the origin and faces +X (yaw 0), so its local right
// axis points along +Z. This lets each test point be placed unambiguously.

#include "core/humanoid.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    const glm::vec3 feet(0.0f);
    const float yaw = 0.0f;

    CHECK(humanoid::hitZone(glm::vec3(0.0f, 1.65f, 0.0f), feet, yaw) == humanoid::HitZone::Head);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 1.20f, 0.0f), feet, yaw) == humanoid::HitZone::Body);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 0.55f, 0.0f), feet, yaw) == humanoid::HitZone::Limb);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 0.04f, 0.0f), feet, yaw) == humanoid::HitZone::Limb);
    // Arms hang at local x = +/-0.3, which is world z with yaw 0.
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 1.20f, 0.32f), feet, yaw) == humanoid::HitZone::Limb);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 1.20f, -0.32f), feet, yaw) == humanoid::HitZone::Limb);

    // Facing -Z (yaw -90): the same arm offset is now along world X.
    CHECK(humanoid::hitZone(glm::vec3(0.32f, 1.20f, 0.0f), feet, -90.0f) == humanoid::HitZone::Limb);

    // Crouching: the whole silhouette is lower (height 1.2 instead of 1.8),
    // so the head threshold drops with it.
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 1.10f, 0.0f), feet, yaw, 1) == humanoid::HitZone::Head);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 0.75f, 0.0f), feet, yaw, 1) == humanoid::HitZone::Body);
    CHECK(humanoid::hitZone(glm::vec3(0.0f, 0.35f, 0.0f), feet, yaw, 1) == humanoid::HitZone::Limb);

    // Prone: the body lies along local +Z, so the head is forward of the
    // torso and the legs trail behind the feet.
    CHECK(humanoid::hitZone(glm::vec3(0.90f, 0.35f, 0.0f), feet, yaw, 2) == humanoid::HitZone::Head);
    CHECK(humanoid::hitZone(glm::vec3(0.30f, 0.35f, 0.0f), feet, yaw, 2) == humanoid::HitZone::Body);
    CHECK(humanoid::hitZone(glm::vec3(-0.50f, 0.35f, 0.0f), feet, yaw, 2) == humanoid::HitZone::Limb);
    CHECK(humanoid::hitZone(glm::vec3(0.30f, 0.35f, 0.32f), feet, yaw, 2) == humanoid::HitZone::Limb);

    CHECK(humanoid::damageMultiplier(humanoid::HitZone::Head) == 2.0f);
    CHECK(humanoid::damageMultiplier(humanoid::HitZone::Body) == 1.0f);
    CHECK(humanoid::damageMultiplier(humanoid::HitZone::Limb) == 0.5f);

    // Base weapon damage before the zone multiplier.
    CHECK(humanoid::projectileBaseDamage(0) == 25.0f);   // pistol
    CHECK(humanoid::projectileBaseDamage(3) == 10.0f);   // shotgun pellet
    CHECK(humanoid::projectileBaseDamage(4) == 100.0f);  // sniper

    std::printf("head x2 / body x1 / limb x0.5: %s\n",
                failures ? "FAILED" : "OK");
    std::printf(failures ? "HUMANOID TEST: %d FAILURES\n" : "HUMANOID TEST: ALL OK\n", failures);
    return failures ? 1 : 0;
}
