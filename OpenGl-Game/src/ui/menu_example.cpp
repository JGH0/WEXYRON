// Example usage of the new Menu and TextRenderer system
// This shows how to create menus with entries that can:
// 1. Execute callbacks when selected
// 2. Open submenus
// 3. Toggle boolean values

#include "ui/menu.hpp"
#include "ui/text_renderer.hpp"
#include <iostream>

// Example: Creating a main menu
void createExampleMenu() {
    // Create the main menu
    auto mainMenu = std::make_shared<Menu>("MAIN MENU");
    
    // Add simple items with callbacks
    mainMenu->addItem("Play Game", []() {
        std::cout << "Starting game..." << std::endl;
        // Start game logic here
    });
    
    // Create a map selection submenu
    auto mapMenu = std::make_shared<Menu>("SELECT MAP");
    mapMenu->addItem("Simple", []() { std::cout << "Loading simple map" << std::endl; });
    mapMenu->addItem("Large", []() { std::cout << "Loading large map" << std::endl; });
    mapMenu->addItem("Arena", []() { std::cout << "Loading arena map" << std::endl; });
    mapMenu->addBack("Back");
    
    // Add submenu to main menu
    mainMenu->addSubmenu("Map Selection", mapMenu);
    
    // Add a toggle item (e.g., for infinite ammo)
    bool infiniteAmmo = false;
    mainMenu->addToggle("Infinite Ammo", &infiniteAmmo);
    
    // Add another submenu for settings
    auto settingsMenu = std::make_shared<Menu>("SETTINGS");
    settingsMenu->addItem("Graphics", []() { /* open graphics settings */ });
    settingsMenu->addItem("Audio", []() { /* open audio settings */ });
    settingsMenu->addBack("Back");
    mainMenu->addSubmenu("Settings", settingsMenu);
    
    // Add exit item
    mainMenu->addItem("Exit", []() {
        std::cout << "Exiting game..." << std::endl;
        // Exit logic here
    });
    
    // --- Navigation Example ---
    // In your input handling:
    // - Up arrow: mainMenu->moveUp()
    // - Down arrow: mainMenu->moveDown()
    // - Enter: mainMenu->select()
    // - Escape: mainMenu->goBack() or go to parent
    
    // --- Rendering Example ---
    // In your render function:
    // mainMenu->render(renderer, 800.0f, 600.0f);
}

// Example: Using TextRenderer for different text sizes
void exampleTextRendering(Renderer& renderer) {
    TextRenderer text(renderer);
    
    // Different text sizes
    text.drawTitle("GAME TITLE", 400.0f, 100.0f);           // Large title
    text.drawHeader("Settings", 100.0f, 200.0f);            // Section header
    text.drawNormal("Normal text description", 100.0f, 250.0f);
    text.drawSmall("Small helper text", 100.0f, 300.0f);    // Small text
    
    // Custom colors
    text.drawLarge("Warning!", 100.0f, 350.0f, glm::vec3(1.0f, 0.0f, 0.0f)); // Red
    text.drawNormal("Health: 100", 100.0f, 400.0f, glm::vec3(0.0f, 1.0f, 0.0f)); // Green
    
    // Centered text
    text.drawTextAligned("Centered Title", 400.0f, 50.0f, TextAlign::CENTER, 
                         TextStyle::Title());
    
    // Right-aligned with custom style
    TextStyle style(1.2f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow, 1.2x scale
    text.drawTextAligned("Score: 1000", 780.0f, 20.0f, TextAlign::RIGHT, style);
}

/* 
Key features of the new system:

1. MenuItem class:
   - Can have callbacks (execute code when selected)
   - Can link to submenus (open other menus)
   - Can be toggles (ON/OFF states)
   - Can be back buttons (return to parent menu)

2. Menu class:
   - Manages a list of MenuItems
   - Handles navigation (up/down, select, back)
   - Automatically renders with styled boxes
   - Supports nested submenus
   - Matches the settings page style (blue boxes, green selection, white indicator)

3. TextRenderer class:
   - Small(), Normal(), Large(), Title(), Header() presets
   - Custom colors with withColor()
   - Left/Center/Right alignment
   - Easy multiline text
   - Calculates text width/height

To integrate into your game:
1. Replace renderMapSelection() with a Menu object
2. Handle input by calling menu->moveUp(), moveDown(), select(), goBack()
3. In render, call menu->render(renderer, width, height)
*/
