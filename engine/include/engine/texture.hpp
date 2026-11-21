/**
 * @file texture.hpp
 * @brief Decoded RGBA image loaded from a file (map material textures)
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef TEXTURE_HPP
#define TEXTURE_HPP

/**
 * @brief An image file decoded to RGBA8 in CPU memory.
 *
 * Supports PNG, JPG and the other formats stb_image reads. The renderer copies
 * these pixels into its texture array; no GPU object is created here, so a
 * Texture can be loaded on any thread (the map loader decodes on a worker).
 */
class Texture {
public:
    /// Image width in pixels (0 if loading failed)
    int width = 0;
    /// Image height in pixels (0 if loading failed)
    int height = 0;
    /// Channels in `pixels` (always 4 once loaded)
    int nrChannels = 0;
    /// Owned RGBA copy of the image (width * height * 4), or nullptr.
    unsigned char* pixels = nullptr;

    /**
     * @brief Load and decode an image file.
     *
     * Relative paths are also tried under the usual asset folders and next to
     * the executable (see texture.cpp).
     * @param path Path to the image file
     */
    explicit Texture(const char* path);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    /// @return true when the image was decoded
    bool valid() const { return pixels != nullptr; }

private:
    bool loadFromFile(const char* path);
};

#endif
