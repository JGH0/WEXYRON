/**
 * @file shader.hpp
 * @brief Shader class for OpenGL shader program management
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef SHADER_HPP
#define SHADER_HPP

#include <glad/glad.h>
#include <string>

/**
 * @brief Shader class for loading, compiling, and using OpenGL shader programs
 * 
 * Handles vertex and fragment shader loading, compilation, linking,
 * and provides uniform setting functionality.
 */
class Shader {
public:
    /// OpenGL shader program ID
    unsigned int ID;
    
    /**
     * @brief Construct a new Shader object
     * 
     * @param vertexPath Path to vertex shader source file
     * @param fragmentPath Path to fragment shader source file
     */
    Shader(const char* vertexPath, const char* fragmentPath);
    
    /**
     * @brief Use this shader program for rendering
     */
    void use();
    
    /**
     * @brief Set a 4x4 matrix uniform
     * 
     * @param name Uniform variable name
     * @param value Pointer to matrix data
     */
    void setMat4(const std::string &name, const float *value) const;
    
    /**
     * @brief Destructor - cleans up shader program
     */
    ~Shader();
};

#endif