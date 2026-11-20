/**
 * @file shader.cpp
 * @brief OpenGL shader program wrapper with cached uniform locations
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "engine/shader.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

namespace {
std::string readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << path << std::endl;
        return {};
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
} // namespace

unsigned int Shader::compileStage(GLenum type, const char* path) {
    const std::string source = readFile(path);
    if (source.empty()) return 0;

    const char* code = source.c_str();
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &code, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR::SHADER::" << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT")
                  << "::COMPILATION_FAILED (" << path << ")\n" << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) : ID(0) {
    unsigned int vertex   = compileStage(GL_VERTEX_SHADER, vertexPath);
    unsigned int fragment = compileStage(GL_FRAGMENT_SHADER, fragmentPath);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return;
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);

    int success = 0;
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(ID, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        glDeleteProgram(ID);
        ID = 0;
    }

    glDetachShader(ID, vertex);
    glDetachShader(ID, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

void Shader::use() const {
    glUseProgram(ID);
}

GLint Shader::location(const char* name) {
    if (!ID) return -1;
    auto it = uniformCache.find(name);
    if (it != uniformCache.end()) return it->second;
    GLint loc = glGetUniformLocation(ID, name);
    uniformCache.emplace(name, loc);
    return loc;
}

void Shader::setMat4(const char* name, const float* value) {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, value);
}

void Shader::setMat4Array(const char* name, const float* value, int count) {
    if (count > 0) glUniformMatrix4fv(location(name), count, GL_FALSE, value);
}

void Shader::setInt(const char* name, int value) {
    glUniform1i(location(name), value);
}

void Shader::setFloat(const char* name, float value) {
    glUniform1f(location(name), value);
}

void Shader::setVec3(const char* name, const glm::vec3& v) {
    glUniform3fv(location(name), 1, glm::value_ptr(v));
}

void Shader::setVec4(const char* name, const glm::vec4& v) {
    glUniform4fv(location(name), 1, glm::value_ptr(v));
}

void Shader::setUint(const char* name, unsigned int value) {
    glUniform1ui(location(name), value);
}

Shader::~Shader() {
    if (ID) glDeleteProgram(ID);
}
