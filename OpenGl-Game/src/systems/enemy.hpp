#ifndef ENEMY_HPP
#define ENEMY_HPP

#include <glm/glm.hpp>

/**
 * @brief Enemy entity system
 */
class Enemy {
public:
    struct EnemyData {
        glm::vec3 position;
        glm::vec3 velocity;
        float health;
        float maxHealth;
        bool active;
        bool alive;
        float damageCooldown;
    };

private:
    static const int MAX_ENEMIES = 10;
    EnemyData enemies[MAX_ENEMIES];
    int activeEnemies;

public:
    Enemy();
    
    // Enemy management
    void spawnEnemy(const glm::vec3& position);
    void updateEnemies(float deltaTime);
    void renderEnemies(class Renderer* renderer);
    void checkProjectileCollision(const glm::vec3& projectilePos, float damage);
    
    // Getters
    int getActiveEnemyCount() const { return activeEnemies; }
    EnemyData* getEnemies() { return enemies; }
    
    // Cleanup
    void clearAllEnemies();
};

#endif
