/**
 * @file model.hpp
 * @brief Model class for loading and rendering 3D models from files
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef MODEL_HPP
#define MODEL_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include "engine/mesh.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

class Model {
public:
    // Model data
    std::vector<MeshTexture> textures_loaded;
    std::vector<Mesh> meshes;
    std::string directory;
    bool gammaCorrection;

    // Constructor
    Model(bool gamma = false) : gammaCorrection(gamma) {}

    // Load model from file
    void loadModel(std::string const& path);

    // Draw the model
    void Draw(unsigned int shaderProgram);

    // Getters
    const std::vector<Mesh>& getMeshes() const { return meshes; }
    const std::string& getDirectory() const { return directory; }

private:
    // Process a node in the assimp scene
    void processNode(aiNode* node, const aiScene* scene);

    // Process a mesh in the assimp scene
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);

    // Load a texture from material
    std::vector<MeshTexture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);

    // Load a texture from file
    unsigned int textureFromFile(const char* path, const std::string& directory);
};

#endif // MODEL_HPP
