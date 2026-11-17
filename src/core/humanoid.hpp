/**
 * @file humanoid.hpp
 * @brief Blocky humanoid model, walk/air/recoil animation and hit zones.
 *
 * Every actor (remote player, teammate, practice enemy) is built from the same
 * handful of boxes: two feet, lower legs, upper legs, a torso, a neck, a head,
 * upper/lower arms and hands. The head carries a tiny face whose mouth follows
 * the actor's health (:), :|, :().
 *
 * The model dimensions match Game::bodyWidth / Game::bodyHeight (0.8 x 1.8), so
 * what is drawn and what a shot hits use the same silhouette.
 */

#ifndef HUMANOID_HPP
#define HUMANOID_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

#include "engine/renderer.hpp"

namespace humanoid {

/// Body dimensions shared with the game's hit boxes.
constexpr float kBodyWidth  = 0.8f;
constexpr float kBodyHeight = 1.8f;
constexpr float kHipY       = 0.90f;   ///< upper-leg pivot
constexpr float kKneeY      = 0.50f;   ///< lower-leg pivot
constexpr float kAnkleY     = 0.10f;
constexpr float kShoulderY  = 1.40f;
constexpr float kElbowY     = 1.06f;
constexpr float kWristY     = 0.76f;
constexpr float kHeadMinY   = 1.50f;   ///< anything at/above this is a head shot
constexpr float kArmHalfX   = 0.24f;   ///< |local x| above this is an arm
constexpr float kHipHalfX   = 0.17f;

// Damage multipliers requested by the game rules.
constexpr float kHeadMultiplier = 2.0f;
constexpr float kBodyMultiplier = 1.0f;
constexpr float kLimbMultiplier = 0.5f;

/// Which part of a body a shot hit. Explosions ignore this (fixed damage).
enum class HitZone {
    Head,
    Body,
    Limb
};

inline const char* hitZoneName(HitZone zone) {
    switch (zone) {
        case HitZone::Head: return "head";
        case HitZone::Limb: return "limb";
        default:            return "body";
    }
}

inline float damageMultiplier(HitZone zone) {
    switch (zone) {
        case HitZone::Head: return kHeadMultiplier;
        case HitZone::Limb: return kLimbMultiplier;
        default:            return kBodyMultiplier;
    }
}

/// Base bullet damage before the hit-zone multiplier. Matches the weapon
/// system: pistol bullet 25, shotgun pellet 10, sniper round 100, everything
/// else 20.
inline float projectileBaseDamage(int projectileType) {
    switch (projectileType) {
        case 0: return 25.0f;   // pistol
        case 3: return 10.0f;   // shotgun pellet
        case 4: return 100.0f;  // sniper
        default: return 20.0f;
    }
}

/// Per-actor animation state. Call advance() once per frame and push damage /
/// shoot events by setting the timers to 1.0.
struct Anim {
    float walkPhase = 0.0f;   ///< gait phase (radians)
    float walkBlend = 0.0f;   ///< 0 = standing, 1 = full stride
    float damage = 0.0f;      ///< 0..1 flinch timer, set to 1 on a hit
    float shoot = 0.0f;       ///< 0..1 fire/recoil timer, set to 1 on a shot
    /// 0 = upright, 1 = fully collapsed. Gameplay advances this after death so
    /// bodies fall over instead of disappearing in the same frame.
    float death = 0.0f;
    float vx = 0.0f;          ///< last horizontal velocity (world)
    float vy = 0.0f;          ///< last vertical velocity
    float vz = 0.0f;
    bool onGround = true;

    // Networked actors only receive positions at the packet rate: a short air
    // hold derived from vertical position changes keeps the jump/fall pose
    // visible between packets (see Game::updateRemoteAnimations).
    float lastY = 0.0f;
    float airHold = 0.0f;
    bool hasLastY = false;

    void advance(float dt, float velocityX, float velocityY, float velocityZ,
                 bool grounded) {
        if (dt <= 0.0f) return;
        vx = velocityX;
        vy = velocityY;
        vz = velocityZ;
        onGround = grounded;

        const float speed = std::sqrt(vx * vx + vz * vz);
        const float target = (grounded && speed > 0.15f)
                                 ? std::min(1.0f, speed / 5.0f)
                                 : 0.0f;
        walkBlend += (target - walkBlend) * std::min(1.0f, dt * 8.0f);
        if (grounded && walkBlend > 0.01f) {
            walkPhase += dt * (3.5f + std::max(0.0f, speed) * 2.2f);
        }
        if (damage > 0.0f) damage = std::max(0.0f, damage - dt * 4.5f);
        if (shoot > 0.0f)  shoot  = std::max(0.0f, shoot - dt * 7.0f);
    }
};

/// Classify a world-space hit point against a body standing at `feet` and
/// facing `yawDeg` (same yaw the camera/player uses).
inline HitZone hitZone(const glm::vec3& point, const glm::vec3& feet,
                       float yawDeg) {
    const glm::vec3 d = point - feet;
    const float yaw = glm::radians(yawDeg);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    // Body-local axes: forward = (cy, 0, sy), right = (-sy, 0, cy).
    const float localX = d.x * (-sy) + d.z * cy;
    const float localZ = d.x * cy + d.z * sy;

    if (d.y >= kHeadMinY) {
        // Only the actual head box is a head shot; shoulder-height hits at the
        // sides of the wide body box stay body hits.
        if (std::abs(localX) <= 0.20f && std::abs(localZ) <= 0.20f) {
            return HitZone::Head;
        }
        return HitZone::Body;
    }

    // Legs/feet below the hip, arms outside the torso.
    if (d.y < kHipY) return HitZone::Limb;
    if (std::abs(localX) > kArmHalfX) return HitZone::Limb;
    return HitZone::Body;
}

/// Draw one blocky humanoid.
/// @param feet       feet position (bottom centre)
/// @param yawDeg     facing direction (camera yaw convention)
/// @param baseColor  team/enemy colour (no texture is used)
/// @param healthPct  0..1, selects the face expression
/// @param anim       current animation state
/// @param pitchDeg   look pitch, used for a small head tilt
/// @param armed      draw a simple gun in the right hand
inline void render(Renderer& renderer, const glm::vec3& feet, float yawDeg,
                   const glm::vec3& baseColor, float healthPct,
                   const Anim& anim, float pitchDeg = 0.0f, bool armed = true) {
    using glm::mat4;
    using glm::vec3;
    using glm::vec4;

    const float health = std::clamp(healthPct, 0.0f, 1.0f);

    // ── Root frame: feet at the origin, +Y up, +Z forward ─────────────────
    const float yaw = glm::radians(yawDeg);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const vec3 forward(cy, 0.0f, sy);
    const vec3 right(-sy, 0.0f, cy);

    mat4 root(1.0f);
    root[0] = vec4(right, 0.0f);
    root[1] = vec4(0.0f, 1.0f, 0.0f, 0.0f);
    root[2] = vec4(forward, 0.0f);
    root[3] = vec4(feet, 1.0f);

    // Death animation: rotate the whole body around the feet (fall backwards)
    // and lift it a little off the floor so the collapsed model does not sink
    // into the ground.
    const float death = std::clamp(anim.death, 0.0f, 1.0f);
    if (death > 0.0f) {
        const float fall = death * (2.0f - death);   // ease out
        root = glm::rotate(root, glm::radians(-82.0f * fall), vec3(1, 0, 0));
        root[3].y += 0.10f * fall;
    }

    // Push a box in a parent's local space. `rot` is applied around the box
    // centre (X then Y then Z).
    auto add = [&](const mat4& parent, const vec3& centre, const vec3& size,
                   const vec3& color, const vec3& rot = vec3(0.0f)) {
        mat4 m = glm::translate(parent, centre);
        if (rot.x != 0.0f) m = glm::rotate(m, glm::radians(rot.x), vec3(1, 0, 0));
        if (rot.y != 0.0f) m = glm::rotate(m, glm::radians(rot.y), vec3(0, 1, 0));
        if (rot.z != 0.0f) m = glm::rotate(m, glm::radians(rot.z), vec3(0, 0, 1));
        m = glm::scale(m, size);
        renderer.drawCube(m, color);
    };

    // Shade the palette slightly between parts so the silhouette reads even
    // without textures: head lightest, hands/feet darkest.
    const vec3 headColor  = glm::min(baseColor * 1.25f + vec3(0.04f), vec3(1.0f));
    const vec3 torsoColor = baseColor;
    const vec3 upperColor = glm::min(baseColor * 1.08f, vec3(1.0f));
    const vec3 lowerColor = baseColor * 0.82f;
    const vec3 handsColor = baseColor * 0.55f;
    const vec3 feetColor  = baseColor * 0.45f;
    const vec3 faceColor(0.035f, 0.035f, 0.045f);

    // ── Gait / air / hit-reaction pose ─────────────────────────────────────
    const float dirSign = (anim.vx * forward.x + anim.vz * forward.z) < -0.15f
                              ? -1.0f : 1.0f;
    const float swing = std::sin(anim.walkPhase) * 30.0f * anim.walkBlend * dirSign;
    const float sideSpeed = anim.vx * right.x + anim.vz * right.z;

    float leftHip =  swing;
    float rightHip = -swing;
    float leftKnee = std::max(0.0f,  std::sin(anim.walkPhase)) * 24.0f * anim.walkBlend;
    float rightKnee = std::max(0.0f, -std::sin(anim.walkPhase)) * 24.0f * anim.walkBlend;
    float leftArm = -swing * 0.65f;
    float rightArm =  swing * 0.65f;
    float armOutL = 4.0f;
    float armOutR = -4.0f;
    float torsoBob = std::abs(std::sin(anim.walkPhase)) * 0.022f * anim.walkBlend;
    // Lean/twist into sideways motion, so strafing in either direction reads
    // differently from walking straight ahead.
    float torsoLean = std::clamp(-sideSpeed * 1.5f, -9.0f, 9.0f);
    float torsoTwist = std::clamp(-sideSpeed * 0.6f, -4.0f, 4.0f);

    if (!anim.onGround) {
        // Jump: knees up, arms forward/out. Falling: legs trail, arms spread.
        const float rising = std::clamp(anim.vy / 4.0f, -1.0f, 1.0f);
        leftHip = rightHip = -12.0f - 8.0f * std::max(0.0f, rising);
        leftKnee = rightKnee = 22.0f + 8.0f * std::max(0.0f, -rising);
        leftArm = rightArm = -28.0f - 12.0f * std::max(0.0f, rising);
        armOutL = 14.0f;
        armOutR = -14.0f;
        torsoLean = -5.0f;
        torsoBob = 0.0f;
    }

    // Taking damage: recoil the torso back, fling the arms out a little.
    const float flinch = anim.damage;
    if (flinch > 0.0f) {
        torsoLean -= 13.0f * flinch;
        leftArm  -= 18.0f * flinch;
        rightArm -= 18.0f * flinch;
        armOutL  += 15.0f * flinch;
        armOutR  -= 15.0f * flinch;
    }

    // Shooting: both arms point forward, the right one holds the gun.
    const float fire = anim.shoot;
    if (fire > 0.0f) {
        rightArm = rightArm * (1.0f - fire) - 74.0f * fire;
        leftArm  = leftArm  * (1.0f - fire) - 58.0f * fire;
        armOutR  = armOutR  * (1.0f - fire) - 4.0f * fire;
        armOutL  = armOutL  * (1.0f - fire) + 10.0f * fire;
        torsoTwist = 8.0f * fire;
    }

    // Dead: limp limbs, no gait / firing pose.
    if (death > 0.0f) {
        leftHip = rightHip = -7.0f * death;
        leftKnee = rightKnee = 20.0f * death;
        leftArm = rightArm = -16.0f * death;
        armOutL = 30.0f * death;
        armOutR = -30.0f * death;
        torsoLean = 0.0f;
        torsoTwist = 0.0f;
        torsoBob = 0.0f;
    }

    const float headPitch = std::clamp(pitchDeg * 0.35f, -18.0f, 18.0f)
                          - 8.0f * flinch - 14.0f * death;

    // ── Torso / neck / head ────────────────────────────────────────────────
    mat4 torsoRoot = glm::translate(root, vec3(0.0f, kHipY + torsoBob, 0.0f));
    torsoRoot = glm::rotate(torsoRoot, glm::radians(torsoLean), vec3(1, 0, 0));
    torsoRoot = glm::rotate(torsoRoot, glm::radians(torsoTwist), vec3(0, 1, 0));

    add(torsoRoot, vec3(0.0f, 0.27f, 0.0f), vec3(0.46f, 0.54f, 0.28f), torsoColor);
    add(torsoRoot, vec3(0.0f, 0.57f, 0.0f), vec3(0.16f, 0.06f, 0.16f), lowerColor);

    mat4 headRoot = glm::translate(torsoRoot, vec3(0.0f, 0.60f, 0.0f));
    headRoot = glm::rotate(headRoot, glm::radians(headPitch), vec3(1, 0, 0));
    add(headRoot, vec3(0.0f, 0.15f, 0.0f), vec3(0.30f, 0.30f, 0.30f), headColor);

    // Face on the front (+Z) side of the head: two eyes and a health-based
    // mouth. Everything is a few flat dark boxes.
    const float eyeY = 0.19f;
    const float faceZ = 0.151f;
    add(headRoot, vec3(-0.075f, eyeY, faceZ), vec3(0.052f, 0.050f, 0.012f), faceColor);
    add(headRoot, vec3( 0.075f, eyeY, faceZ), vec3(0.052f, 0.050f, 0.012f), faceColor);

    float mouthY = 0.080f;
    float cornerOffset = 0.0f;
    if (health > 0.66f) {
        cornerOffset =  0.018f;   // :)
    } else if (health < 0.33f) {
        cornerOffset = -0.016f;   // :(
    }
    add(headRoot, vec3(0.0f, mouthY, faceZ), vec3(0.105f, 0.018f, 0.012f), faceColor);
    add(headRoot, vec3(-0.065f, mouthY + cornerOffset, faceZ),
        vec3(0.030f, 0.018f, 0.012f), faceColor);
    add(headRoot, vec3( 0.065f, mouthY + cornerOffset, faceZ),
        vec3(0.030f, 0.018f, 0.012f), faceColor);

    // ── Arms ───────────────────────────────────────────────────────────────
    // Shoulder local coordinates are relative to the torso pivot.
    auto arm = [&](float side, float swingDeg, float outDeg) {
        mat4 shoulder = glm::translate(torsoRoot, vec3(side * 0.295f, 0.50f, 0.0f));
        shoulder = glm::rotate(shoulder, glm::radians(outDeg), vec3(0, 0, 1));
        shoulder = glm::rotate(shoulder, glm::radians(swingDeg), vec3(1, 0, 0));

        add(shoulder, vec3(0.0f, -0.17f, 0.0f), vec3(0.13f, 0.34f, 0.16f),
            upperColor, vec3(0.0f, 0.0f, 0.0f));
        const float elbowBend = (fire > 0.0f ? -18.0f * fire : 0.0f)
                              + (anim.onGround ? 0.0f : 8.0f);
        mat4 elbow = glm::translate(shoulder, vec3(0.0f, -0.34f, 0.0f));
        elbow = glm::rotate(elbow, glm::radians(elbowBend), vec3(1, 0, 0));
        add(elbow, vec3(0.0f, -0.15f, 0.0f), vec3(0.12f, 0.30f, 0.14f), lowerColor);
        mat4 hand = glm::translate(elbow, vec3(0.0f, -0.30f, 0.0f));
        add(hand, vec3(0.0f, -0.055f, 0.0f), vec3(0.12f, 0.11f, 0.14f), handsColor);
        return hand;
    };

    const mat4 leftHand  = arm(+1.0f, leftArm,  armOutL);
    const mat4 rightHand = arm(-1.0f, rightArm, armOutR);

    if (armed) {
        // A compact blocky gun in the right hand. It points along the arm, so
        // raising the arm for a shot aims it forward.
        add(rightHand, vec3(0.0f, -0.10f, 0.0f), vec3(0.065f, 0.24f, 0.085f),
            vec3(0.10f, 0.10f, 0.12f));
        add(rightHand, vec3(0.0f, -0.215f, 0.0f), vec3(0.045f, 0.05f, 0.055f),
            vec3(0.05f, 0.05f, 0.06f));
        if (fire > 0.55f) {
            add(rightHand, vec3(0.0f, -0.255f, 0.0f), vec3(0.09f, 0.07f, 0.09f),
                vec3(1.0f, 0.72f, 0.18f));
        }
        // Supporting left hand meets the gun when firing.
        if (fire > 0.15f) {
            add(leftHand, vec3(0.0f, -0.04f, -0.02f), vec3(0.10f, 0.08f, 0.10f),
                handsColor);
        }
    }

    // ── Legs ────────────────────────────────────────────────────────────────
    auto leg = [&](float side, float hipDeg, float kneeDeg) {
        mat4 hip = glm::translate(root, vec3(side * kHipHalfX, kHipY, 0.0f));
        hip = glm::rotate(hip, glm::radians(hipDeg), vec3(1, 0, 0));
        add(hip, vec3(0.0f, -0.20f, 0.0f), vec3(0.21f, 0.40f, 0.22f), upperColor);

        mat4 knee = glm::translate(hip, vec3(0.0f, -0.40f, 0.0f));
        knee = glm::rotate(knee, glm::radians(kneeDeg), vec3(1, 0, 0));
        add(knee, vec3(0.0f, -0.20f, 0.0f), vec3(0.19f, 0.40f, 0.20f), lowerColor);

        mat4 ankle = glm::translate(knee, vec3(0.0f, -0.40f, 0.0f));
        add(ankle, vec3(0.0f, -0.05f, 0.06f), vec3(0.21f, 0.10f, 0.32f), feetColor);
    };

    leg(+1.0f, leftHip, leftKnee);
    leg(-1.0f, rightHip, rightKnee);
}

} // namespace humanoid

#endif // HUMANOID_HPP
