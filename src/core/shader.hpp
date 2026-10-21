/**
 * @file shader.hpp
 * @brief OpenGL shader program wrapper with cached uniform locations
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef SHADER_HPP
#define SHADER_HPP

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * @brief Thin RAII wrapper around an OpenGL shader program.
 *
 * Uniform locations are looked up once and cached by name. Looking them up per
 * draw (glGetUniformLocation + a std::string construction every call) used to
 * stall every frame; glowDraw now sets uniforms through a small UBO instead,
 * but these helpers remain for any ad-hoc uniform.
 */
class Shader {
public:
    /// OpenGL shader program ID (0 if compilation failed)
    unsigned int ID;

    /**
     * @brief Compile and link a shader program from source files
     * @param vertexPath Path to the vertex shader source
     * @param fragmentPath Path to the fragment shader source
     */
    Shader(const char* vertexPath, const char* fragmentPath);

    /// Bind this program for rendering
    void use() const;

    /// @return true if the program linked successfully
    bool valid() const { return ID != 0; }

    /// Cached uniform location lookup (-1 when the uniform is not used)
    GLint location(const char* name);

    void setMat4(const char* name, const float* value);
    void setInt(const char* name, int value);
    void setFloat(const char* name, float value);
    void setVec3(const char* name, const glm::vec3& v);
    void setUint(const char* name, unsigned int value);

    ~Shader();

private:
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    std::unordered_map<std::string, GLint> uniformCache;

    static unsigned int compileStage(GLenum type, const char* path);
};

#endif // SHADER_HPP
