#ifndef MENU_ITEM_HPP
#define MENU_ITEM_HPP

#include <string>
#include <functional>
#include <memory>

// Forward declarations
class Menu;

// Types of menu actions
enum class MenuActionType {
    NONE,           // No action
    CALLBACK,       // Execute a function
    SUBMENU,        // Open another menu
    TOGGLE,         // Toggle a boolean value
    BACK            // Go back to previous menu
};

// Menu item class for individual entries
class MenuItem {
public:
    MenuItem(const std::string& label);
    MenuItem(const std::string& label, const std::string& value);
    
    // Set actions
    void setCallback(std::function<void()> callback);
    void setSubmenu(std::shared_ptr<Menu> submenu);
    void setToggle(bool* toggleValue);
    void setBackAction();
    
    // Getters
    const std::string& getLabel() const { return label; }
    const std::string& getValue() const { return value; }
    MenuActionType getActionType() const { return actionType; }
    
    // Action execution
    void execute();
    std::shared_ptr<Menu> getSubmenu();
    
    // For toggle items
    bool getToggleState() const;
    void toggle();
    
    // Set display value
    void setValue(const std::string& newValue);
    
private:
    std::string label;
    std::string value;
    MenuActionType actionType;
    
    // Action data
    std::function<void()> callback;
    std::weak_ptr<Menu> submenu;
    bool* toggleValue;
};

#endif // MENU_ITEM_HPP
