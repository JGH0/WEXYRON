/**
 * @file camera.hpp
 * @brief Camera class for 3D scene navigation and view matrix generation
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * @brief Camera class implementing FPS-style camera controls
 * 
 * Provides camera movement, mouse look, and zoom functionality.
 * Uses Euler angles (yaw and pitch) for camera orientation.
 */
class Camera {
public:
    /// Camera position in world space
    glm::vec3 Position;
    /// Camera front direction vector
    glm::vec3 Front;
    /// Camera up direction vector
    glm::vec3 Up;
    /// Camera right direction vector
    glm::vec3 Right;
    /// World up direction (usually Y-axis)
    glm::vec3 WorldUp;

    /// Euler angles for camera orientation
    float Yaw;
    float Pitch;
    /// Camera movement speed
    float MovementSpeed;
    /// Mouse sensitivity for look controls
    float MouseSensitivity;
    /// Camera zoom (FOV in degrees)
    float Zoom;

    /**
     * @brief Construct a new Camera object
     * 
     * @param position Initial camera position (default: (0, 0, 3))
     * @param up World up vector (default: (0, 1, 0))
     * @param yaw Initial yaw angle in degrees (default: -90)
     * @param pitch Initial pitch angle in degrees (default: 0)
     */
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = -90.0f, float pitch = 0.0f);

    /**
     * @brief Get the view matrix for the camera
     * 
     * @return glm::mat4 View matrix using glm::lookAt
     */
    glm::mat4 GetViewMatrix();
    
    /**
     * @brief Process keyboard input for camera movement
     * 
     * @param direction Movement direction: 0=forward, 1=backward, 2=left, 3=right
     * @param deltaTime Time delta for frame-independent movement
     */
    void ProcessKeyboard(int direction, float deltaTime);
    
    /**
     * @brief Process mouse movement for camera look
     * 
     * @param xoffset Mouse X offset
     * @param yoffset Mouse Y offset
     * @param constrainPitch Whether to constrain pitch to prevent flip (default: true)
     */
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    
    /**
     * @brief Process mouse scroll for zoom
     * 
     * @param yoffset Scroll offset
     */
    void ProcessMouseScroll(float yoffset);

    /**
     * @brief Update camera vectors based on current Euler angles
     */
    void updateCameraVectors();
};

#endif