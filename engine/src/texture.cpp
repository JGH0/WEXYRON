/**
 * @file texture.cpp
 * @brief Implementation of Texture class for OpenGL texture management
 * @author OpenGL Game Developer
 * @date 2025
 */

// Define STB_IMAGE_IMPLEMENTATION in one source file
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "engine/texture.hpp"
#include <filesystem>
#include <iostream>
#include <iterator>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <unistd.h>
#endif

namespace {

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    wchar_t buffer[32768];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer,
                                              static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        return std::filesystem::path(buffer, buffer + length).parent_path();
    }
#elif defined(__linux__)
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    std::error_code cwdEc;
    return std::filesystem::current_path(cwdEc);
}

/// Resolve a texture path against the places it may live. The game normally
/// runs with the project root (or build/bin) as its working directory, but a
/// Windows double-click, a launcher or a shortcut can leave the process in a
/// different directory. Trying exe-relative folders as well keeps textures
/// loading in those cases instead of silently falling back to flat colours.
std::vector<std::filesystem::path> textureCandidates(const char* path) {
    std::vector<std::filesystem::path> out;
    auto add = [&out](const std::filesystem::path& p) {
        if (p.empty()) return;
        for (const auto& existing : out) {
            if (existing == p) return;
        }
        out.push_back(p);
    };

    const std::filesystem::path given(path);
    add(given);
    if (given.is_absolute()) return out;

    const std::filesystem::path file = given.filename();
    const std::filesystem::path exeDir = executableDirectory();
    const std::filesystem::path roots[] = {
        std::filesystem::path("."),
        std::filesystem::path(".."),
        std::filesystem::path("../.."),
        exeDir,
        exeDir / "..",
        exeDir / "../..",
    };

    // "assets/textures/<name>" is the canonical location. Also accept a bare
    // file name, a path that already includes assets/, and the same relative
    // path under every plausible parent directory.
    for (const auto& root : roots) {
        add(root / given);
        add(root / "assets" / "textures" / file);
        add(root / "assets" / file);
        add(root / "textures" / file);
    }
    return out;
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

} // namespace

Texture::Texture(const char* path, GLenum textureUnit) 
    : ID(0), width(0), height(0), nrChannels(0), textureUnit(textureUnit) {
    
    if (!loadFromFile(path)) {
        std::cerr << "Failed to load texture: " << path << std::endl;
    }
}

Texture::~Texture() {
    if (pixels) stbi_image_free(pixels);
    if (ID != 0) {
        glDeleteTextures(1, &ID);
    }
}

bool Texture::loadFromFile(const char* path) {
    if (!path || !path[0]) return false;

    std::filesystem::path resolved;
    for (const auto& candidate : textureCandidates(path)) {
        if (fileExists(candidate)) {
            resolved = candidate;
            break;
        }
    }
    if (resolved.empty()) {
        std::cerr << "Texture file not found: " << path << std::endl;
        return false;
    }

    // Decode once, always as RGBA. The renderer uploads this same buffer into
    // its texture array, so there is no second decode that could disagree with
    // the GL texture (and no path-sensitive difference between platforms).
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(resolved.string().c_str(), &w, &h, &channels, 4);
    if (!data || w <= 0 || h <= 0) {
        std::cerr << "Failed to decode texture: " << resolved.string() << std::endl;
        stbi_image_free(data);
        return false;
    }

    width = w;
    height = h;
    nrChannels = 4;

    glGenTextures(1, &ID);
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, ID);

    // Set default texture parameters
    setWrapParameters();
    setFilterParameters();

    // Upload texture data to GPU. No glGenerateMipmap on purpose: the 64x64
    // source textures are ever only read back as pixels and re-uploaded into
    // the renderer's texture array (NEAREST, no mipmaps), so mipmaps are never
    // sampled. Calling glGenerateMipmap here crashes the Mesa D3D12 software
    // driver on GPU-less Windows VMs (process exits with 0x80070057 while
    // loading the second texture) and is wasted work on every driver.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Keep the RGBA copy of the source pixels. The renderer copies textures
    // into a single texture array, and CPUs cannot read a GPU texture back
    // cheaply (the old FBO readback also crashed inside the Mesa driver).
    pixels = data;

    std::cout << "Loaded texture: " << resolved.string() << " (" << width << "x"
              << height << ", " << nrChannels << " channels)" << std::endl;
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
