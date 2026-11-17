/**
 * @file geometry.hpp
 * @brief SpatialKit geometry: Vec3, AABB, rays and continuous (swept) tests.
 *
 * Everything in this header is dependency-free (C++17 standard library only)
 * and header-based, so a game or tool can use the continuous collision tests
 * without linking the spatial grid.
 *
 * Fast projectiles are the motivating use case: a bullet that travels several
 * units per frame must be tested as a segment (previous position -> current
 * position), not as a point at the end of the frame. A point test tunnels
 * straight through small targets.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace spatialkit {

/// Comparison tolerance used by the geometry helpers.
constexpr float kEpsilon = 1.0e-6f;

/**
 * @brief Minimal three-component vector.
 *
 * Deliberately independent of GLM/OpenGL so the library stays engine-agnostic.
 */
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue)
        : x(xValue), y(yValue), z(zValue) {}

    constexpr Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }
    constexpr Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }
    constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
    constexpr Vec3 operator*(float scalar) const {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }
    constexpr Vec3 operator/(float scalar) const {
        return Vec3(x / scalar, y / scalar, z / scalar);
    }
    constexpr Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    constexpr Vec3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    constexpr float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    constexpr float lengthSquared() const { return dot(*this); }
    float length() const { return std::sqrt(lengthSquared()); }

    /// Unit vector; returns a zero vector for a zero-length input.
    Vec3 normalized() const {
        const float len = length();
        return len > kEpsilon ? Vec3(x / len, y / len, z / len) : Vec3{};
    }

    constexpr float& operator[](int axis) {
        return axis == 0 ? x : (axis == 1 ? y : z);
    }
    constexpr float operator[](int axis) const {
        return axis == 0 ? x : (axis == 1 ? y : z);
    }
};

inline constexpr Vec3 operator*(float scalar, const Vec3& value) {
    return value * scalar;
}

inline constexpr Vec3 componentMin(const Vec3& a, const Vec3& b) {
    return Vec3(a.x < b.x ? a.x : b.x,
                a.y < b.y ? a.y : b.y,
                a.z < b.z ? a.z : b.z);
}

inline constexpr Vec3 componentMax(const Vec3& a, const Vec3& b) {
    return Vec3(a.x > b.x ? a.x : b.x,
                a.y > b.y ? a.y : b.y,
                a.z > b.z ? a.z : b.z);
}

/**
 * @brief Axis-aligned bounding box.
 *
 * Overlap is inclusive: boxes that only touch along an edge or face count as
 * intersecting, matching the grid's insert/query behaviour.
 */
struct AABB {
    Vec3 min;
    Vec3 max;

    /// Empty (inverted) box, useful as the identity for merged().
    static constexpr AABB empty() {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return AABB{Vec3(inf, inf, inf), Vec3(-inf, -inf, -inf)};
    }

    static constexpr AABB fromMinMax(const Vec3& minCorner,
                                     const Vec3& maxCorner) {
        return AABB{minCorner, maxCorner};
    }

    static constexpr AABB fromCenterHalfExtents(const Vec3& center,
                                                const Vec3& halfExtents) {
        return AABB{center - halfExtents, center + halfExtents};
    }

    constexpr Vec3 center() const { return (min + max) * 0.5f; }

    constexpr Vec3 halfExtents() const { return (max - min) * 0.5f; }

    /// True when the box has no volume or is inverted.
    constexpr bool isEmpty() const {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    /// Inclusive AABB-AABB overlap test.
    constexpr bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    constexpr bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    /// Squared distance from a point to the nearest point of the box (0 inside).
    constexpr float squaredDistanceTo(const Vec3& point) const {
        const Vec3 nearest = componentMax(min, componentMin(point, max));
        const Vec3 offset = point - nearest;
        return offset.lengthSquared();
    }

    float distanceTo(const Vec3& point) const {
        return std::sqrt(squaredDistanceTo(point));
    }

    /// Grow by the same margin on all six sides.
    constexpr AABB expanded(float margin) const {
        const Vec3 padding(margin, margin, margin);
        return AABB{min - padding, max + padding};
    }

    /// Grow by a per-axis margin (used by the moving-box sweep).
    constexpr AABB expandedBy(const Vec3& margin) const {
        return AABB{min - margin, max + margin};
    }

    constexpr AABB translated(const Vec3& offset) const {
        return AABB{min + offset, max + offset};
    }

    /// Smallest box containing both inputs; works with empty().
    constexpr AABB merged(const AABB& other) const {
        return AABB{componentMin(min, other.min), componentMax(max, other.max)};
    }
};

/**
 * @brief Ray with an optional maximum travel distance.
 *
 * `direction` does not have to be normalized; the intersection helpers
 * normalize it internally, so distances are always world-space units.
 */
struct Ray {
    Vec3 origin;
    Vec3 direction;
    float maxDistance = std::numeric_limits<float>::infinity();

    /// Segment helper: direction points from `from` to `to` and the ray stops
    /// exactly at `to`.
    static Ray fromTo(const Vec3& from, const Vec3& to) {
        Ray ray;
        ray.origin = from;
        ray.direction = to - from;
        ray.maxDistance = ray.direction.length();
        return ray;
    }
};

/// Result of a ray/segment/sweep test.
struct RayHit {
    bool hit = false;
    /// For rays: world-space distance along the ray. For segments: 0..1.
    float t = 0.0f;
    /// World-space distance from the ray origin to the hit point.
    float distance = 0.0f;
    Vec3 point;
    /// Outward surface normal of the box that was entered. A ray starting
    /// inside a box reports a normal facing back towards the origin.
    Vec3 normal;
};

/**
 * @brief Slab-method ray/AABB intersection.
 *
 * Returns the first (nearest) entry point. A ray that starts inside the box
 * reports a hit at distance 0. Zero direction components are handled, so rays
 * exactly parallel to a slab do not produce NaNs.
 */
inline RayHit intersectAABB(const Ray& ray, const AABB& box) {
    RayHit result;
    if (box.isEmpty() || ray.maxDistance < 0.0f) {
        return result;
    }

    const float directionLength = ray.direction.length();
    if (directionLength <= kEpsilon) {
        // Degenerate ray: treat it as a point query.
        if (box.contains(ray.origin)) {
            result.hit = true;
            result.point = ray.origin;
        }
        return result;
    }
    const Vec3 direction = ray.direction / directionLength;

    float tMin = 0.0f;
    float tMax = ray.maxDistance;
    int nearAxis = -1;
    float nearSign = 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float dir = direction[axis];
        const float lo = box.min[axis];
        const float hi = box.max[axis];

        if (std::fabs(dir) <= kEpsilon) {
            // Parallel to this slab: the origin must already be between the
            // two planes, otherwise the ray can never enter the box.
            if (origin < lo || origin > hi) {
                return result;
            }
            continue;
        }

        float tNear = (lo - origin) / dir;
        float tFar = (hi - origin) / dir;
        // Entering through the low side gives a negative normal; entering
        // through the high side (negative direction) gives a positive one.
        float normalSign = -1.0f;
        if (tNear > tFar) {
            std::swap(tNear, tFar);
            normalSign = 1.0f;
        }

        if (tNear > tMin) {
            tMin = tNear;
            nearAxis = axis;
            nearSign = normalSign;
        }
        if (tFar < tMax) {
            tMax = tFar;
        }
        if (tMin > tMax) {
            return result;
        }
    }

    result.hit = true;
    result.t = tMin;
    result.distance = tMin;

    if (nearAxis < 0) {
        // Origin is inside the box: immediate hit at the origin.
        result.point = ray.origin;
        result.normal = -direction;
        return result;
    }

    result.point = ray.origin + direction * tMin;
    result.normal = Vec3(0.0f, 0.0f, 0.0f);
    result.normal[nearAxis] = nearSign;
    return result;
}

/**
 * @brief Swept segment/AABB intersection.
 *
 * This is the core fix for fast bullets: a bullet that moved from `from` to
 * `to` in one frame is tested as the whole segment, so a small target on the
 * way cannot be skipped. `hit.t` is the fraction along the segment (0..1).
 */
inline RayHit intersectSegmentAABB(const Vec3& from, const Vec3& to,
                                   const AABB& box) {
    const Vec3 delta = to - from;
    const float length = delta.length();

    RayHit result;
    if (length <= kEpsilon) {
        if (box.contains(from)) {
            result.hit = true;
            result.point = from;
        }
        return result;
    }

    Ray ray;
    ray.origin = from;
    ray.direction = delta;
    ray.maxDistance = length;

    result = intersectAABB(ray, box);
    if (result.hit) {
        result.t = result.distance / length;
    }
    return result;
}

/// Convenience predicate for callers that only need a yes/no answer.
inline bool segmentIntersectsAABB(const Vec3& from, const Vec3& to,
                                  const AABB& box) {
    return intersectSegmentAABB(from, to, box).hit;
}

/**
 * @brief Swept moving-box vs. static-box test.
 *
 * The moving box keeps its orientation and travels by `delta`. This is the
 * Minkowski-sum trick: grow the static box by the moving box's half extents
 * and sweep the moving box's centre along the segment. Use it for "fat"
 * projectiles (a bullet with a radius) or for moving bodies.
 */
inline RayHit sweepAABB(const AABB& movingBox, const Vec3& delta,
                        const AABB& staticBox) {
    if (movingBox.isEmpty() || staticBox.isEmpty()) {
        return RayHit{};
    }

    const Vec3 halfExtents = movingBox.halfExtents();
    const AABB grownStatic = staticBox.expandedBy(halfExtents);
    const Vec3 startCenter = movingBox.center();
    const Vec3 endCenter = startCenter + delta;

    RayHit result = intersectSegmentAABB(startCenter, endCenter, grownStatic);
    if (result.hit) {
        // Convert the centre-path hit back to the moving box's position.
        result.point = startCenter + delta * result.t;
    }
    return result;
}

/// Convenience predicate for the moving-box sweep.
inline bool sweepIntersectsAABB(const AABB& movingBox, const Vec3& delta,
                                const AABB& staticBox) {
    return sweepAABB(movingBox, delta, staticBox).hit;
}

} // namespace spatialkit
