#include "player.hpp"
#include <iostream>

Player::Player() {
    reset();
}

void Player::reset() {
    data.position = glm::vec3(0.0f, 2.0f, 0.0f);
    data.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
    data.health = 100;
    data.maxHealth = 100;
    data.isDead = false;
    data.damageCooldown = 0.0f;
    data.damageEffectTime = 0.0f;
    data.score = 0;
}

void Player::takeDamage(int amount) {
    if (data.damageCooldown <= 0.0f && !data.isDead) {
        data.health -= amount;
        data.damageCooldown = 1.0f; // 1 second invincibility
        data.damageEffectTime = 0.5f; // 0.5 second red flash effect
        
        if (data.health <= 0) {
            data.health = 0;
            data.isDead = true;
        }
        
        std::cout << "Player hit! Health: " << data.health << "/" << data.maxHealth << std::endl;
    }
}

void Player::heal(int amount) {
    data.health += amount;
    if (data.health > data.maxHealth) {
        data.health = data.maxHealth;
    }
}

void Player::addScore(int points) {
    data.score += points;
}

void Player::update(float deltaTime) {
    // Update damage cooldown
    if (data.damageCooldown > 0.0f) {
        data.damageCooldown -= deltaTime;
    }
    
    // Update damage effect time
    if (data.damageEffectTime > 0.0f) {
        data.damageEffectTime -= deltaTime;
    }
}
