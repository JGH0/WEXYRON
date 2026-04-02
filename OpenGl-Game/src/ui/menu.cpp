#include "menu.hpp"
#include "../core/renderer.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

Menu::Menu(const std::string& title)
    : title(title), selection(0), menuX(0), menuY(0),
      menuWidth(400.0f), menuHeight(40.0f), itemHeight(40.0f), spacing(50.0f), textScale(1.2f),
      colorNormal(0.3f, 0.5f, 0.7f),
      colorSelected(0.6f, 0.8f, 0.2f),
      colorIndicator(1.0f, 1.0f, 1.0f),
      colorTitleBg(0.8f, 0.4f, 0.1f) {
}

void Menu::addItem(std::shared_ptr<MenuItem> item) {
    items.push_back(item);
}

void Menu::addItem(const std::string& label, std::function<void()> callback) {
    auto item = std::make_shared<MenuItem>(label);
    item->setCallback(callback);
    items.push_back(item);
}

void Menu::addItem(const std::string& label, const std::string& value, std::function<void()> callback) {
    auto item = std::make_shared<MenuItem>(label, value);
    item->setCallback(callback);
    items.push_back(item);
}

void Menu::addSubmenu(const std::string& label, std::shared_ptr<Menu> submenu) {
    auto item = std::make_shared<MenuItem>(label, ">");
    item->setSubmenu(submenu);
    submenu->setParent(shared_from_this());
    items.push_back(item);
}

void Menu::addToggle(const std::string& label, bool* toggleValue) {
    auto item = std::make_shared<MenuItem>(label);
    item->setToggle(toggleValue);
    items.push_back(item);
}

void Menu::addBack(const std::string& label) {
    auto item = std::make_shared<MenuItem>(label, "<");
    item->setBackAction();
    items.push_back(item);
}

void Menu::setParent(std::shared_ptr<Menu> parentMenu) {
    parent = parentMenu;
}

void Menu::moveUp() {
    if (items.empty()) return;
    selection = (selection - 1 + static_cast<int>(items.size())) % static_cast<int>(items.size());
}

void Menu::moveDown() {
    if (items.empty()) return;
    selection = (selection + 1) % static_cast<int>(items.size());
}

void Menu::select() {
    if (items.empty()) return;
    
    auto item = items[selection];
    MenuActionType action = item->getActionType();
    
    switch (action) {
        case MenuActionType::CALLBACK:
        case MenuActionType::TOGGLE:
            item->execute();
            break;
            
        case MenuActionType::SUBMENU:
            currentSubmenu = item->getSubmenu();
            if (currentSubmenu) {
                currentSubmenu->selection = 0; // Reset selection when entering
            }
            break;
            
        case MenuActionType::BACK:
            goBack();
            break;
            
        default:
            break;
    }
}

std::shared_ptr<Menu> Menu::getCurrentMenu() {
    if (currentSubmenu) {
        // Delegate to submenu
        auto submenuCurrent = currentSubmenu->getCurrentMenu();
        if (submenuCurrent != currentSubmenu) {
            // Submenu wants to go back
            if (submenuCurrent == nullptr) {
                currentSubmenu.reset();
                return shared_from_this();
            }
        }
        return submenuCurrent;
    }
    return shared_from_this();
}

void Menu::goBack() {
    if (currentSubmenu) {
        currentSubmenu.reset();
    }
}

std::shared_ptr<MenuItem> Menu::getItem(size_t index) {
    if (index < items.size()) {
        return items[index];
    }
    return nullptr;
}

std::shared_ptr<MenuItem> Menu::getSelectedItem() {
    return getItem(selection);
}

void Menu::render(Renderer& renderer, float screenWidth, float screenHeight) {
    if (currentSubmenu) {
        currentSubmenu->render(renderer, screenWidth, screenHeight);
        return;
    }
    
    if (items.empty()) return;
    
    // Calculate centered position if not set
    float x = (menuX == 0) ? (screenWidth - menuWidth) / 2.0f : menuX;
    float startY = (menuY == 0) ? 150.0f : menuY;
    float titleY = (menuY == 0) ? 80.0f : menuY - 70.0f;
    
    // Render title bar
    glm::mat4 titleModel = glm::mat4(1.0f);
    titleModel = glm::translate(titleModel, glm::vec3(screenWidth / 2.0f, titleY, 0.0f));
    titleModel = glm::scale(titleModel, glm::vec3(600.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(titleModel, colorTitleBg);
    
    // Render title text (centered)
    float titleWidth = title.length() * 12.0f; // Approximate width
    renderer.drawText(title, (screenWidth - titleWidth) / 2.0f, titleY - 8.0f, 1.5f);
    
    // Render menu items
    for (size_t i = 0; i < items.size(); i++) {
        float y = startY + i * spacing;
        bool isSelected = (static_cast<int>(i) == selection);
        
        // Background box
        glm::mat4 bgModel = glm::mat4(1.0f);
        bgModel = glm::translate(bgModel, glm::vec3(x + menuWidth / 2.0f, y + itemHeight / 2.0f, 0.0f));
        bgModel = glm::scale(bgModel, glm::vec3(menuWidth, itemHeight, 1.0f));
        renderer.drawColoredQuad(bgModel, isSelected ? colorSelected : colorNormal);
        
        // Selection indicator
        if (isSelected) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(x + 10.0f, y + itemHeight / 2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, colorIndicator);
        }
        
        // Label text
        renderer.drawText(items[i]->getLabel(), x + 20.0f, y + itemHeight / 2.0f - 8.0f, textScale);
        
        // Value/action indicator on the right
        std::string valueText = items[i]->getValue();
        if (items[i]->getActionType() == MenuActionType::TOGGLE) {
            valueText = items[i]->getToggleState() ? "ON" : "OFF";
        }
        if (!valueText.empty()) {
            float valueWidth = valueText.length() * 8.0f * textScale;
            renderer.drawText(valueText, x + menuWidth - valueWidth - 20.0f, y + itemHeight / 2.0f - 8.0f, textScale);
        }
    }
}
