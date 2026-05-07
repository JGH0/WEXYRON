#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void Game::renderEditor() {
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);

    renderer.clear();

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                            (float)fbW / (float)fbH, 0.1f, 1000.0f);
    renderer.setViewProjection(view, projection);

    // Render the map
    if (currentMap) {
        renderer.renderMap(currentMap);
        // Render spawn points (only visible in editor mode)
        renderer.renderSpawnPoints(currentMap, true);
    }

    // Render grid lines on ground - grid size now matches block size
    if (editorShowGrid) {
        // Use the largest block dimension as the grid cell size
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));

        // Static grid covering the map area (no rotation - always aligned with world axes)
        glm::vec3 gridCenter = glm::vec3(0.0f, 0.0f, 0.0f); // Center of map at ground level

        // Get map dimensions to determine grid size
        float mapHalfX = 20.0f;
        float mapHalfZ = 20.0f;
        if (currentMap) {
            glm::vec3 dims = currentMap->getDimensions();
            mapHalfX = dims.x * 0.5f;
            mapHalfZ = dims.z * 0.5f;
        }

        // Calculate number of grid lines based on map size
        int numLinesX = static_cast<int>(mapHalfX / gridCellSize) + 1;
        int numLinesZ = static_cast<int>(mapHalfZ / gridCellSize) + 1;

        for (int i = -numLinesX; i <= numLinesX; i++) {
            // X lines (static, no rotation)
            glm::mat4 lineX = glm::mat4(1.0f);
            glm::vec3 lineStart = gridCenter + glm::vec3(i * gridCellSize, 0.0f, 0.0f);
            lineX = glm::translate(lineX, lineStart);
            lineX = glm::scale(lineX, glm::vec3(0.02f, 0.02f, mapHalfZ * 2.0f));
            renderer.drawCube(lineX, glm::vec3(0.3f, 0.3f, 0.3f));
        }

        for (int i = -numLinesZ; i <= numLinesZ; i++) {
            // Z lines (static, no rotation)
            glm::mat4 lineZ = glm::mat4(1.0f);
            glm::vec3 lineZStart = gridCenter + glm::vec3(0.0f, 0.0f, i * gridCellSize);
            lineZ = glm::translate(lineZ, lineZStart);
            lineZ = glm::scale(lineZ, glm::vec3(mapHalfX * 2.0f, 0.02f, 0.02f));
            renderer.drawCube(lineZ, glm::vec3(0.3f, 0.3f, 0.3f));
        }
    }

    // Render ghost block at grid position - wireframe style (edges only)
    if (editorMode == MODE_BLOCK) {
        glm::vec3 center = editorGridPos;
        float edgeThickness = 0.02f;
        glm::vec3 edgeColor = glm::vec3(0.0f, 1.0f, 0.0f); // Bright green edges

        glm::vec3 halfSize = editorBlockSize * 0.5f;
        float xEdgeLen = editorBlockSize.x - edgeThickness;
        float yEdgeLen = editorBlockSize.y - edgeThickness;
        float zEdgeLen = editorBlockSize.z - edgeThickness;

        // Create rotation matrix
        glm::mat4 rotationMat = glm::mat4(1.0f);
        rotationMat = glm::rotate(rotationMat, glm::radians(editorBlockPitch), glm::vec3(1.0f, 0.0f, 0.0f));
        rotationMat = glm::rotate(rotationMat, glm::radians(editorBlockRotation), glm::vec3(0.0f, 1.0f, 0.0f));

        // Vertical edges (4 corners) - Y direction
        for (float x = -1; x <= 1; x += 2) {
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                glm::vec3 offset = glm::vec3(x * halfSize.x, 0.0f, z * halfSize.z);
                edge = glm::translate(edge, center);
                edge = edge * rotationMat;
                edge = glm::translate(edge, offset);
                edge = glm::scale(edge, glm::vec3(edgeThickness, yEdgeLen, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
        }
        // Horizontal edges at top and bottom
        for (float y = -1; y <= 1; y += 2) {
            // X-direction edges
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                glm::vec3 offset = glm::vec3(0.0f, y * halfSize.y, z * halfSize.z);
                edge = glm::translate(edge, center);
                edge = edge * rotationMat;
                edge = glm::translate(edge, offset);
                edge = glm::scale(edge, glm::vec3(xEdgeLen, edgeThickness, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
            // Z-direction edges
            for (float x = -1; x <= 1; x += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                glm::vec3 offset = glm::vec3(x * halfSize.x, y * halfSize.y, 0.0f);
                edge = glm::translate(edge, center);
                edge = edge * rotationMat;
                edge = glm::translate(edge, offset);
                edge = glm::scale(edge, glm::vec3(edgeThickness, edgeThickness, zEdgeLen));
                renderer.drawCube(edge, edgeColor);
            }
        }
    }

    // SHAPE mode - render simple indicator
    if (editorMode == MODE_SHAPE) {
        // Ensure 3D projection is set
        renderer.setViewProjection(view, projection);

        // Simple indicator at grid position
        glm::mat4 indicator = glm::mat4(1.0f);
        indicator = glm::translate(indicator, editorGridPos);
        indicator = glm::scale(indicator, glm::vec3(0.3f, 0.3f, 0.3f));
        renderer.drawCube(indicator, glm::vec3(1.0f, 0.0f, 1.0f)); // Purple indicator
    }

    // Switch to 2D UI projection
    UIHelpers::setProjection(renderer, window);
    renderCrosshair();

    // Render editor UI overlay (single pass)
    auto _b = UIHelpers::getBounds(window);
    float left=_b.left, top=_b.top;

    float textScale  = UIHelpers::uiScale(window, 0.65f, 0.5f, 0.9f);
    float lineHeight = std::max(18.0f, 22.0f * UIHelpers::uiScale(window));
    float startX     = std::max(10.0f, left + 12.0f);
    float startY     = std::max(10.0f, top  + 12.0f);
    float maxHelpWidth = _b.width() * 0.45f;

    renderer.drawText("MAP EDITOR", startX, startY, textScale);
    renderer.drawTextFit("WASD=move SPACE/SHIFT=up/down",             startX, startY + lineHeight,      maxHelpWidth, textScale);
    renderer.drawTextFit("LMB=place RCLICK=retex ALT+RCLICK=delete",  startX, startY + lineHeight * 2,  maxHelpWidth, textScale);
    renderer.drawTextFit("1-4=tex T=rot S+T=back C+T=pitchdown",      startX, startY + lineHeight * 3, maxHelpWidth, textScale);
    renderer.drawTextFit("SC+T=pitchup E/Q=resize M=mode",            startX, startY + lineHeight * 4, maxHelpWidth, textScale);
    renderer.drawTextFit("arrows=move UP/DOWN=dist F1 DM F2 A F3 B", startX, startY + lineHeight * 5,  maxHelpWidth, textScale);
    renderer.drawTextFit("F4 Block F5 BombA F6 BombB F9 save ESC exit", startX, startY + lineHeight * 6,  maxHelpWidth, textScale);

    // Show mode-specific help
    if (editorMode == MODE_SHAPE) {
        renderer.drawTextFit("SHAPE MODE: Click to place points (simplified)", startX, startY + lineHeight * 7, maxHelpWidth, textScale);
        renderer.drawTextFit("Press M to switch back to BLOCK mode", startX, startY + lineHeight * 8, maxHelpWidth, textScale);
    }

    // Show current settings
    char buf[128];
    snprintf(buf, sizeof(buf), "POS: %.1f, %.1f, %.1f", editorGridPos.x, editorGridPos.y, editorGridPos.z);
    renderer.drawText(buf, startX, startY + lineHeight * 8, textScale);
    snprintf(buf, sizeof(buf), "SIZE: %.1f, %.1f, %.1f", editorBlockSize.x, editorBlockSize.y, editorBlockSize.z);
    renderer.drawText(buf, startX, startY + lineHeight * 9, textScale);
    snprintf(buf, sizeof(buf), "TEX: %d  DIST: %.1f", editorSelectedTexture, editorBlockOffset);
    renderer.drawText(buf, startX, startY + lineHeight * 10, textScale);
    snprintf(buf, sizeof(buf), "GRID: %s  NOCLIP: %s", editorShowGrid ? "ON" : "OFF", editorNoclip ? "ON" : "OFF");
    renderer.drawText(buf, startX, startY + lineHeight * 11, textScale);
    snprintf(buf, sizeof(buf), "ROT: %.0f  PITCH: %.0f", editorBlockRotation, editorBlockPitch);
    renderer.drawText(buf, startX, startY + lineHeight * 12, textScale);

    // Show spawn mode
    const char* spawnModeStr = "BLOCK MODE";
    switch (editorSpawnMode) {
        case SPAWN_DEATHMATCH:  spawnModeStr = "DM SPAWN MODE";            break;
        case SPAWN_TEAM_A:      spawnModeStr = "TEAM A SPAWN (Red)";      break;
        case SPAWN_TEAM_B:      spawnModeStr = "TEAM B SPAWN (Blue)";     break;
        case SPAWN_BOMB_SITE_A: spawnModeStr = "BOMB SITE A (Yellow)";    break;
        case SPAWN_BOMB_SITE_B: spawnModeStr = "BOMB SITE B (Orange)";    break;
        default: break;
    }
    snprintf(buf, sizeof(buf), "SPAWN: %s", spawnModeStr);
    renderer.drawText(buf, startX, startY + lineHeight * 13, textScale);

    // Show editor mode
    const char* modeStr = editorMode == MODE_BLOCK ? "BLOCK MODE" : "SHAPE MODE";
    snprintf(buf, sizeof(buf), "EDIT: %s", modeStr);
    renderer.drawText(buf, startX, startY + lineHeight * 14, textScale);

    // Texture color palette (matching the map texture IDs)
    glm::vec3 textureColors[8] = {
        glm::vec3(0.4f, 0.8f, 0.4f),  // 0 = grass (green)
        glm::vec3(0.6f, 0.6f, 0.6f),  // 1 = stone (gray)
        glm::vec3(0.8f, 0.4f, 0.3f),  // 2 = brick (red-brown)
        glm::vec3(0.5f, 0.5f, 0.6f),  // 3 = metal (blue-gray)
        glm::vec3(0.8f, 0.6f, 0.4f),  // 4 = wood (brown)
        glm::vec3(0.9f, 0.8f, 0.5f),  // 5 = sand (tan)
        glm::vec3(0.7f, 0.9f, 0.9f),  // 6 = ice (light blue)
        glm::vec3(0.5f, 0.4f, 0.3f)   // 7 = dirt (dark brown)
    };

    // Texture preview in top-right corner
    float previewSize = 80.0f;
    float previewX    = _b.right - previewSize - 40.0f;
    float previewY    = _b.top   + 40.0f;

    // Ensure texture index is valid
    int texIndex = editorSelectedTexture;
    if (texIndex < 0) texIndex = 0;
    if (texIndex > 7) texIndex = 7;

    // Black background
    glm::mat4 bgModel = glm::mat4(1.0f);
    bgModel = glm::translate(bgModel, glm::vec3(previewX, previewY, 0.0f));
    bgModel = glm::scale(bgModel, glm::vec3(previewSize + 8.0f, previewSize + 8.0f, 1.0f));
    renderer.drawColoredQuad(bgModel, glm::vec3(0.0f, 0.0f, 0.0f));

    // White border
    glm::mat4 borderModel = glm::mat4(1.0f);
    borderModel = glm::translate(borderModel, glm::vec3(previewX, previewY, 0.0f));
    borderModel = glm::scale(borderModel, glm::vec3(previewSize + 6.0f, previewSize + 6.0f, 1.0f));
    renderer.drawColoredQuad(borderModel, glm::vec3(1.0f, 1.0f, 1.0f));

    // Colored preview
    glm::mat4 previewModel = glm::mat4(1.0f);
    previewModel = glm::translate(previewModel, glm::vec3(previewX, previewY, 0.0f));
    previewModel = glm::scale(previewModel, glm::vec3(previewSize, previewSize, 1.0f));
    renderer.drawColoredQuad(previewModel, textureColors[texIndex]);

    // Label below the preview box
    char previewLabel[32];
    snprintf(previewLabel, sizeof(previewLabel), "TEX %d", editorSelectedTexture);
    renderer.drawText(previewLabel, previewX, previewY + previewSize + 15.0f, textScale);
}

void Game::processEditorInput() {
    // All one-shot key states are in inp.xxx (no static bools here)
    const double placeInterval = 0.25; // Seconds between placements when holding

    float moveSpeed = editorSpeed * 0.016f;

    // Flying camera movement (WASD + Space/Shift)
    glm::vec3 moveDir(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        moveDir += camera.Front;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        moveDir -= camera.Front;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        moveDir -= camera.Right;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        moveDir += camera.Right;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Normalize and apply movement
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        camera.Position += moveDir * moveSpeed;
        // Update editor grid position to follow camera
        editorGridPos = camera.Position + camera.Front * editorBlockOffset;
    }

    // UP/DOWN arrows move block placement forward/backward along camera direction
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upArrowPressed) {
        inp.upArrowPressed = true;
        editorBlockOffset += editorGridSize;
        if (editorBlockOffset > 20.0f) editorBlockOffset = 20.0f;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        inp.upArrowPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downArrowPressed) {
        inp.downArrowPressed = true;
        editorBlockOffset -= editorGridSize;
        if (editorBlockOffset < 0.5f) editorBlockOffset = 0.5f;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        inp.downArrowPressed = false;
    }

    // Calculate base placement position from where camera is looking
    glm::vec3 lookPos = camera.Position + camera.Front * editorBlockOffset;

    // LEFT/RIGHT arrows move placement sideways (perpendicular to camera)
    static int gridCellX = 0;
    static int gridCellZ = 0;

    if (editorShowGrid) {
        // Calculate grid cell size
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));

        // In grid mode, LEFT/RIGHT moves between grid cells
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !inp.leftArrowPressed) {
            inp.leftArrowPressed = true;
            gridCellX--;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            inp.leftArrowPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !inp.rightArrowPressed) {
            inp.rightArrowPressed = true;
            gridCellX++;
        } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
            inp.rightArrowPressed = false;
        }

        // Calculate grid position based on where player is looking + cell offsets
        // Transform look position into block-local space (inverse full rotation)
        glm::mat4 inverseRotationMat = glm::mat4(1.0f);
        inverseRotationMat = glm::rotate(inverseRotationMat, glm::radians(-editorBlockRotation), glm::vec3(0.0f, 1.0f, 0.0f));
        inverseRotationMat = glm::rotate(inverseRotationMat, glm::radians(-editorBlockPitch), glm::vec3(1.0f, 0.0f, 0.0f));

        // Use the actual look position (not clamped) for placement - allows placing in air
        glm::vec3 localLookPos = glm::vec3(inverseRotationMat * glm::vec4(lookPos, 1.0f));

        // Snap to grid in local space
        float baseGridX = floor(localLookPos.x / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridY = floor(localLookPos.y / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridZ = floor(localLookPos.z / gridCellSize) * gridCellSize + gridCellSize * 0.5f;

        glm::vec3 gridOffset = glm::vec3(gridCellX * gridCellSize, 0.0f, gridCellZ * gridCellSize);

        // Combine base grid and offset in local space
        glm::vec3 localGridPos = glm::vec3(baseGridX + gridOffset.x, baseGridY, baseGridZ + gridOffset.z);

        // Transform back to world space with forward full rotation
        glm::mat4 rotationMat = glm::mat4(1.0f);
        rotationMat = glm::rotate(rotationMat, glm::radians(editorBlockPitch), glm::vec3(1.0f, 0.0f, 0.0f));
        rotationMat = glm::rotate(rotationMat, glm::radians(editorBlockRotation), glm::vec3(0.0f, 1.0f, 0.0f));
        editorGridPos = glm::vec3(rotationMat * glm::vec4(localGridPos, 1.0f));
    } else {
        // Free mode: use smooth sideways offset
        float moveStep = 0.5f;

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !inp.leftArrowPressed) {
            inp.leftArrowPressed = true;
            inp.sidewaysOffset -= moveStep;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            inp.leftArrowPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !inp.rightArrowPressed) {
            inp.rightArrowPressed = true;
            inp.sidewaysOffset += moveStep;
        } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
            inp.rightArrowPressed = false;
        }

        // Use look position + sideways offset
        glm::vec3 placementPos = lookPos + camera.Right * inp.sidewaysOffset;

        // Free placement mode: snap to nearest block face per axis (sticky edges)
        glm::vec3 snapPos = placementPos;
        float snapThreshold = 1.0f;

        if (currentMap) {
            const auto& blocks = currentMap->getBlocks();

            float bestDistX = snapThreshold, bestDistY = snapThreshold, bestDistZ = snapThreshold;
            float bestSnapX = placementPos.x, bestSnapY = placementPos.y, bestSnapZ = placementPos.z;

            for (const auto& block : blocks) {
                glm::vec3 blockHalfSize = block.scale * 0.5f;
                glm::vec3 blockMin = block.position - blockHalfSize;
                glm::vec3 blockMax = block.position + blockHalfSize;

                float dXMin = fabs(placementPos.x - (blockMin.x - editorBlockSize.x * 0.5f));
                float dXMax = fabs(placementPos.x - (blockMax.x + editorBlockSize.x * 0.5f));
                if (dXMin < bestDistX) { bestDistX = dXMin; bestSnapX = blockMin.x - editorBlockSize.x * 0.5f; }
                if (dXMax < bestDistX) { bestDistX = dXMax; bestSnapX = blockMax.x + editorBlockSize.x * 0.5f; }

                float dYMin = fabs(placementPos.y - (blockMin.y - editorBlockSize.y * 0.5f));
                float dYMax = fabs(placementPos.y - (blockMax.y + editorBlockSize.y * 0.5f));
                if (dYMin < bestDistY) { bestDistY = dYMin; bestSnapY = blockMin.y - editorBlockSize.y * 0.5f; }
                if (dYMax < bestDistY) { bestDistY = dYMax; bestSnapY = blockMax.y + editorBlockSize.y * 0.5f; }

                float dZMin = fabs(placementPos.z - (blockMin.z - editorBlockSize.z * 0.5f));
                float dZMax = fabs(placementPos.z - (blockMax.z + editorBlockSize.z * 0.5f));
                if (dZMin < bestDistZ) { bestDistZ = dZMin; bestSnapZ = blockMin.z - editorBlockSize.z * 0.5f; }
                if (dZMax < bestDistZ) { bestDistZ = dZMax; bestSnapZ = blockMax.z + editorBlockSize.z * 0.5f; }
            }

            if (bestDistX < snapThreshold) snapPos.x = bestSnapX;
            if (bestDistY < snapThreshold) snapPos.y = bestSnapY;
            if (bestDistZ < snapThreshold) snapPos.z = bestSnapZ;
        }

        editorGridPos = snapPos;
    }

    // Texture selection (keys 1-4)
    for (int i = 0; i < 4; i++) {
        int key = GLFW_KEY_1 + i;
        if (glfwGetKey(window, key) == GLFW_PRESS && !inp.texKeyPressed[i]) {
            inp.texKeyPressed[i] = true;
            editorSelectedTexture = i;
        } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
            inp.texKeyPressed[i] = false;
        }
    }

    // Rotation control (T key - cycles through 9 steps for 90°: 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, etc.)
    // Shift+T = rotate backward, Ctrl+T = pitch down, Shift+Ctrl+T = pitch up
    bool shiftPressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !inp.texKeyPressed[9]) {
        inp.texKeyPressed[9] = true;

        if (ctrlPressed && shiftPressed) {
            // Shift+Ctrl+T = pitch up
            editorBlockPitch += 10.0f;
            if (editorBlockPitch > 90.0f) editorBlockPitch = 90.0f;
        } else if (ctrlPressed) {
            // Ctrl+T = pitch down
            editorBlockPitch -= 10.0f;
            if (editorBlockPitch < -90.0f) editorBlockPitch = -90.0f;
        } else if (shiftPressed) {
            // Shift+T = rotate backward
            editorBlockRotation -= 10.0f;
            if (editorBlockRotation < 0.0f) editorBlockRotation = 350.0f;
        } else {
            // T = rotate forward
            editorBlockRotation += 10.0f;
            if (editorBlockRotation >= 360.0f) editorBlockRotation = 0.0f;
        }
    } else if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE) {
        inp.texKeyPressed[9] = false;
    }

    // Grid toggle
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !inp.gPressed) {
        inp.gPressed = true;
        editorShowGrid = !editorShowGrid;
    } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
        inp.gPressed = false;
    }

    // Mode toggle (M key)
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !inp.mPressed) {
        inp.mPressed = true;
        editorMode = (editorMode == MODE_BLOCK) ? MODE_SHAPE : MODE_BLOCK;
        // Reset shape creation when switching modes
        if (editorMode == MODE_BLOCK) {
            shapeCreation.selectedPoints = 0;
            shapeCreation.nodes.clear();
        }
    } else if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) {
        inp.mPressed = false;
    }

    // Reset shape creation (R key in SHAPE mode) - simplified
    if (editorMode == MODE_SHAPE) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !inp.shapeResetPressed) {
            inp.shapeResetPressed = true;
        } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
            inp.shapeResetPressed = false;
        }
    }

    // Noclip toggle
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !inp.nPressed) {
        inp.nPressed = true;
        editorNoclip = !editorNoclip;
    } else if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) {
        inp.nPressed = false;
    }

    // Save map (F9)
    if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS && !inp.f9Pressed) {
        inp.f9Pressed = true;
        saveMap("custom.map");
    } else if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_RELEASE) {
        inp.f9Pressed = false;
    }

    // Spawn mode selection (F1-F4)
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS && !inp.f1Pressed) {
        inp.f1Pressed = true;
        editorSpawnMode = SPAWN_DEATHMATCH;
    } else if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) {
        inp.f1Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS && !inp.f2Pressed) {
        inp.f2Pressed = true;
        editorSpawnMode = SPAWN_TEAM_A;
    } else if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_RELEASE) {
        inp.f2Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS && !inp.f3Pressed) {
        inp.f3Pressed = true;
        editorSpawnMode = SPAWN_TEAM_B;
    } else if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_RELEASE) {
        inp.f3Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS && !inp.f4Pressed) {
        inp.f4Pressed = true;
        editorSpawnMode = SPAWN_NONE;
    } else if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_RELEASE) {
        inp.f4Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS && !inp.f5Pressed) {
        inp.f5Pressed = true;
        editorSpawnMode = SPAWN_BOMB_SITE_A;
    } else if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_RELEASE) {
        inp.f5Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F6) == GLFW_PRESS && !inp.f6Pressed) {
        inp.f6Pressed = true;
        editorSpawnMode = SPAWN_BOMB_SITE_B;
    } else if (glfwGetKey(window, GLFW_KEY_F6) == GLFW_RELEASE) {
        inp.f6Pressed = false;
    }

    // Block placement (Left mouse button) - hold to keep placing, throttled
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double now = glfwGetTime();
        if (now - inp.lastPlaceTime >= placeInterval) {
            inp.lastPlaceTime = now;
            if (currentMap) {
                // Check if we need to expand map dimensions
                glm::vec3 halfSize = editorBlockSize * 0.5f;
                glm::vec3 blockMin = editorGridPos - halfSize;
                glm::vec3 blockMax = editorGridPos + halfSize;
                glm::vec3 dims = currentMap->getDimensions();

                bool needsExpansion = false;
                float newWidth  = dims.x;
                float newHeight = dims.y;
                float newDepth  = dims.z;

                if (blockMin.x < -dims.x * 0.5f || blockMax.x > dims.x * 0.5f) {
                    newWidth = glm::max(dims.x, glm::abs(blockMax.x) * 2.0f + 2.0f);
                    newWidth = glm::min(newWidth, 1000.0f); // Clamp to reasonable max
                    needsExpansion = true;
                }
                if (blockMin.y < -dims.y * 0.5f || blockMax.y > dims.y * 0.5f) {
                    newHeight = glm::max(dims.y, glm::abs(blockMax.y) * 2.0f + 2.0f);
                    newHeight = glm::min(newHeight, 1000.0f); // Clamp to reasonable max
                    needsExpansion = true;
                }
                if (blockMin.z < -dims.z * 0.5f || blockMax.z > dims.z * 0.5f) {
                    newDepth = glm::max(dims.z, glm::abs(blockMax.z) * 2.0f + 2.0f);
                    newDepth = glm::min(newDepth, 1000.0f); // Clamp to reasonable max
                    needsExpansion = true;
                }

                if (needsExpansion) {
                    currentMap->setDimensions(glm::vec3(newWidth, newHeight, newDepth));
                }

                // Handle spawn point placement or block placement
                if (editorSpawnMode == SPAWN_DEATHMATCH) {
                    currentMap->addSpawnPoint(SpawnPoint(editorGridPos, camera.Yaw, camera.Pitch));
                } else if (editorSpawnMode == SPAWN_TEAM_A) {
                    currentMap->addTeamSpawn(TeamSpawn(editorGridPos, 0, camera.Yaw, camera.Pitch));
                } else if (editorSpawnMode == SPAWN_TEAM_B) {
                    currentMap->addTeamSpawn(TeamSpawn(editorGridPos, 1, camera.Yaw, camera.Pitch));
                } else if (editorSpawnMode == SPAWN_BOMB_SITE_A) {
                    currentMap->addBombSite(BombSiteData(editorGridPos, 'A'));
                } else if (editorSpawnMode == SPAWN_BOMB_SITE_B) {
                    currentMap->addBombSite(BombSiteData(editorGridPos, 'B'));
                } else {
                    // Normal block placement (only in BLOCK mode)
                    if (editorMode == MODE_BLOCK) {
                        // Remove any existing block at the same position with same size and orientation
                        currentMap->deleteBlockExact(editorGridPos, editorBlockSize, editorBlockRotation, editorBlockPitch, 0.05f);

                        MapBlock block;
                        block.position  = editorGridPos;
                        block.scale     = editorBlockSize;
                        block.color     = glm::vec3(1.0f, 1.0f, 1.0f);
                        block.textureId = editorSelectedTexture;
                        block.solid     = true;
                        block.rotation  = editorBlockRotation;
                        block.pitch     = editorBlockPitch;
                        currentMap->addBlock(block);
                    } else {
                        // SHAPE mode - simplified (no action)
                    }
                }
            }
        }
    }

    // Right mouse button:
    //   Plain RMB       = replace texture of aimed block with current texture
    //   ALT + RMB       = delete aimed block entirely or spawn point
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !inp.rmbPressed) {
        inp.rmbPressed = true;
        if (currentMap) {
            glm::vec3 rayOrigin = camera.Position;
            glm::vec3 rayDir    = camera.Front;
            float maxDist = 100.0f;
            float step    = 0.1f;
            bool altHeld  = glfwGetKey(window, GLFW_KEY_LEFT_ALT)  == GLFW_PRESS ||
                            glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

            bool hitSomething = false;

            // Check for blocks first
            for (float t = 0; t < maxDist; t += step) {
                glm::vec3 point = rayOrigin + rayDir * t;
                if (currentMap->findBlockAt(point, 0.15f) >= 0) {
                    if (altHeld) {
                        currentMap->deleteBlockAt(point, 0.15f);
                    } else {
                        currentMap->replaceTextureAt(point, editorSelectedTexture);
                    }
                    hitSomething = true;
                    break;
                }
            }

            // If ALT held and no block hit, check for spawn points
            if (altHeld && !hitSomething) {
                // Check deathmatch spawn points
                const auto& spawns = currentMap->getSpawnPoints();
                for (size_t i = 0; i < spawns.size(); i++) {
                    float dist = glm::length(spawns[i].position - rayOrigin);
                    if (dist < 2.0f) {
                        currentMap->removeSpawnPoint(i);
                        hitSomething = true;
                        break;
                    }
                }

                // Check team spawn points
                if (!hitSomething) {
                    const auto& teamSpawns = currentMap->getTeamSpawns();
                    for (size_t i = 0; i < teamSpawns.size(); i++) {
                        float dist = glm::length(teamSpawns[i].position - rayOrigin);
                        if (dist < 2.0f) {
                            currentMap->removeTeamSpawn(i);
                            break;
                        }
                    }
                }
            }
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE) {
        inp.rmbPressed = false;
    }

    // E key - increase block size on selected axis (or all)
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS && !inp.ePressed) {
        inp.ePressed = true;
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
            editorBlockSize.x = glm::min(editorBlockSize.x + 0.5f, 20.0f);
        } else if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
            editorBlockSize.y = glm::min(editorBlockSize.y + 0.5f, 20.0f);
        } else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            editorBlockSize.z = glm::min(editorBlockSize.z + 0.5f, 20.0f);
        } else {
            editorBlockSize += glm::vec3(0.5f);
            editorBlockSize  = glm::min(editorBlockSize, glm::vec3(20.0f));
        }
    } else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_RELEASE) {
        inp.ePressed = false;
    }

    // Q key - decrease block size on selected axis (or all)
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS && !inp.qPressed) {
        inp.qPressed = true;
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
            editorBlockSize.x = glm::max(editorBlockSize.x - 0.5f, 0.5f);
        } else if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
            editorBlockSize.y = glm::max(editorBlockSize.y - 0.5f, 0.5f);
        } else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            editorBlockSize.z = glm::max(editorBlockSize.z - 0.5f, 0.5f);
        } else {
            editorBlockSize -= glm::vec3(0.5f);
            editorBlockSize  = glm::max(editorBlockSize, glm::vec3(0.5f));
        }
    } else if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE) {
        inp.qPressed = false;
    }

    // Grid size adjustment with [ and ] keys
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS && !inp.lbracketPressed) {
        inp.lbracketPressed = true;
        editorGridSize = glm::max(editorGridSize - 0.5f, 0.5f);
    } else if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_RELEASE) {
        inp.lbracketPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS && !inp.rbracketPressed) {
        inp.rbracketPressed = true;
        editorGridSize = glm::min(editorGridSize + 0.5f, 5.0f);
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_RELEASE) {
        inp.rbracketPressed = false;
    }

    // ESC to exit editor back to menu
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
        inp.escapePressed = true;
        inp.resetNavKeys();
        menuJustEntered = true;
        gameState = MENU;
        menuSelection = isConnectedToServer() ? 1 : 2;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        inp.escapePressed = false;
    }
}

void Game::saveMap(const std::string& filename) {
    if (!currentMap) {
        std::cerr << "No map to save!" << std::endl;
        return;
    }

    std::string filepath = "assets/maps/" + filename;
    std::ofstream file(filepath);

    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filepath << std::endl;
        return;
    }

    glm::vec3 dims = currentMap->getDimensions();

    // Write header
    file << "# Custom map created in editor\n";
    file << "name Custom\n";
    file << "dimensions " << dims.x << " " << dims.y << " " << dims.z << "\n\n";

    // Write blocks
    const auto& blocks = currentMap->getBlocks();
    for (const auto& block : blocks) {
        file << "block "
             << block.position.x << " "
             << block.position.y << " "
             << block.position.z << " "
             << block.scale.x    << " "
             << block.scale.y    << " "
             << block.scale.z    << " "
             << block.textureId  << " "
             << block.blockType  << " "
             << (block.solid ? "1" : "0") << " "
             << block.color.r    << " "
             << block.color.g    << " "
             << block.color.b    << " "
             << block.rotation   << " "
             << block.pitch      << "\n";
    }

    // Write deathmatch spawn points
    const auto& spawns = currentMap->getSpawnPoints();
    if (!spawns.empty()) {
        file << "\n# Deathmatch spawn points\n";
        for (const auto& spawn : spawns) {
            file << "spawn "
                 << spawn.position.x << " "
                 << spawn.position.y << " "
                 << spawn.position.z << " "
                 << spawn.yaw        << " "
                 << spawn.pitch      << "\n";
        }
    }

    // Write team spawn points
    const auto& teamSpawns = currentMap->getTeamSpawns();
    if (!teamSpawns.empty()) {
        file << "\n# Team spawn points\n";
        for (const auto& spawn : teamSpawns) {
            file << "team_spawn "
                 << spawn.position.x << " "
                 << spawn.position.y << " "
                 << spawn.position.z << " "
                 << spawn.teamId     << " "
                 << spawn.yaw        << " "
                 << spawn.pitch      << "\n";
        }
    }

    // Write bomb sites
    const auto& bombSites = currentMap->getBombSites();
    if (!bombSites.empty()) {
        file << "\n# Bomb sites (for Bomb Defusal mode)\n";
        for (const auto& site : bombSites) {
            file << "bomb_site "
                 << site.position.x << " "
                 << site.position.y << " "
                 << site.position.z << " "
                 << site.label      << "\n";
        }
    }

    file.close();
}