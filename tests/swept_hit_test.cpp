// Regression test: fast projectiles must not tunnel through a player body.
//
// The gameplay fix tests the whole frame segment (previous position -> current
// position) with SpatialKit instead of only the end point. This test uses the
// same body dimensions as Game (0.8 x 1.8, 0.15 hit slack) and the same speeds,
// so a regression in the continuous-detection contract fails the build.

#include "spatialkit/geometry.hpp"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAIL %d: %s\n", __LINE__, #condition);                \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

using spatialkit::AABB;
using spatialkit::RayHit;
using spatialkit::Vec3;

constexpr float kBodyWidth = 0.8f;
constexpr float kBodyHeight = 1.8f;
constexpr float kHitSlack = 0.15f;

AABB bodyBox(const Vec3& feet) {
    const float half = kBodyWidth * 0.5f;
    return AABB{{feet.x - half - kHitSlack, feet.y - kHitSlack,
                 feet.z - half - kHitSlack},
                {feet.x + half + kHitSlack, feet.y + kBodyHeight + kHitSlack,
                 feet.z + half + kHitSlack}};
}

bool hits(const Vec3& from, const Vec3& to, const AABB& body) {
    return spatialkit::intersectSegmentAABB(from, to, body).hit;
}

} // namespace

int main() {
    const AABB body = bodyBox(Vec3(0.0f, 0.0f, 0.0f));

    // Pistol: 120 units/s at 60 fps -> 2 units per frame. The end point is
    // already behind the body; the old point test at `to` missed the hit.
    const Vec3 pistolFrom(-1.0f, 1.0f, 0.0f);
    const Vec3 pistolTo(1.0f, 1.0f, 0.0f);
    CHECK(hits(pistolFrom, pistolTo, body));
    CHECK(!body.contains(pistolTo));

    const RayHit pistolHit =
        spatialkit::intersectSegmentAABB(pistolFrom, pistolTo, body);
    CHECK(pistolHit.hit);
    CHECK(pistolHit.t > 0.0f && pistolHit.t < 0.5f);
    CHECK(std::fabs(pistolHit.point.x + 0.55f) < 1.0e-3f); // body surface

    // Even faster / lower frame rate: 300 units/s at 20 fps -> 15 units.
    CHECK(hits(Vec3(-7.5f, 1.0f, 0.0f), Vec3(7.5f, 1.0f, 0.0f), body));

    // Shotgun pellets use the same sweep, just with a spread direction.
    CHECK(hits(Vec3(-2.0f, 1.4f, 0.1f), Vec3(3.0f, 1.2f, -0.1f), body));

    // Passing above the head or behind the body must still miss.
    CHECK(!hits(Vec3(-1.0f, 2.1f, 0.0f), Vec3(1.0f, 2.1f, 0.0f), body));
    CHECK(!hits(Vec3(1.0f, 1.0f, 0.0f), Vec3(3.0f, 1.0f, 0.0f), body));

    // A fat bullet (radius 0.05) is the swept moving-box variant.
    const AABB bullet = AABB::fromCenterHalfExtents(
        Vec3(-1.0f, 1.0f, 0.0f), Vec3(0.05f, 0.05f, 0.05f));
    CHECK(spatialkit::sweepAABB(bullet, Vec3(10.0f, 0.0f, 0.0f),
                                bodyBox(Vec3(4.0f, 0.0f, 0.0f))).hit);

    if (failures == 0) {
        std::printf("SWEPT HIT TEST: ALL OK\n");
        return 0;
    }
    std::printf("SWEPT HIT TEST: %d FAILURES\n", failures);
    return 1;
}
