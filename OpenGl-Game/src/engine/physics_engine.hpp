#ifndef PHYSICS_ENGINE_HPP
#define PHYSICS_ENGINE_HPP

#include <vector>
#include <memory>
#include <glm/glm.hpp>

// Forward declarations
class RigidBody;
class Collider;
class PhysicsWorld;

/**
 * @brief Physics engine system
 * 
 * Handles physics simulation including:
 * - Rigid body dynamics
 * - Collision detection and response
 * - Forces and impulses
 * - Raycasting and queries
 */
class PhysicsEngine {
public:
    struct PhysicsConfig {
        glm::vec3 gravity;
        float timeStep;
        int maxSubSteps;
        bool enableDebugDraw;
        
        PhysicsConfig() : gravity(0.0f, -9.81f, 0.0f), timeStep(1.0f / 60.0f),
                         maxSubSteps(4), enableDebugDraw(false) {}
    };

private:
    PhysicsConfig config;
    std::unique_ptr<PhysicsWorld> world;
    std::vector<std::shared_ptr<RigidBody>> rigidBodies;
    
    bool isInitialized;

public:
    PhysicsEngine();
    ~PhysicsEngine();
    
    // Physics engine lifecycle
    bool initialize(const PhysicsConfig& config);
    void shutdown();
    void update(float deltaTime);
    
    // Rigid body management
    std::shared_ptr<RigidBody> createRigidBody();
    void removeRigidBody(std::shared_ptr<RigidBody> body);
    void clearAllRigidBodies();
    
    // Physics queries
    bool raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, glm::vec3& hitPoint, glm::vec3& hitNormal);
    std::vector<std::shared_ptr<RigidBody>> getBodiesInSphere(const glm::vec3& center, float radius);
    std::vector<std::shared_ptr<RigidBody>> getBodiesInBox(const glm::vec3& min, const glm::vec3& max);
    
    // Configuration
    void setGravity(const glm::vec3& gravity);
    const glm::vec3& getGravity() const { return config.gravity; }
    void setTimeStep(float timeStep);
    float getTimeStep() const { return config.timeStep; }
    
    // Debug
    void setDebugDraw(bool enable);
    void renderDebug();
    
    // Statistics
    int getBodyCount() const { return rigidBodies.size(); }
    const PhysicsConfig& getConfig() const { return config; }

private:
    void stepPhysics(float deltaTime);
    void updateCollisions();
};

/**
 * @brief Rigid body component for physics simulation
 */
class RigidBody {
public:
    enum class BodyType {
        Static,
        Dynamic,
        Kinematic
    };
    
    enum class ShapeType {
        Box,
        Sphere,
        Capsule,
        Mesh
    };

private:
    BodyType bodyType = BodyType::Dynamic;
    ShapeType shapeType = ShapeType::Box;
    
    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    // Physics properties
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    float mass = 1.0f;
    float restitution = 0.5f;
    float friction = 0.5f;
    
    // Forces
    glm::vec3 force = glm::vec3(0.0f);
    glm::vec3 torque = glm::vec3(0.0f);
    
    bool isActive = true;
    bool isGrounded = false;

public:
    RigidBody() = default;
    virtual ~RigidBody() = default;
    
    // Transform
    const glm::vec3& getPosition() const { return position; }
    void setPosition(const glm::vec3& pos) { position = pos; }
    const glm::vec3& getRotation() const { return rotation; }
    void setRotation(const glm::vec3& rot) { rotation = rot; }
    const glm::vec3& getScale() const { return scale; }
    void setScale(const glm::vec3& scl) { scale = scl; }
    
    // Physics properties
    BodyType getBodyType() const { return bodyType; }
    void setBodyType(BodyType type) { bodyType = type; }
    ShapeType getShapeType() const { return shapeType; }
    void setShapeType(ShapeType type) { shapeType = type; }
    
    // Motion
    const glm::vec3& getVelocity() const { return velocity; }
    void setVelocity(const glm::vec3& vel) { velocity = vel; }
    const glm::vec3& getAngularVelocity() const { return angularVelocity; }
    void setAngularVelocity(const glm::vec3& angVel) { angularVelocity = angVel; }
    
    // Physical properties
    float getMass() const { return mass; }
    void setMass(float m) { mass = m; }
    float getRestitution() const { return restitution; }
    void setRestitution(float r) { restitution = r; }
    float getFriction() const { return friction; }
    void setFriction(float f) { friction = f; }
    
    // Forces
    void addForce(const glm::vec3& force) { this->force += force; }
    void addImpulse(const glm::vec3& impulse) { velocity += impulse / mass; }
    void clearForces() { force = glm::vec3(0.0f); torque = glm::vec3(0.0f); }
    
    // State
    bool getIsActive() const { return isActive; }
    void setActive(bool active) { isActive = active; }
    bool getIsGrounded() const { return isGrounded; }
    void setIsGrounded(bool grounded) { isGrounded = grounded; }
};

#endif
