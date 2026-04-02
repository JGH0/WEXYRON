#ifndef MENU_HPP
#define MENU_HPP

#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <glm/glm.hpp>
#include "menu_item.hpp"

// Forward declaration
class Renderer;

// Menu class for managing a collection of menu items
class Menu : public std::enable_shared_from_this<Menu> {
public:
    Menu(const std::string& title);
    
    // Menu management
    void addItem(std::shared_ptr<MenuItem> item);
    void addItem(const std::string& label, std::function<void()> callback);
    void addItem(const std::string& label, const std::string& value, std::function<void()> callback);
    void addSubmenu(const std::string& label, std::shared_ptr<Menu> submenu);
    void addToggle(const std::string& label, bool* toggleValue);
    void addBack(const std::string& label = "Back");
    
    // Navigation
    void moveUp();
    void moveDown();
    void select();
    std::shared_ptr<Menu> getCurrentMenu();
    void goBack();
    
    // Getters
    const std::string& getTitle() const { return title; }
    int getSelection() const { return selection; }
    size_t getItemCount() const { return items.size(); }
    std::shared_ptr<MenuItem> getItem(size_t index);
    std::shared_ptr<MenuItem> getSelectedItem();
    std::shared_ptr<Menu> getParent() { return parent.lock(); }
    
    // Rendering
    void render(Renderer& renderer, float screenWidth = 800.0f, float screenHeight = 600.0f);
    void setPosition(float x, float y) { menuX = x; menuY = y; }
    void setSize(float width, float height) { menuWidth = width; menuHeight = height; }
    
    // Parent menu for navigation back
    void setParent(std::shared_ptr<Menu> parentMenu);
    
private:
    std::string title;
    std::vector<std::shared_ptr<MenuItem>> items;
    int selection;
    
    // Navigation stack
    std::weak_ptr<Menu> parent;
    std::shared_ptr<Menu> currentSubmenu;
    
    // Rendering parameters
    float menuX, menuY;
    float menuWidth, menuHeight;
    float itemHeight;
    float spacing;
    float textScale;
    
    // Colors (matching settings style)
    glm::vec3 colorNormal;
    glm::vec3 colorSelected;
    glm::vec3 colorIndicator;
    glm::vec3 colorTitleBg;
};

#endif // MENU_HPP
