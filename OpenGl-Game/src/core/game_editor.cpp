#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

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
    }

    // Render grid lines on ground - grid size now matches block size
    if (editorShowGrid) {
        // Use the largest block dimension as the grid cell size
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));
        for (int i = -20; i <= 20; i++) {
            // X lines
            glm::mat4 lineX = glm::mat4(1.0f);
            lineX = glm::translate(lineX, glm::vec3(i * gridCellSize, -0.05f, 0.0f));
            lineX = glm::scale(lineX, glm::vec3(0.02f, 0.02f, 40.0f * gridCellSize));
            renderer.drawCube(lineX, glm::vec3(0.3f, 0.3f, 0.3f));

            // Z lines
            glm::mat4 lineZ = glm::mat4(1.0f);
            lineZ = glm::translate(lineZ, glm::vec3(0.0f, -0.05f, i * gridCellSize));
            lineZ = glm::scale(lineZ, glm::vec3(40.0f * gridCellSize, 0.02f, 0.02f));
            renderer.drawCube(lineZ, glm::vec3(0.3f, 0.3f, 0.3f));
        }
    }

    // Render ghost block at grid position - wireframe style (edges only)
    {
        glm::vec3 halfSize = editorBlockSize * 0.5f;
        glm::vec3 center = editorGridPos;
        float edgeThickness = 0.02f;
        glm::vec3 edgeColor = glm::vec3(0.0f, 1.0f, 0.0f); // Bright green edges

        float xEdgeLen = editorBlockSize.x - edgeThickness;
        float yEdgeLen = editorBlockSize.y - edgeThickness;
        float zEdgeLen = editorBlockSize.z - edgeThickness;

        // Vertical edges (4 corners) - Y direction
        for (float x = -1; x <= 1; x += 2) {
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(x * halfSize.x, 0.0f, z * halfSize.z));
                edge = glm::scale(edge, glm::vec3(edgeThickness, yEdgeLen, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
        }
        // Horizontal edges at top and bottom
        for (float y = -1; y <= 1; y += 2) {
            // X-direction edges
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(0.0f, y * halfSize.y, z * halfSize.z));
                edge = glm::scale(edge, glm::vec3(xEdgeLen, edgeThickness, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
            // Z-direction edges
            for (float x = -1; x <= 1; x += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(x * halfSize.x, y * halfSize.y, 0.0f));
                edge = glm::scale(edge, glm::vec3(edgeThickness, edgeThickness, zEdgeLen));
                renderer.drawCube(edge, edgeColor);
            }
        }
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
    renderer.drawTextFit("1-8=tex E/Q=resize arrows=move UP/DOWN=dist", startX, startY + lineHeight * 3, maxHelpWidth, textScale);
    renderer.drawTextFit("F1 DM F2 A F3 B F4 Block F9 save ESC exit", startX, startY + lineHeight * 4,  maxHelpWidth, textScale);

    // Show current settings
    char buf[128];
    snprintf(buf, sizeof(buf), "POS: %.1f, %.1f, %.1f", editorGridPos.x, editorGridPos.y, editorGridPos.z);
    renderer.drawText(buf, startX, startY + lineHeight * 6, textScale);
    snprintf(buf, sizeof(buf), "SIZE: %.1f, %.1f, %.1f", editorBlockSize.x, editorBlockSize.y, editorBlockSize.z);
    renderer.drawText(buf, startX, startY + lineHeight * 7, textScale);
    snprintf(buf, sizeof(buf), "TEX: %d  DIST: %.1f", editorSelectedTexture, editorBlockOffset);
    renderer.drawText(buf, startX, startY + lineHeight * 8, textScale);
    snprintf(buf, sizeof(buf), "GRID: %s  NOCLIP: %s", editorShowGrid ? "ON" : "OFF", editorNoclip ? "ON" : "OFF");
    renderer.drawText(buf, startX, startY + lineHeight * 9, textScale);

    // Show spawn mode
    const char* spawnModeStr = "BLOCK MODE";
    switch (editorSpawnMode) {
        case SPAWN_DEATHMATCH: spawnModeStr = "DM SPAWN MODE";          break;
        case SPAWN_TEAM_A:     spawnModeStr = "TEAM A SPAWN (Red)";     break;
        case SPAWN_TEAM_B:     spawnModeStr = "TEAM B SPAWN (Blue)";    break;
        default: break;
    }
    snprintf(buf, sizeof(buf), "MODE: %s", spawnModeStr);
    renderer.drawText(buf, startX, startY + lineHeight * 10, textScale);

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
    renderer.drawColoredQuad(previewModel, textureColors[editorSelectedTexture]);

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
    }

    // UP/DOWN arrows move block placement forward/backward along camera direction
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upArrowPressed) {
        inp.upArrowPressed = true;
        editorBlockOffset += editorGridSize;
        if (editorBlockOffset > 20.0f) editorBlockOffset = 20.0f;
        std::cout << "Placement forward: " << editorBlockOffset << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        inp.upArrowPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downArrowPressed) {
        inp.downArrowPressed = true;
        editorBlockOffset -= editorGridSize;
        if (editorBlockOffset < 0.5f) editorBlockOffset = 0.5f;
        std::cout << "Placement backward: " << editorBlockOffset << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        inp.downArrowPressed = false;
    }

    // Calculate base placement position from where camera is looking
    glm::vec3 lookPos = camera.Position + camera.Front * editorBlockOffset;

    // LEFT/RIGHT arrows move placement sideways (perpendicular to camera)
    static int gridCellX = 0;
    static int gridCellZ = 0;

    if (editorShowGrid) {
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));

        // In grid mode, LEFT/RIGHT moves between grid cells
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !inp.leftArrowPressed) {
            inp.leftArrowPressed = true;
            gridCellX--;
            std::cout << "Grid cell: " << gridCellX << ", " << gridCellZ << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            inp.leftArrowPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !inp.rightArrowPressed) {
            inp.rightArrowPressed = true;
            gridCellX++;
            std::cout << "Grid cell: " << gridCellX << ", " << gridCellZ << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
            inp.rightArrowPressed = false;
        }

        // Calculate grid position based on where player is looking + cell offsets
        float baseGridX = floor(lookPos.x / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridY = floor(lookPos.y / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridZ = floor(lookPos.z / gridCellSize) * gridCellSize + gridCellSize * 0.5f;

        editorGridPos.x = baseGridX + gridCellX * gridCellSize;
        editorGridPos.y = baseGridY;
        editorGridPos.z = baseGridZ + gridCellZ * gridCellSize;
    } else {
        // Free mode: use smooth sideways offset
        float moveStep = 0.5f;

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !inp.leftArrowPressed) {
            inp.leftArrowPressed = true;
            inp.sidewaysOffset -= moveStep;
            std::cout << "Placement left: " << inp.sidewaysOffset << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            inp.leftArrowPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !inp.rightArrowPressed) {
            inp.rightArrowPressed = true;
            inp.sidewaysOffset += moveStep;
            std::cout << "Placement right: " << inp.sidewaysOffset << std::endl;
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

    // Texture selection (keys 1-8)
    for (int i = 0; i < 8; i++) {
        int key = GLFW_KEY_1 + i;
        if (glfwGetKey(window, key) == GLFW_PRESS && !inp.texKeyPressed[i]) {
            inp.texKeyPressed[i] = true;
            editorSelectedTexture = i;
            std::cout << "Selected texture: " << i << std::endl;
        } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
            inp.texKeyPressed[i] = false;
        }
    }

    // Grid toggle
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !inp.gPressed) {
        inp.gPressed = true;
        editorShowGrid = !editorShowGrid;
    } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
        inp.gPressed = false;
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
        std::cout << "Editor mode: Deathmatch Spawn Point" << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) {
        inp.f1Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS && !inp.f2Pressed) {
        inp.f2Pressed = true;
        editorSpawnMode = SPAWN_TEAM_A;
        std::cout << "Editor mode: Team A (Red) Spawn Point" << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_RELEASE) {
        inp.f2Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS && !inp.f3Pressed) {
        inp.f3Pressed = true;
        editorSpawnMode = SPAWN_TEAM_B;
        std::cout << "Editor mode: Team B (Blue) Spawn Point" << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_RELEASE) {
        inp.f3Pressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS && !inp.f4Pressed) {
        inp.f4Pressed = true;
        editorSpawnMode = SPAWN_NONE;
        std::cout << "Editor mode: Block Placement" << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_RELEASE) {
        inp.f4Pressed = false;
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
                    needsExpansion = true;
                }
                if (blockMin.y < -dims.y * 0.5f || blockMax.y > dims.y * 0.5f) {
                    newHeight = glm::max(dims.y, glm::abs(blockMax.y) * 2.0f + 2.0f);
                    needsExpansion = true;
                }
                if (blockMin.z < -dims.z * 0.5f || blockMax.z > dims.z * 0.5f) {
                    newDepth = glm::max(dims.z, glm::abs(blockMax.z) * 2.0f + 2.0f);
                    needsExpansion = true;
                }

                if (needsExpansion) {
                    currentMap->setDimensions(glm::vec3(newWidth, newHeight, newDepth));
                    std::cout << "Map expanded to dimensions: " << newWidth << "x" << newHeight << "x" << newDepth << std::endl;
                }

                // Handle spawn point placement or block placement
                if (editorSpawnMode == SPAWN_DEATHMATCH) {
                    currentMap->addSpawnPoint(SpawnPoint(editorGridPos, camera.Yaw, camera.Pitch));
                    std::cout << "Added DM spawn point at (" << editorGridPos.x << ", " << editorGridPos.y << ", " << editorGridPos.z
                              << ") yaw=" << camera.Yaw << " pitch=" << camera.Pitch << std::endl;
                } else if (editorSpawnMode == SPAWN_TEAM_A) {
                    currentMap->addTeamSpawn(TeamSpawn(editorGridPos, 0, camera.Yaw, camera.Pitch));
                    std::cout << "Added Team A (Red) spawn point at (" << editorGridPos.x << ", " << editorGridPos.y << ", " << editorGridPos.z
                              << ") yaw=" << camera.Yaw << " pitch=" << camera.Pitch << std::endl;
                } else if (editorSpawnMode == SPAWN_TEAM_B) {
                    currentMap->addTeamSpawn(TeamSpawn(editorGridPos, 1, camera.Yaw, camera.Pitch));
                    std::cout << "Added Team B (Blue) spawn point at (" << editorGridPos.x << ", " << editorGridPos.y << ", " << editorGridPos.z
                              << ") yaw=" << camera.Yaw << " pitch=" << camera.Pitch << std::endl;
                } else {
                    // Normal block placement
                    MapBlock block;
                    block.position  = editorGridPos;
                    block.scale     = editorBlockSize;
                    block.color     = glm::vec3(1.0f, 1.0f, 1.0f);
                    block.textureId = editorSelectedTexture;
                    block.solid     = true;
                    currentMap->addBlock(block);
                    std::cout << "Added block at " << editorGridPos.x << "," << editorGridPos.y << "," << editorGridPos.z
                              << " size " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
                }
            }
        }
    }

    // Right mouse button:
    //   Plain RMB       = replace texture of aimed block with current texture
    //   ALT + RMB       = delete aimed block entirely
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !inp.rmbPressed) {
        inp.rmbPressed = true;
        if (currentMap) {
            glm::vec3 rayOrigin = camera.Position;
            glm::vec3 rayDir    = camera.Front;
            float maxDist = 100.0f;
            float step    = 0.1f;
            bool altHeld  = glfwGetKey(window, GLFW_KEY_LEFT_ALT)  == GLFW_PRESS ||
                            glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

            for (float t = 0; t < maxDist; t += step) {
                glm::vec3 point = rayOrigin + rayDir * t;
                if (currentMap->findBlockAt(point, 0.15f) >= 0) {
                    if (altHeld) {
                        if (currentMap->deleteBlockAt(point, 0.15f)) {
                            std::cout << "Deleted block at distance " << t << std::endl;
                        }
                    } else {
                        if (currentMap->replaceTextureAt(point, editorSelectedTexture)) {
                            std::cout << "Replaced texture -> " << editorSelectedTexture
                                      << " at distance " << t << std::endl;
                        }
                    }
                    break;
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
        std::cout << "Block size: " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
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
        std::cout << "Block size: " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE) {
        inp.qPressed = false;
    }

    // Grid size adjustment with [ and ] keys
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS && !inp.lbracketPressed) {
        inp.lbracketPressed = true;
        editorGridSize = glm::max(editorGridSize - 0.5f, 0.5f);
        std::cout << "Grid size: " << editorGridSize << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_RELEASE) {
        inp.lbracketPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS && !inp.rbracketPressed) {
        inp.rbracketPressed = true;
        editorGridSize = glm::min(editorGridSize + 0.5f, 5.0f);
        std::cout << "Grid size: " << editorGridSize << std::endl;
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
             << block.color.b    << "\n";
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

    file.close();
    std::cout << "Map saved to " << filepath << " with " << blocks.size() << " blocks, "
              << spawns.size() << " DM spawns, " << teamSpawns.size() << " team spawns" << std::endl;
}