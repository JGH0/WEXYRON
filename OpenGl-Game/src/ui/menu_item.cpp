#include "menu_item.hpp"
#include "menu.hpp"

MenuItem::MenuItem(const std::string& label) 
    : label(label), value(""), actionType(MenuActionType::NONE), toggleValue(nullptr) {
}

MenuItem::MenuItem(const std::string& label, const std::string& value)
    : label(label), value(value), actionType(MenuActionType::NONE), toggleValue(nullptr) {
}

void MenuItem::setCallback(std::function<void()> callback) {
    this->callback = callback;
    actionType = MenuActionType::CALLBACK;
}

void MenuItem::setSubmenu(std::shared_ptr<Menu> submenu) {
    this->submenu = submenu;
    actionType = MenuActionType::SUBMENU;
}

void MenuItem::setToggle(bool* toggleValue) {
    this->toggleValue = toggleValue;
    actionType = MenuActionType::TOGGLE;
}

void MenuItem::setBackAction() {
    actionType = MenuActionType::BACK;
}

void MenuItem::execute() {
    if (actionType == MenuActionType::CALLBACK && callback) {
        callback();
    } else if (actionType == MenuActionType::TOGGLE && toggleValue) {
        *toggleValue = !(*toggleValue);
    }
}

std::shared_ptr<Menu> MenuItem::getSubmenu() {
    return submenu.lock();
}

bool MenuItem::getToggleState() const {
    return toggleValue ? *toggleValue : false;
}

void MenuItem::toggle() {
    if (toggleValue) {
        *toggleValue = !(*toggleValue);
    }
}

void MenuItem::setValue(const std::string& newValue) {
    value = newValue;
}
