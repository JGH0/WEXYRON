#include "projectile.hpp"
#include "../core/renderer.hpp"
#include "../core/map.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

ProjectileSystem::ProjectileSystem() : activeProjectiles(0) {
    clearAllProjectiles();
}

bool ProjectileSystem::createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = position;
            projectiles[i].velocity = velocity;
            projectiles[i].lifetime = lifetime;
            projectiles[i].active = true;
            activeProjectiles++;
            return true;
        }
    }
    return false;
}

void ProjectileSystem::updateProjectiles(float deltaTime, Map* map) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        
        // Update position
        projectiles[i].position += projectiles[i].velocity * deltaTime;
        
        // Update lifetime
        projectiles[i].lifetime -= deltaTime;
        
        // Check collision with map blocks
        if (map) {
            // Simple boundary check for now
            if (projectiles[i].position.y < -10.0f || projectiles[i].position.y > 20.0f ||
                abs(projectiles[i].position.x) > 50.0f || abs(projectiles[i].position.z) > 50.0f) {
                projectiles[i].active = false;
                activeProjectiles--;
                std::cout << "Projectile out of bounds! Active: " << activeProjectiles << std::endl;
                continue;
            }
        }
        
        // Check if projectile expired
        if (projectiles[i].lifetime <= 0.0f) {
            projectiles[i].active = false;
            activeProjectiles--;
        }
    }
}

void ProjectileSystem::renderProjectiles(Renderer* renderer) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        
        // Render projectile as a small cube
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, projectiles[i].position);
        model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.1f));
        renderer->drawCube(model, glm::vec3(0.0f, 0.0f, 0.0f)); // Black projectiles
    }
}

void ProjectileSystem::clearAllProjectiles() {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
    }
    activeProjectiles = 0;
}
