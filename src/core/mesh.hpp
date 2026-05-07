/**
 * @file mesh.hpp
 * @brief Mesh class for storing 3D geometry data
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef MESH_HPP
#define MESH_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

/**
 * @brief Structure to hold vertex data
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

/**
 * @brief Structure to hold texture information
 */
struct Texture {
    unsigned int id;
    std::string type;
    std::string path;
};

/**
 * @brief Mesh class for storing and rendering 3D geometry
 */
class Mesh {
public:
    // Mesh data
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    // Constructor
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures);

    // Render the mesh
    void Draw(unsigned int shaderProgram);

    // Getters for OpenGL buffers
    unsigned int getVAO() const { return VAO; }
    unsigned int getVBO() const { return VBO; }
    unsigned int getEBO() const { return EBO; }

private:
    // Render data
    unsigned int VAO, VBO, EBO;

    // Initialize buffers
    void setupMesh();
};

#endif // MESH_HPP
