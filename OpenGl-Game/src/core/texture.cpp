/**
 * @file texture.cpp
 * @brief Implementation of Texture class for OpenGL texture management
 * @author OpenGL Game Developer
 * @date 2025
 */

// Define STB_IMAGE_IMPLEMENTATION in one source file
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "texture.hpp"
#include <iostream>

Texture::Texture(const char* path, GLenum textureUnit) 
    : ID(0), width(0), height(0), nrChannels(0), textureUnit(textureUnit) {
    
    if (!loadFromFile(path)) {
        std::cerr << "Failed to load texture: " << path << std::endl;
    }
}

Texture::~Texture() {
    if (ID != 0) {
        glDeleteTextures(1, &ID);
    }
}

bool Texture::loadFromFile(const char* path) {
    // Generate and bind texture
    glGenTextures(1, &ID);
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, ID);
    
    // Set default texture parameters
    setWrapParameters();
    setFilterParameters();
    
    // Load image data
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        // Determine format based on number of channels
        GLenum format = GL_RGB;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 4)
            format = GL_RGBA;
        
        // Upload texture data to GPU
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        std::cout << "Loaded texture: " << path << " (" << width << "x" << height << ", " << nrChannels << " channels)" << std::endl;
    } else {
        std::cerr << "Failed to load texture: " << path << std::endl;
        stbi_image_free(data);
        return false;
    }
    
    // Free image data
    stbi_image_free(data);
    
    return true;
}

void Texture::bind() const {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, ID);
}

void Texture::unbind() const {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::setWrapParameters(GLenum wrapS, GLenum wrapT) {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, ID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
}

void Texture::setFilterParameters(GLenum minFilter, GLenum magFilter) {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, ID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
}
