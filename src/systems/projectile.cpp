#include "projectile.hpp"
#include "engine/renderer.hpp"
#include "engine/map.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
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
        
        // Store old position for collision detection
        glm::vec3 oldPosition = projectiles[i].position;
        
        // Update position
        projectiles[i].position += projectiles[i].velocity * deltaTime;
        
        // Update lifetime
        projectiles[i].lifetime -= deltaTime;
        
        // Check collision with map blocks
        if (map) {
            // Check block collision with small radius (projectile size)
            if (map->checkCollision(projectiles[i].position, 0.1f)) {
                projectiles[i].active = false;
                activeProjectiles--;
                std::cout << "Projectile hit block! Active: " << activeProjectiles << std::endl;
                continue;
            }
            
            // Boundary check as fallback: use the loaded map's extent so
            // projectiles do not vanish in mid-air on large maps.
            const glm::vec3 half = map->getDimensions() * 0.5f;
            if (projectiles[i].position.y < -10.0f ||
                projectiles[i].position.y > half.y * 2.0f + 5.0f ||
                std::abs(projectiles[i].position.x) > half.x + 2.0f ||
                std::abs(projectiles[i].position.z) > half.z + 2.0f) {
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
