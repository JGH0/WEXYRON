#ifndef GAME_PLAYER_HPP
#define GAME_PLAYER_HPP

#include <glm/glm.hpp>

/**
 * @brief Player data and state management
 */
class Player {
public:
    struct PlayerData {
        glm::vec3 position;
        glm::vec3 velocity;
        int health;
        int maxHealth;
        bool isDead;
        float damageCooldown;
        float damageEffectTime;
        int score;
    };

private:
    PlayerData data;

public:
    Player();
    
    // Player management
    void reset();
    void takeDamage(int amount);
    void heal(int amount);
    void addScore(int points);
    
    // Getters
    const glm::vec3& getPosition() const { return data.position; }
    const glm::vec3& getVelocity() const { return data.velocity; }
    int getHealth() const { return data.health; }
    int getMaxHealth() const { return data.maxHealth; }
    bool isDead() const { return data.isDead; }
    float getDamageCooldown() const { return data.damageCooldown; }
    float getDamageEffectTime() const { return data.damageEffectTime; }
    int getScore() const { return data.score; }
    
    // Setters
    void setPosition(const glm::vec3& pos) { data.position = pos; }
    void setVelocity(const glm::vec3& vel) { data.velocity = vel; }
    void setHealth(int health) { data.health = health; }
    void setMaxHealth(int maxHealth) { data.maxHealth = maxHealth; }
    void setDead(bool dead) { data.isDead = dead; }
    void setDamageCooldown(float cooldown) { data.damageCooldown = cooldown; }
    void setDamageEffectTime(float effectTime) { data.damageEffectTime = effectTime; }
    void setScore(int score) { data.score = score; }
    
    // Update
    void update(float deltaTime);
};

#endif
