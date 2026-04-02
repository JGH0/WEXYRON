/**
 * @file texture.hpp
 * @brief Texture class for loading and managing OpenGL textures
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include <glad/glad.h>
#include <string>

/**
 * @brief Texture class for loading and managing OpenGL textures
 * 
 * Supports loading PNG, JPG, and other common image formats.
 * Provides texture binding and management functionality.
 */
class Texture {
public:
    /// OpenGL texture ID
    unsigned int ID;
    /// Texture width
    int width;
    /// Texture height
    int height;
    /// Number of color channels
    int nrChannels;
    
    /**
     * @brief Construct a new Texture object
     * 
     * @param path Path to the texture file
     * @param textureUnit OpenGL texture unit (default: GL_TEXTURE0)
     */
    Texture(const char* path, GLenum textureUnit = GL_TEXTURE0);
    
    /**
     * @brief Destructor - cleans up texture resources
     */
    ~Texture();
    
    /**
     * @brief Bind this texture for use in rendering
     */
    void bind() const;
    
    /**
     * @brief Unbind the current texture
     */
    void unbind() const;
    
    /**
     * @brief Set texture wrapping parameters
     * 
     * @param wrapS Texture wrapping mode for S coordinate (default: GL_REPEAT)
     * @param wrapT Texture wrapping mode for T coordinate (default: GL_REPEAT)
     */
    void setWrapParameters(GLenum wrapS = GL_REPEAT, GLenum wrapT = GL_REPEAT);
    
    /**
     * @brief Set texture filtering parameters
     * 
     * @param minFilter Minification filter (default: GL_LINEAR)
     * @param magFilter Magnification filter (default: GL_LINEAR)
     */
    void setFilterParameters(GLenum minFilter = GL_LINEAR, GLenum magFilter = GL_LINEAR);
    
    /**
     * @brief Get the OpenGL texture unit this texture uses
     * @return GLenum The texture unit (GL_TEXTURE0, GL_TEXTURE1, etc.)
     */
    GLenum getUnit() const { return textureUnit; }

private:
    /// OpenGL texture unit
    GLenum textureUnit;
    
    /**
     * @brief Load texture from file
     * 
     * @param path Path to the texture file
     * @return true if loaded successfully, false otherwise
     */
    bool loadFromFile(const char* path);
};

#endif
