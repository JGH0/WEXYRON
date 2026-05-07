#include "enemy.hpp"
#include "../core/renderer.hpp"
#include <iostream>
#include <random>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

Enemy::Enemy() : activeEnemies(0) {
    clearAllEnemies();
}

void Enemy::spawnEnemy(const glm::vec3& position) {
    if (activeEnemies >= MAX_ENEMIES) {
        std::cout << "Max enemies reached!" << std::endl;
        return;
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].position = position;
            enemies[i].velocity = glm::vec3(0.0f, 0.0f, 0.0f);
            enemies[i].health = 50.0f;
            enemies[i].maxHealth = 50.0f;
            enemies[i].active = true;
            enemies[i].alive = true;
            enemies[i].damageCooldown = 0.0f;
            activeEnemies++;
            std::cout << "Spawned enemy at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
            break;
        }
    }
}

void Enemy::updateEnemies(float deltaTime) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || !enemies[i].alive) continue;
        
        // Simple AI - move towards player (this would need player position)
        // For now, just simple random movement
        if (rand() % 100 < 2) { // 2% chance to change direction
            enemies[i].velocity.x = (rand() % 3 - 1) * 2.0f;
            enemies[i].velocity.z = (rand() % 3 - 1) * 2.0f;
        }
        
        // Update position
        enemies[i].position += enemies[i].velocity * deltaTime;
        
        // Update damage cooldown
        if (enemies[i].damageCooldown > 0.0f) {
            enemies[i].damageCooldown -= deltaTime;
        }
        
        // Check if enemy is dead
        if (enemies[i].health <= 0.0f) {
            enemies[i].alive = false;
            enemies[i].active = false;
            activeEnemies--;
            std::cout << "Enemy killed!" << std::endl;
        }
    }
}

void Enemy::renderEnemies(Renderer* renderer) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || !enemies[i].alive) continue;
        
        // Render enemy as a red cube
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, enemies[i].position);
        model = glm::scale(model, glm::vec3(0.5f, 0.8f, 0.5f));
        
        // Color based on health
        float healthPercent = enemies[i].health / enemies[i].maxHealth;
        glm::vec3 enemyColor = glm::vec3(0.8f * (1.0f - healthPercent), 0.2f, 0.2f);
        renderer->drawCube(model, enemyColor);
    }
}

void Enemy::checkProjectileCollision(const glm::vec3& projectilePos, float damage) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active || !enemies[i].alive) continue;
        if (enemies[i].damageCooldown > 0.0f) continue;
        
        // Simple sphere collision
        float distance = glm::length(enemies[i].position - projectilePos);
        if (distance < 1.0f) { // Hit radius
            enemies[i].health -= damage;
            enemies[i].damageCooldown = 0.5f; // 0.5 second invincibility
            std::cout << "Enemy hit! Health: " << enemies[i].health << std::endl;
            
            if (enemies[i].health <= 0.0f) {
                enemies[i].alive = false;
                enemies[i].active = false;
                activeEnemies--;
                std::cout << "Enemy killed!" << std::endl;
            }
            break;
        }
    }
}

void Enemy::clearAllEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive = false;
    }
    activeEnemies = 0;
}
