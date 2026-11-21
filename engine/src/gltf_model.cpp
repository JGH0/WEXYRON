/**
 * @file gltf_model.cpp
 * @brief Self-contained glTF 2.0 loader, skinning and animation runtime.
 */

#include "engine/gltf_model.hpp"
#include "engine/shader.hpp"

#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "gltf_json.hpp"

namespace {

namespace fs = std::filesystem;

constexpr GLuint kMaxBones = 32;

// ---------------------------------------------------------------------------
// Bytes, base64 and data URIs
// ---------------------------------------------------------------------------

bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) file.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<unsigned char> base64Decode(const std::string& text) {
    std::vector<unsigned char> out;
    out.reserve(text.size() * 3 / 4);
    int buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int value = base64Value(c);
        if (value < 0) continue;  // whitespace / newlines
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xff));
        }
    }
    return out;
}

bool decodeDataUri(const std::string& uri, std::vector<unsigned char>& out) {
    if (uri.rfind("data:", 0) != 0) return false;
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos) return false;
    const std::string meta = uri.substr(5, comma - 5);
    if (meta.find(";base64") == std::string::npos) {
        // Percent-encoded text is legal but never used for glTF binary data.
        out.assign(uri.begin() + static_cast<std::ptrdiff_t>(comma + 1), uri.end());
        return true;
    }
    out = base64Decode(uri.substr(comma + 1));
    return true;
}

std::string lowerCopy(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

// ---------------------------------------------------------------------------
// glTF document: JSON + resolved buffers
// ---------------------------------------------------------------------------

struct Doc {
    gltfjson::Value root;
    std::vector<std::vector<unsigned char>> buffers;
    std::string baseDir;
};

bool loadBuffers(Doc& doc, const std::vector<unsigned char>& glbBin, std::string& error) {
    const gltfjson::Value& buffers = doc.root["buffers"];
    doc.buffers.resize(buffers.size());
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        const gltfjson::Value& buffer = buffers.at(i);
        const std::string uri = buffer["uri"].asString();
        if (uri.empty()) {
            if (i == 0 && !glbBin.empty()) {
                doc.buffers[i] = glbBin;
            } else {
                error = "buffer has no uri and no GLB binary chunk";
                return false;
            }
            continue;
        }
        if (uri.rfind("data:", 0) == 0) {
            if (!decodeDataUri(uri, doc.buffers[i])) {
                error = "failed to decode data-URI buffer";
                return false;
            }
        } else {
            const fs::path bufferPath = fs::path(doc.baseDir) / uri;
            if (!readFileBytes(bufferPath.string(), doc.buffers[i])) {
                error = "cannot read buffer '" + bufferPath.string() + "'";
                return false;
            }
        }
        const double declared = buffer["byteLength"].asDouble(-1.0);
        if (declared >= 0.0 && static_cast<std::size_t>(declared) > doc.buffers[i].size()) {
            error = "buffer is shorter than its declared byteLength";
            return false;
        }
    }
    return true;
}

bool parseGltfFile(const std::string& path, Doc& doc, std::string& error) {
    std::vector<unsigned char> file;
    if (!readFileBytes(path, file) || file.empty()) {
        error = "cannot read '" + path + "'";
        return false;
    }

    doc.baseDir = fs::path(path).parent_path().string();
    std::string jsonText;
    std::vector<unsigned char> glbBin;

    const bool isGlb = file.size() >= 12 &&
                       file[0] == 'g' && file[1] == 'l' && file[2] == 'T' && file[3] == 'F';
    if (isGlb) {
        auto readU32 = [&](std::size_t offset) -> uint32_t {
            uint32_t value = 0;
            std::memcpy(&value, file.data() + offset, 4);
            return value;
        };
        const uint32_t version = readU32(4);
        if (version != 2) {
            error = "only glTF 2.0 is supported";
            return false;
        }
        const uint32_t totalLength = readU32(8);
        std::size_t cursor = 12;
        while (cursor + 8 <= file.size() && cursor < totalLength) {
            const uint32_t chunkLength = readU32(cursor);
            const uint32_t chunkType = readU32(cursor + 4);
            cursor += 8;
            if (cursor + chunkLength > file.size()) {
                error = "truncated GLB chunk";
                return false;
            }
            if (chunkType == 0x4E4F534A) {  // 'JSON'
                jsonText.assign(reinterpret_cast<const char*>(file.data() + cursor), chunkLength);
            } else if (chunkType == 0x004E4942) {  // 'BIN\0'
                glbBin.assign(file.begin() + static_cast<std::ptrdiff_t>(cursor),
                              file.begin() + static_cast<std::ptrdiff_t>(cursor + chunkLength));
            }
            cursor += chunkLength;
        }
        if (jsonText.empty()) {
            error = "GLB has no JSON chunk";
            return false;
        }
    } else {
        jsonText.assign(file.begin(), file.end());
    }

    if (jsonText.size() >= 3 && static_cast<unsigned char>(jsonText[0]) == 0xEF &&
        static_cast<unsigned char>(jsonText[1]) == 0xBB &&
        static_cast<unsigned char>(jsonText[2]) == 0xBF) {
        jsonText.erase(0, 3);
    }
    // GLB JSON chunks are padded with spaces; tolerate NUL padding too.
    while (!jsonText.empty() &&
           (jsonText.back() == '\0' || jsonText.back() == ' ' ||
            jsonText.back() == '\n' || jsonText.back() == '\r' ||
            jsonText.back() == '\t')) {
        jsonText.pop_back();
    }
    if (!gltfjson::Value::parse(jsonText, doc.root, error)) return false;
    if (!doc.root.isObject()) {
        error = "glTF root is not a JSON object";
        return false;
    }
    return loadBuffers(doc, glbBin, error);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int componentSize(int componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1;  // BYTE, UNSIGNED_BYTE
        case 5122: case 5123: return 2;  // SHORT, UNSIGNED_SHORT
        case 5125: case 5126: return 4;  // UNSIGNED_INT, FLOAT
        default: return 0;
    }
}

int typeComponents(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

double readComponent(const unsigned char* p, int componentType, bool normalized) {
    switch (componentType) {
        case 5120: {  // BYTE
            int8_t v = 0;
            std::memcpy(&v, p, 1);
            double d = static_cast<double>(v);
            return normalized ? std::max(d / 127.0, -1.0) : d;
        }
        case 5121: {  // UNSIGNED_BYTE
            uint8_t v = 0;
            std::memcpy(&v, p, 1);
            return normalized ? static_cast<double>(v) / 255.0 : static_cast<double>(v);
        }
        case 5122: {  // SHORT
            int16_t v = 0;
            std::memcpy(&v, p, 2);
            double d = static_cast<double>(v);
            return normalized ? std::max(d / 32767.0, -1.0) : d;
        }
        case 5123: {  // UNSIGNED_SHORT
            uint16_t v = 0;
            std::memcpy(&v, p, 2);
            return normalized ? static_cast<double>(v) / 65535.0 : static_cast<double>(v);
        }
        case 5125: {  // UNSIGNED_INT
            uint32_t v = 0;
            std::memcpy(&v, p, 4);
            return normalized ? static_cast<double>(v) / 4294967295.0 : static_cast<double>(v);
        }
        case 5126: {  // FLOAT
            float v = 0.0f;
            std::memcpy(&v, p, 4);
            return static_cast<double>(v);
        }
        default: return 0.0;
    }
}

uint32_t readComponentUint(const unsigned char* p, int componentType) {
    switch (componentType) {
        case 5120: { int8_t v = 0; std::memcpy(&v, p, 1); return static_cast<uint32_t>(static_cast<int32_t>(v)); }
        case 5121: { uint8_t v = 0; std::memcpy(&v, p, 1); return v; }
        case 5122: { int16_t v = 0; std::memcpy(&v, p, 2); return static_cast<uint32_t>(static_cast<int32_t>(v)); }
        case 5123: { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
        case 5125: { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
        case 5126: { float v = 0.0f; std::memcpy(&v, p, 4); return static_cast<uint32_t>(v); }
        default: return 0;
    }
}

struct BufferView {
    const unsigned char* data = nullptr;
    std::size_t length = 0;
    std::size_t stride = 0;
};

bool resolveBufferView(const Doc& doc, int index, BufferView& view, std::string& error) {
    const gltfjson::Value& bufferViews = doc.root["bufferViews"];
    if (index < 0 || static_cast<std::size_t>(index) >= bufferViews.size()) {
        error = "invalid bufferView index";
        return false;
    }
    const gltfjson::Value& bv = bufferViews.at(static_cast<std::size_t>(index));
    const int bufferIndex = bv["buffer"].asInt(-1);
    if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= doc.buffers.size()) {
        error = "bufferView references an invalid buffer";
        return false;
    }
    const std::vector<unsigned char>& buffer = doc.buffers[static_cast<std::size_t>(bufferIndex)];
    const std::size_t offset = static_cast<std::size_t>(std::max(0, bv["byteOffset"].asInt(0)));
    const std::size_t length = static_cast<std::size_t>(std::max(0, bv["byteLength"].asInt(0)));
    if (offset > buffer.size() || length > buffer.size() - offset) {
        error = "bufferView is outside its buffer";
        return false;
    }
    view.data = buffer.data() + offset;
    view.length = length;
    view.stride = static_cast<std::size_t>(std::max(0, bv["byteStride"].asInt(0)));
    return true;
}

struct Accessor {
    const unsigned char* base = nullptr;
    const unsigned char* bufferEnd = nullptr;
    std::size_t stride = 0;
    std::size_t count = 0;
    int components = 0;
    int componentType = 0;
    bool normalized = false;

    bool sparse = false;
    const unsigned char* sparseIndices = nullptr;
    std::size_t sparseIndexStride = 0;
    int sparseIndexType = 0;
    const unsigned char* sparseValues = nullptr;
    std::size_t sparseValueStride = 0;
    std::size_t sparseCount = 0;
};

bool resolveAccessor(const Doc& doc, int index, Accessor& out, std::string& error) {
    const gltfjson::Value& accessors = doc.root["accessors"];
    if (index < 0 || static_cast<std::size_t>(index) >= accessors.size()) {
        error = "invalid accessor index";
        return false;
    }
    const gltfjson::Value& accessor = accessors.at(static_cast<std::size_t>(index));

    out = Accessor{};
    out.count = static_cast<std::size_t>(std::max(0, accessor["count"].asInt(0)));
    out.componentType = accessor["componentType"].asInt(0);
    out.components = typeComponents(accessor["type"].asString());
    out.normalized = accessor["normalized"].asBool(false);
    const int elementSize = componentSize(out.componentType) * out.components;
    if (elementSize <= 0) {
        error = "unsupported accessor component type";
        return false;
    }

    const int bufferViewIndex = accessor["bufferView"].asInt(-1);
    if (bufferViewIndex >= 0) {
        BufferView view;
        if (!resolveBufferView(doc, bufferViewIndex, view, error)) return false;
        const std::size_t accessorOffset =
            static_cast<std::size_t>(std::max(0, accessor["byteOffset"].asInt(0)));
        if (accessorOffset > view.length) {
            error = "accessor byteOffset is outside its bufferView";
            return false;
        }
        out.base = view.data + accessorOffset;
        const std::size_t available = view.length - accessorOffset;
        out.stride = view.stride > 0 ? view.stride : static_cast<std::size_t>(elementSize);
        if (out.count > 0 && (out.count - 1) * out.stride + static_cast<std::size_t>(elementSize) > available) {
            error = "accessor reads past the end of its bufferView";
            return false;
        }
    }

    const gltfjson::Value& sparse = accessor["sparse"];
    if (sparse.size() > 0) {
        out.sparse = true;
        out.sparseCount = static_cast<std::size_t>(std::max(0, sparse["count"].asInt(0)));
        const gltfjson::Value& indices = sparse["indices"];
        const gltfjson::Value& values = sparse["values"];
        BufferView indexView;
        BufferView valueView;
        if (!resolveBufferView(doc, indices["bufferView"].asInt(-1), indexView, error)) return false;
        if (!resolveBufferView(doc, values["bufferView"].asInt(-1), valueView, error)) return false;
        out.sparseIndices = indexView.data +
                            static_cast<std::size_t>(std::max(0, indices["byteOffset"].asInt(0)));
        out.sparseIndexType = indices["componentType"].asInt(0);
        out.sparseIndexStride = static_cast<std::size_t>(componentSize(out.sparseIndexType));
        out.sparseValues = valueView.data +
                           static_cast<std::size_t>(std::max(0, values["byteOffset"].asInt(0)));
        out.sparseValueStride = static_cast<std::size_t>(elementSize);
    }
    return true;
}

bool readAccessorFloats(const Doc& doc, int accessorIndex,
                        std::vector<float>& out, int& components, std::size_t& count,
                        std::string& error) {
    Accessor accessor;
    if (!resolveAccessor(doc, accessorIndex, accessor, error)) return false;
    components = accessor.components;
    count = accessor.count;
    out.assign(count * static_cast<std::size_t>(components), 0.0f);

    const int size = componentSize(accessor.componentType);
    if (accessor.base) {
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* element = accessor.base + i * accessor.stride;
            for (int c = 0; c < components; ++c) {
                out[i * static_cast<std::size_t>(components) + static_cast<std::size_t>(c)] =
                    static_cast<float>(readComponent(element + c * size, accessor.componentType,
                                                     accessor.normalized));
            }
        }
    }

    if (accessor.sparse) {
        for (std::size_t k = 0; k < accessor.sparseCount; ++k) {
            const unsigned char* indexPtr = accessor.sparseIndices + k * accessor.sparseIndexStride;
            const uint32_t target = readComponentUint(indexPtr, accessor.sparseIndexType);
            if (target >= count) continue;
            const unsigned char* valuePtr = accessor.sparseValues + k * accessor.sparseValueStride;
            for (int c = 0; c < components; ++c) {
                out[static_cast<std::size_t>(target) * static_cast<std::size_t>(components) +
                    static_cast<std::size_t>(c)] =
                    static_cast<float>(readComponent(valuePtr + c * size, accessor.componentType,
                                                     accessor.normalized));
            }
        }
    }
    return true;
}

bool readAccessorUints(const Doc& doc, int accessorIndex,
                       std::vector<uint32_t>& out, int& components, std::size_t& count,
                       std::string& error) {
    Accessor accessor;
    if (!resolveAccessor(doc, accessorIndex, accessor, error)) return false;
    components = accessor.components;
    count = accessor.count;
    out.assign(count * static_cast<std::size_t>(components), 0u);

    const int size = componentSize(accessor.componentType);
    if (accessor.base) {
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* element = accessor.base + i * accessor.stride;
            for (int c = 0; c < components; ++c) {
                out[i * static_cast<std::size_t>(components) + static_cast<std::size_t>(c)] =
                    readComponentUint(element + c * size, accessor.componentType);
            }
        }
    }

    if (accessor.sparse) {
        for (std::size_t k = 0; k < accessor.sparseCount; ++k) {
            const uint32_t target = readComponentUint(
                accessor.sparseIndices + k * accessor.sparseIndexStride, accessor.sparseIndexType);
            if (target >= count) continue;
            const unsigned char* valuePtr = accessor.sparseValues + k * accessor.sparseValueStride;
            for (int c = 0; c < components; ++c) {
                out[static_cast<std::size_t>(target) * static_cast<std::size_t>(components) +
                    static_cast<std::size_t>(c)] =
                    readComponentUint(valuePtr + c * size, accessor.componentType);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Textures / images
// ---------------------------------------------------------------------------

GLenum glWrap(int wrap) {
    switch (wrap) {
        case 33071: return GL_CLAMP_TO_EDGE;
        case 33648: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
    }
}

GLenum glMinFilter(int filter) {
    switch (filter) {
        case 9728: return GL_NEAREST;
        case 9729: return GL_LINEAR;
        case 9984: return GL_NEAREST_MIPMAP_NEAREST;
        case 9985: return GL_LINEAR_MIPMAP_NEAREST;
        case 9986: return GL_NEAREST_MIPMAP_LINEAR;
        case 9987: return GL_LINEAR_MIPMAP_LINEAR;
        default: return GL_LINEAR_MIPMAP_LINEAR;
    }
}

GLenum glMagFilter(int filter) {
    switch (filter) {
        case 9728: return GL_NEAREST;
        case 9729: return GL_LINEAR;
        default: return GL_LINEAR;
    }
}

bool imageBytes(const Doc& doc, const gltfjson::Value& image,
                std::vector<unsigned char>& out, std::string& error) {
    const int bufferView = image["bufferView"].asInt(-1);
    if (bufferView >= 0) {
        BufferView view;
        if (!resolveBufferView(doc, bufferView, view, error)) return false;
        out.assign(view.data, view.data + view.length);
        return true;
    }
    const std::string uri = image["uri"].asString();
    if (uri.empty()) {
        error = "image has neither uri nor bufferView";
        return false;
    }
    if (uri.rfind("data:", 0) == 0) return decodeDataUri(uri, out);
    return readFileBytes((fs::path(doc.baseDir) / uri).string(), out);
}

GLuint createTexture(const std::vector<unsigned char>& bytes,
                     GLenum wrapS, GLenum wrapT, GLenum minFilter, GLenum magFilter) {
    if (bytes.empty()) return 0;
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return texture;
}

GLuint loadTexture(const Doc& doc, int textureIndex,
                   std::vector<GLuint>& ownedTextures, std::string& error) {
    if (textureIndex < 0) return 0;
    const gltfjson::Value& textures = doc.root["textures"];
    if (static_cast<std::size_t>(textureIndex) >= textures.size()) return 0;
    const gltfjson::Value& texture = textures.at(static_cast<std::size_t>(textureIndex));

    const int imageIndex = texture["source"].asInt(-1);
    const gltfjson::Value& images = doc.root["images"];
    if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= images.size()) return 0;

    std::vector<unsigned char> bytes;
    if (!imageBytes(doc, images.at(static_cast<std::size_t>(imageIndex)), bytes, error)) {
        return 0;
    }

    GLenum wrapS = GL_REPEAT, wrapT = GL_REPEAT;
    GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR, magFilter = GL_LINEAR;
    const int samplerIndex = texture["sampler"].asInt(-1);
    const gltfjson::Value& samplers = doc.root["samplers"];
    if (samplerIndex >= 0 && static_cast<std::size_t>(samplerIndex) < samplers.size()) {
        const gltfjson::Value& sampler = samplers.at(static_cast<std::size_t>(samplerIndex));
        wrapS = glWrap(sampler["wrapS"].asInt(10497));
        wrapT = glWrap(sampler["wrapT"].asInt(10497));
        minFilter = glMinFilter(sampler["minFilter"].asInt(9987));
        magFilter = glMagFilter(sampler["magFilter"].asInt(9729));
    }

    const GLuint id = createTexture(bytes, wrapS, wrapT, minFilter, magFilter);
    if (id) ownedTextures.push_back(id);
    return id;
}

// ---------------------------------------------------------------------------
// Node matrix decomposition
// ---------------------------------------------------------------------------

void decomposeMatrix(const glm::mat4& matrix, glm::vec3& translation,
                     glm::quat& rotation, glm::vec3& scale) {
    translation = glm::vec3(matrix[3]);
    glm::vec3 c0(matrix[0]);
    glm::vec3 c1(matrix[1]);
    glm::vec3 c2(matrix[2]);
    scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (glm::determinant(glm::mat3(matrix)) < 0.0f) scale.x = -scale.x;

    glm::mat3 basis(glm::normalize(c0), glm::normalize(c1), glm::normalize(c2));
    if (scale.x < 0.0f) basis[0] = -basis[0];
    rotation = glm::normalize(glm::quat_cast(basis));
}

// ---------------------------------------------------------------------------
// Animation sampling
// ---------------------------------------------------------------------------

float clampTime(float time, float duration) {
    if (duration <= 0.0f) return 0.0f;
    if (time < 0.0f) return 0.0f;
    return time > duration ? duration : time;
}

glm::vec4 sampleValues(const GltfAnimationSampler& sampler, float time,
                       bool rotation, bool& valid) {
    valid = false;
    if (sampler.times.empty() || sampler.values.empty()) return glm::vec4(0.0f);

    const float duration = sampler.times.back();
    const float t = clampTime(time, duration);
    const std::size_t keyCount = sampler.times.size();
    if (keyCount == 1) {
        valid = true;
        return sampler.interpolation == 2 ? sampler.values[1] : sampler.values[0];
    }

    std::size_t i = 0;
    while (i + 2 < keyCount && t >= sampler.times[i + 1]) ++i;
    if (i + 1 >= keyCount) i = keyCount - 2;

    const float t0 = sampler.times[i];
    const float t1 = sampler.times[i + 1];
    const float span = t1 - t0;
    const float u = span > 1e-8f ? (t - t0) / span : 0.0f;

    auto valueAt = [&](std::size_t key) -> glm::vec4 {
        if (sampler.interpolation == 2) {
            const std::size_t base = key * 3 + 1;  // (inTangent, value, outTangent)
            return base < sampler.values.size() ? sampler.values[base] : glm::vec4(0.0f);
        }
        return key < sampler.values.size() ? sampler.values[key] : glm::vec4(0.0f);
    };

    if (sampler.interpolation == 1) {  // STEP
        valid = true;
        return valueAt(i);
    }

    if (sampler.interpolation == 2) {  // CUBICSPLINE
        const std::size_t base0 = i * 3;
        const std::size_t base1 = (i + 1) * 3;
        const glm::vec4 p0 = sampler.values[base0 + 1];
        const glm::vec4 m0 = sampler.values[base0 + 2];
        const glm::vec4 p1 = sampler.values[base1 + 1];
        const glm::vec4 m1 = sampler.values[base1 + 0];

        const float u2 = u * u;
        const float u3 = u2 * u;
        const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
        const float h10 = u3 - 2.0f * u2 + u;
        const float h01 = -2.0f * u3 + 3.0f * u2;
        const float h11 = u3 - u2;
        glm::vec4 value = h00 * p0 + h10 * span * m0 + h01 * p1 + h11 * span * m1;
        if (rotation) {
            glm::quat q(value.w, value.x, value.y, value.z);
            q = glm::normalize(q);
            value = glm::vec4(q.x, q.y, q.z, q.w);
        }
        valid = true;
        return value;
    }

    // LINEAR
    const glm::vec4 a = valueAt(i);
    const glm::vec4 b = valueAt(i + 1);
    if (rotation) {
        const glm::quat qa(a.w, a.x, a.y, a.z);
        const glm::quat qb(b.w, b.x, b.y, b.z);
        const glm::quat q = glm::normalize(glm::slerp(qa, qb, u));
        valid = true;
        return glm::vec4(q.x, q.y, q.z, q.w);
    }
    valid = true;
    return glm::mix(a, b, u);
}

// ---------------------------------------------------------------------------
// GPU vertex layout
// ---------------------------------------------------------------------------

struct GpuVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
    glm::uvec4 joints;
    glm::vec4 weights;
};

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

struct Loader {
    std::vector<GltfMesh>& meshes;
    std::vector<GltfMaterial>& materials;
    std::vector<GltfNode>& nodes;
    std::vector<GltfSkin>& skins;
    std::vector<GltfAnimation>& animations;
    std::vector<int>& roots;
    std::vector<int>& order;
    std::vector<int>& meshNodes;
    std::vector<GLuint>& textures;

    bool buildMaterials(const Doc& doc, std::string& error) {
        const gltfjson::Value& array = doc.root["materials"];
        materials.reserve(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const gltfjson::Value& source = array.at(i);
            GltfMaterial material;
            const gltfjson::Value& pbr = source["pbrMetallicRoughness"];
            const gltfjson::Value& base = pbr["baseColorFactor"];
            if (base.size() >= 4) {
                material.baseColorFactor = glm::vec4(base.at(0).asFloat(1.0f),
                                                     base.at(1).asFloat(1.0f),
                                                     base.at(2).asFloat(1.0f),
                                                     base.at(3).asFloat(1.0f));
            }
            material.metallicFactor = pbr["metallicFactor"].asFloat(1.0f);
            material.roughnessFactor = pbr["roughnessFactor"].asFloat(1.0f);
            material.baseColorTexture = loadTexture(doc, pbr["baseColorTexture"]["index"].asInt(-1),
                                                    textures, error);
            material.hasBaseColorTexture = material.baseColorTexture != 0;

            const gltfjson::Value& emissive = source["emissiveFactor"];
            if (emissive.size() >= 3) {
                material.emissiveFactor = glm::vec3(emissive.at(0).asFloat(0.0f),
                                                    emissive.at(1).asFloat(0.0f),
                                                    emissive.at(2).asFloat(0.0f));
            }
            material.emissiveTexture = loadTexture(doc, source["emissiveTexture"]["index"].asInt(-1),
                                                   textures, error);
            material.hasEmissiveTexture = material.emissiveTexture != 0;

            const std::string alpha = lowerCopy(source["alphaMode"].asStringOr("OPAQUE"));
            material.alphaMode = alpha == "MASK" ? 1 : (alpha == "BLEND" ? 2 : 0);
            material.alphaCutoff = source["alphaCutoff"].asFloat(0.5f);
            material.doubleSided = source["doubleSided"].asBool(false);
            materials.push_back(material);
        }
        return true;
    }

    bool buildMeshPrimitives(const Doc& doc, const gltfjson::Value& meshValue, GltfMesh& mesh,
                             std::string& error) {
        const gltfjson::Value& primitives = meshValue["primitives"];
        for (std::size_t p = 0; p < primitives.size(); ++p) {
            const gltfjson::Value& primitive = primitives.at(p);
            const int mode = primitive["mode"].asInt(4);
            if (mode != 4) continue;  // only TRIANGLES

            const gltfjson::Value& attributes = primitive["attributes"];
            const int positionAccessor = attributes["POSITION"].asInt(-1);
            if (positionAccessor < 0) continue;

            std::vector<float> positions;
            int positionComponents = 0;
            std::size_t vertexCount = 0;
            if (!readAccessorFloats(doc, positionAccessor, positions, positionComponents,
                                    vertexCount, error)) {
                return false;
            }
            if (positionComponents < 3 || vertexCount == 0) continue;

            std::vector<float> normals;
            int normalComponents = 0;
            std::size_t normalCount = 0;
            readAccessorFloats(doc, attributes["NORMAL"].asInt(-1), normals,
                               normalComponents, normalCount, error);
            error.clear();

            std::vector<float> uvs;
            int uvComponents = 0;
            std::size_t uvCount = 0;
            readAccessorFloats(doc, attributes["TEXCOORD_0"].asInt(-1), uvs,
                               uvComponents, uvCount, error);
            error.clear();

            std::vector<float> colors;
            int colorComponents = 0;
            std::size_t colorCount = 0;
            readAccessorFloats(doc, attributes["COLOR_0"].asInt(-1), colors,
                               colorComponents, colorCount, error);
            error.clear();

            std::vector<uint32_t> joints;
            int jointComponents = 0;
            std::size_t jointCount = 0;
            readAccessorUints(doc, attributes["JOINTS_0"].asInt(-1), joints,
                              jointComponents, jointCount, error);
            error.clear();

            std::vector<float> weights;
            int weightComponents = 0;
            std::size_t weightCount = 0;
            readAccessorFloats(doc, attributes["WEIGHTS_0"].asInt(-1), weights,
                               weightComponents, weightCount, error);
            error.clear();

            std::vector<GpuVertex> vertices(vertexCount);
            for (std::size_t v = 0; v < vertexCount; ++v) {
                GpuVertex& out = vertices[v];
                out.position = glm::vec3(positions[v * 3 + 0], positions[v * 3 + 1],
                                         positions[v * 3 + 2]);
                out.normal = (normalCount == vertexCount && normalComponents >= 3)
                                 ? glm::vec3(normals[v * static_cast<std::size_t>(normalComponents) + 0],
                                             normals[v * static_cast<std::size_t>(normalComponents) + 1],
                                             normals[v * static_cast<std::size_t>(normalComponents) + 2])
                                 : glm::vec3(0.0f, 1.0f, 0.0f);
                out.uv = (uvCount == vertexCount && uvComponents >= 2)
                             ? glm::vec2(uvs[v * static_cast<std::size_t>(uvComponents) + 0],
                                         uvs[v * static_cast<std::size_t>(uvComponents) + 1])
                             : glm::vec2(0.0f);
                if (colorCount == vertexCount && colorComponents >= 3) {
                    out.color = glm::vec4(
                        colors[v * static_cast<std::size_t>(colorComponents) + 0],
                        colors[v * static_cast<std::size_t>(colorComponents) + 1],
                        colors[v * static_cast<std::size_t>(colorComponents) + 2],
                        colorComponents >= 4
                            ? colors[v * static_cast<std::size_t>(colorComponents) + 3]
                            : 1.0f);
                } else {
                    out.color = glm::vec4(1.0f);
                }
                if (jointCount == vertexCount && jointComponents >= 1) {
                    out.joints = glm::uvec4(
                        joints[v * static_cast<std::size_t>(jointComponents) + 0],
                        jointComponents >= 2 ? joints[v * static_cast<std::size_t>(jointComponents) + 1] : 0u,
                        jointComponents >= 3 ? joints[v * static_cast<std::size_t>(jointComponents) + 2] : 0u,
                        jointComponents >= 4 ? joints[v * static_cast<std::size_t>(jointComponents) + 3] : 0u);
                } else {
                    out.joints = glm::uvec4(0u);
                }
                if (weightCount == vertexCount && weightComponents >= 1) {
                    out.weights = glm::vec4(
                        weights[v * static_cast<std::size_t>(weightComponents) + 0],
                        weightComponents >= 2 ? weights[v * static_cast<std::size_t>(weightComponents) + 1] : 0.0f,
                        weightComponents >= 3 ? weights[v * static_cast<std::size_t>(weightComponents) + 2] : 0.0f,
                        weightComponents >= 4 ? weights[v * static_cast<std::size_t>(weightComponents) + 3] : 0.0f);
                    const float sum = out.weights.x + out.weights.y + out.weights.z + out.weights.w;
                    if (sum > 1e-6f) out.weights /= sum;
                } else {
                    out.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }

            std::vector<uint32_t> indices;
            const int indexAccessor = primitive["indices"].asInt(-1);
            if (indexAccessor >= 0) {
                int indexComponents = 0;
                std::size_t indexCount = 0;
                if (!readAccessorUints(doc, indexAccessor, indices, indexComponents,
                                       indexCount, error)) {
                    return false;
                }
            } else {
                indices.resize(vertexCount);
                for (std::size_t v = 0; v < vertexCount; ++v) {
                    indices[v] = static_cast<uint32_t>(v);
                }
            }

            GltfPrimitive out;
            out.indexCount = static_cast<GLsizei>(indices.size());
            out.material = primitive["material"].asInt(-1);

            glGenVertexArrays(1, &out.vao);
            glGenBuffers(1, &out.vbo);
            glGenBuffers(1, &out.ebo);
            glBindVertexArray(out.vao);
            glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)),
                         vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                         indices.data(), GL_STATIC_DRAW);

            const GLsizei stride = sizeof(GpuVertex);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, position));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, uv));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, color));
            glEnableVertexAttribArray(4);
            glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, stride, (void*)offsetof(GpuVertex, joints));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, weights));

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            mesh.primitives.push_back(out);
        }
        return true;
    }

    bool buildMeshes(const Doc& doc, std::string& error) {
        const gltfjson::Value& array = doc.root["meshes"];
        meshes.resize(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (!buildMeshPrimitives(doc, array.at(i), meshes[i], error)) return false;
            error.clear();
        }
        return true;
    }

    void buildNodes(const Doc& doc) {
        const gltfjson::Value& array = doc.root["nodes"];
        nodes.resize(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const gltfjson::Value& source = array.at(i);
            GltfNode& node = nodes[i];
            node.name = source["name"].asString();
            node.mesh = source["mesh"].asInt(-1);
            node.skin = source["skin"].asInt(-1);

            const gltfjson::Value& matrix = source["matrix"];
            if (matrix.size() >= 16) {
                glm::mat4 m(1.0f);
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        m[c][r] = matrix.at(static_cast<std::size_t>(c * 4 + r)).asFloat(0.0f);
                    }
                }
                decomposeMatrix(m, node.translation, node.rotation, node.scale);
            } else {
                const gltfjson::Value& t = source["translation"];
                if (t.size() >= 3) {
                    node.translation = glm::vec3(t.at(0).asFloat(), t.at(1).asFloat(), t.at(2).asFloat());
                }
                const gltfjson::Value& r = source["rotation"];
                if (r.size() >= 4) {
                    node.rotation = glm::normalize(glm::quat(r.at(3).asFloat(1.0f),
                                                             r.at(0).asFloat(0.0f),
                                                             r.at(1).asFloat(0.0f),
                                                             r.at(2).asFloat(0.0f)));
                }
                const gltfjson::Value& s = source["scale"];
                if (s.size() >= 3) {
                    node.scale = glm::vec3(s.at(0).asFloat(1.0f), s.at(1).asFloat(1.0f), s.at(2).asFloat(1.0f));
                }
            }

            const gltfjson::Value& children = source["children"];
            node.children.reserve(children.size());
            for (std::size_t c = 0; c < children.size(); ++c) {
                const int child = children.at(c).asInt(-1);
                if (child >= 0 && static_cast<std::size_t>(child) < nodes.size()) {
                    node.children.push_back(child);
                    nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(i);
                }
            }
        }

        // Scene roots (first scene, or every node without a parent).
        roots.clear();
        const gltfjson::Value& scenes = doc.root["scenes"];
        const int sceneIndex = doc.root["scene"].asInt(0);
        if (sceneIndex >= 0 && static_cast<std::size_t>(sceneIndex) < scenes.size()) {
            const gltfjson::Value& sceneNodes = scenes.at(static_cast<std::size_t>(sceneIndex))["nodes"];
            for (std::size_t i = 0; i < sceneNodes.size(); ++i) {
                const int index = sceneNodes.at(i).asInt(-1);
                if (index >= 0 && static_cast<std::size_t>(index) < nodes.size()) roots.push_back(index);
            }
        }
        if (roots.empty()) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].parent < 0) roots.push_back(static_cast<int>(i));
            }
        }

        // Topological order: parents before children.
        order.clear();
        std::vector<bool> visited(nodes.size(), false);
        std::vector<int> stack;
        for (int root : roots) stack.push_back(root);
        while (!stack.empty()) {
            const int index = stack.back();
            stack.pop_back();
            if (index < 0 || static_cast<std::size_t>(index) >= nodes.size() || visited[static_cast<std::size_t>(index)]) {
                continue;
            }
            visited[static_cast<std::size_t>(index)] = true;
            order.push_back(index);
            const GltfNode& node = nodes[static_cast<std::size_t>(index)];
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!visited[i]) {
                // Orphan node: give it a parent above it so it still draws.
                roots.push_back(static_cast<int>(i));
                order.push_back(static_cast<int>(i));
                visited[i] = true;
            }
        }

        meshNodes.clear();
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].mesh >= 0) meshNodes.push_back(static_cast<int>(i));
        }
    }

    bool buildSkins(const Doc& doc, std::string& error) {
        const gltfjson::Value& array = doc.root["skins"];
        skins.resize(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const gltfjson::Value& source = array.at(i);
            GltfSkin& skin = skins[i];
            const gltfjson::Value& joints = source["joints"];
            skin.joints.reserve(joints.size());
            for (std::size_t j = 0; j < joints.size(); ++j) {
                skin.joints.push_back(joints.at(j).asInt(-1));
            }

            const int ibmAccessor = source["inverseBindMatrices"].asInt(-1);
            if (ibmAccessor >= 0) {
                std::vector<float> values;
                int components = 0;
                std::size_t count = 0;
                if (!readAccessorFloats(doc, ibmAccessor, values, components, count, error)) {
                    return false;
                }
                if (components == 16) {
                    skin.inverseBindMatrices.resize(count);
                    for (std::size_t m = 0; m < count; ++m) {
                        skin.inverseBindMatrices[m] = glm::make_mat4(&values[m * 16]);
                    }
                }
            }
            if (skin.inverseBindMatrices.size() < skin.joints.size()) {
                skin.inverseBindMatrices.resize(skin.joints.size(), glm::mat4(1.0f));
            }
        }
        return true;
    }

    bool buildAnimations(const Doc& doc, std::string& error) {
        const gltfjson::Value& array = doc.root["animations"];
        animations.resize(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const gltfjson::Value& source = array.at(i);
            GltfAnimation& animation = animations[i];
            animation.name = source["name"].asString();

            const gltfjson::Value& samplers = source["samplers"];
            animation.samplers.resize(samplers.size());
            for (std::size_t s = 0; s < samplers.size(); ++s) {
                const gltfjson::Value& sourceSampler = samplers.at(s);
                GltfAnimationSampler& sampler = animation.samplers[s];

                std::vector<float> input;
                int inputComponents = 0;
                std::size_t inputCount = 0;
                if (!readAccessorFloats(doc, sourceSampler["input"].asInt(-1), input,
                                        inputComponents, inputCount, error)) {
                    return false;
                }
                sampler.times.resize(inputCount);
                for (std::size_t k = 0; k < inputCount; ++k) {
                    sampler.times[k] = input[k];
                    animation.duration = std::max(animation.duration, input[k]);
                }

                std::vector<float> output;
                int outputComponents = 0;
                std::size_t outputCount = 0;
                if (!readAccessorFloats(doc, sourceSampler["output"].asInt(-1), output,
                                        outputComponents, outputCount, error)) {
                    return false;
                }
                sampler.values.resize(outputCount);
                for (std::size_t k = 0; k < outputCount; ++k) {
                    sampler.values[k] = glm::vec4(
                        output[k * static_cast<std::size_t>(outputComponents) + 0],
                        outputComponents >= 2 ? output[k * static_cast<std::size_t>(outputComponents) + 1] : 0.0f,
                        outputComponents >= 3 ? output[k * static_cast<std::size_t>(outputComponents) + 2] : 0.0f,
                        outputComponents >= 4 ? output[k * static_cast<std::size_t>(outputComponents) + 3] : 0.0f);
                }

                const std::string interpolation = lowerCopy(sourceSampler["interpolation"].asStringOr("LINEAR"));
                sampler.interpolation = interpolation == "STEP" ? 1
                                      : interpolation == "CUBICSPLINE" ? 2 : 0;
            }

            const gltfjson::Value& channels = source["channels"];
            animation.channels.reserve(channels.size());
            for (std::size_t c = 0; c < channels.size(); ++c) {
                const gltfjson::Value& channel = channels.at(c);
                const std::string path = channel["target"]["path"].asString();
                int pathIndex = -1;
                if (path == "translation") pathIndex = 0;
                else if (path == "rotation") pathIndex = 1;
                else if (path == "scale") pathIndex = 2;
                if (pathIndex < 0) continue;  // morph weights are not supported

                GltfAnimationChannel out;
                out.node = channel["target"]["node"].asInt(-1);
                out.path = pathIndex;
                out.sampler = channel["sampler"].asInt(-1);
                if (out.node >= 0 && out.sampler >= 0) animation.channels.push_back(out);
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void setMaterialUniforms(Shader& shader, const GltfMaterial& material) {
    shader.setVec4("uBaseColorFactor", material.baseColorFactor);
    shader.setVec3("uEmissiveFactor", material.emissiveFactor);
    shader.setFloat("uMetallic", material.metallicFactor);
    shader.setFloat("uRoughness", material.roughnessFactor);
    shader.setInt("uAlphaMode", material.alphaMode);
    shader.setFloat("uAlphaCutoff", material.alphaCutoff);
    shader.setInt("uHasBaseColorTexture", material.hasBaseColorTexture ? 1 : 0);
    shader.setInt("uHasEmissiveTexture", material.hasEmissiveTexture ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, material.baseColorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, material.emissiveTexture);
    glActiveTexture(GL_TEXTURE0);
}

GltfMaterial defaultMaterial() {
    GltfMaterial material;
    material.baseColorFactor = glm::vec4(1.0f);
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 0.85f;
    return material;
}

}  // namespace

// ---------------------------------------------------------------------------
// GltfModel
// ---------------------------------------------------------------------------

GltfModel::GltfModel() = default;

GltfModel::~GltfModel() {
    for (const GltfMesh& mesh : meshes_) {
        for (const GltfPrimitive& primitive : mesh.primitives) {
            if (primitive.vao) glDeleteVertexArrays(1, &primitive.vao);
            if (primitive.vbo) glDeleteBuffers(1, &primitive.vbo);
            if (primitive.ebo) glDeleteBuffers(1, &primitive.ebo);
        }
    }
    if (!textures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    }
}

bool GltfModel::load(const std::string& path) {
    loaded_ = false;
    error_.clear();

    Doc doc;
    if (!parseGltfFile(path, doc, error_)) return false;

    Loader loader{meshes_, materials_, nodes_, skins_, animations_,
                  roots_, order_, meshNodes_, textures_};
    if (!loader.buildMaterials(doc, error_)) return false;
    if (!loader.buildMeshes(doc, error_)) return false;
    loader.buildNodes(doc);
    if (!loader.buildSkins(doc, error_)) return false;
    if (!loader.buildAnimations(doc, error_)) return false;

    loaded_ = true;
    return true;
}

const std::string& GltfModel::animationName(int index) const {
    static const std::string empty;
    if (index < 0 || static_cast<std::size_t>(index) >= animations_.size()) return empty;
    return animations_[static_cast<std::size_t>(index)].name;
}

float GltfModel::animationDuration(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= animations_.size()) return 0.0f;
    return animations_[static_cast<std::size_t>(index)].duration;
}

int GltfModel::findAnimation(const std::string& name) const {
    const std::string wanted = lowerCopy(name);
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        if (lowerCopy(animations_[i].name) == wanted) return static_cast<int>(i);
    }
    return -1;
}

int GltfModel::findNode(const std::string& name) const {
    const std::string wanted = lowerCopy(name);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (lowerCopy(nodes_[i].name) == wanted) return static_cast<int>(i);
    }
    return -1;
}

void GltfModel::draw(const GltfInstance& instance, Shader& shader,
                     const glm::mat4& transform, const glm::vec3& tint,
                     const glm::mat4& viewProjection) const {
    if (!loaded_ || instance.model_ != this) return;

    shader.use();
    shader.setMat4("uViewProjection", glm::value_ptr(viewProjection));
    shader.setVec3("uTint", tint);
    shader.setVec3("uLightDir", glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f)));
    shader.setVec3("uLightColor", glm::vec3(0.95f));
    shader.setInt("uBaseColorTexture", 0);
    shader.setInt("uEmissiveTexture", 1);

    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
    const glm::vec4 camera = inverseViewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shader.setVec3("uCameraPos", camera.w != 0.0f ? glm::vec3(camera) / camera.w
                                                  : glm::vec3(0.0f));

    const GltfMaterial fallback = defaultMaterial();
    const GLboolean cullingWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);

    for (int nodeIndex : meshNodes_) {
        const GltfNode& node = nodes_[static_cast<std::size_t>(nodeIndex)];
        if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= meshes_.size()) continue;
        const GltfMesh& mesh = meshes_[static_cast<std::size_t>(node.mesh)];

        const bool skinned = node.skin >= 0 &&
                             static_cast<std::size_t>(node.skin) < skins_.size();
        if (skinned) {
            const GltfSkin& skin = skins_[static_cast<std::size_t>(node.skin)];
            const glm::mat4 meshInverse = glm::inverse(instance.globalTransform(nodeIndex));
            std::vector<glm::mat4> bones(kMaxBones, glm::mat4(1.0f));
            const std::size_t count = std::min<std::size_t>(skin.joints.size(), kMaxBones);
            for (std::size_t j = 0; j < count; ++j) {
                const int joint = skin.joints[j];
                bones[j] = meshInverse * instance.globalTransform(joint) *
                           skin.inverseBindMatrices[j];
            }
            shader.setMat4Array("uBones", glm::value_ptr(bones[0]), static_cast<int>(kMaxBones));
            shader.setInt("uHasSkin", 1);
            shader.setMat4("uModel", glm::value_ptr(transform));
        } else {
            shader.setInt("uHasSkin", 0);
            const glm::mat4 model = transform * instance.globalTransform(nodeIndex);
            shader.setMat4("uModel", glm::value_ptr(model));
        }

        for (const GltfPrimitive& primitive : mesh.primitives) {
            const GltfMaterial& material =
                (primitive.material >= 0 &&
                 static_cast<std::size_t>(primitive.material) < materials_.size())
                    ? materials_[static_cast<std::size_t>(primitive.material)]
                    : fallback;

            setMaterialUniforms(shader, material);

            if (material.doubleSided) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);

            if (material.alphaMode == 2) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            } else {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }

            glBindVertexArray(primitive.vao);
            glDrawElements(GL_TRIANGLES, primitive.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(0);

    // Restore the state the box pass expects.
    if (cullingWasEnabled) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    glDepthMask(depthWriteWasEnabled);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// GltfInstance
// ---------------------------------------------------------------------------

void GltfInstance::init(const GltfModel* model) {
    model_ = model;
    rest_.clear();
    pose_.clear();
    globals_.clear();
    if (!model_ || !model_->loaded_) return;

    rest_.resize(model_->nodes_.size());
    for (std::size_t i = 0; i < model_->nodes_.size(); ++i) {
        const GltfNode& node = model_->nodes_[i];
        rest_[i].translation = node.translation;
        rest_[i].rotation = node.rotation;
        rest_[i].scale = node.scale;
    }
    pose_ = rest_;
    globals_.resize(rest_.size(), glm::mat4(1.0f));
    poseDirty_ = true;
}

void GltfInstance::setPose(int clipA, float timeA, float weightA,
                           int clipB, float timeB, float weightB) {
    clipA_ = clipA;
    clipB_ = clipB;
    timeA_ = timeA;
    timeB_ = timeB;
    weightA_ = std::max(0.0f, weightA);
    weightB_ = std::max(0.0f, weightB);
    poseDirty_ = true;
}

void GltfInstance::clearNodeOverrides() {
    overrides_.clear();
    poseDirty_ = true;
}

void GltfInstance::setNodeRotationOffset(int nodeIndex, const glm::quat& delta) {
    if (nodeIndex < 0) return;
    if (static_cast<std::size_t>(nodeIndex) >= overrides_.size()) {
        overrides_.resize(static_cast<std::size_t>(nodeIndex) + 1);
    }
    overrides_[static_cast<std::size_t>(nodeIndex)].hasRotation = true;
    overrides_[static_cast<std::size_t>(nodeIndex)].rotationDelta =
        glm::normalize(delta);
    poseDirty_ = true;
}

void GltfInstance::setNodeTranslationOffset(int nodeIndex, const glm::vec3& delta) {
    if (nodeIndex < 0) return;
    if (static_cast<std::size_t>(nodeIndex) >= overrides_.size()) {
        overrides_.resize(static_cast<std::size_t>(nodeIndex) + 1);
    }
    overrides_[static_cast<std::size_t>(nodeIndex)].hasTranslation = true;
    overrides_[static_cast<std::size_t>(nodeIndex)].translationDelta = delta;
    poseDirty_ = true;
}

glm::mat4 GltfInstance::localMatrix(const LocalTRS& local) {
    return glm::translate(glm::mat4(1.0f), local.translation) *
           glm::mat4_cast(local.rotation) *
           glm::scale(glm::mat4(1.0f), local.scale);
}

void GltfInstance::evaluateClip(const GltfAnimation& animation,
                                float time, std::vector<LocalTRS>& pose) {
    for (const GltfAnimationChannel& channel : animation.channels) {
        if (channel.node < 0 || static_cast<std::size_t>(channel.node) >= pose.size()) continue;
        if (channel.sampler < 0 ||
            static_cast<std::size_t>(channel.sampler) >= animation.samplers.size()) {
            continue;
        }
        const GltfAnimationSampler& sampler =
            animation.samplers[static_cast<std::size_t>(channel.sampler)];
        bool valid = false;
        const glm::vec4 value = sampleValues(sampler, time, channel.path == 1, valid);
        if (!valid) continue;

        LocalTRS& local = pose[static_cast<std::size_t>(channel.node)];
        if (channel.path == 0) {
            local.translation = glm::vec3(value);
        } else if (channel.path == 1) {
            glm::quat q(value.w, value.x, value.y, value.z);
            if (glm::dot(q, q) > 1e-12f) local.rotation = glm::normalize(q);
        } else if (channel.path == 2) {
            local.scale = glm::vec3(value);
        }
    }
}

void GltfInstance::update() {
    if (!model_ || !model_->loaded_ || rest_.empty()) return;
    if (!poseDirty_) return;

    const std::size_t nodeCount = rest_.size();
    const bool validA = clipA_ >= 0 && static_cast<std::size_t>(clipA_) < model_->animations_.size();
    const bool validB = clipB_ >= 0 && static_cast<std::size_t>(clipB_) < model_->animations_.size();

    std::vector<LocalTRS> poseA;
    std::vector<LocalTRS> poseB;
    if (validA) {
        poseA = rest_;
        evaluateClip(model_->animations_[static_cast<std::size_t>(clipA_)], timeA_, poseA);
    }
    if (validB) {
        poseB = rest_;
        evaluateClip(model_->animations_[static_cast<std::size_t>(clipB_)], timeB_, poseB);
    }

    const float wa = validA ? weightA_ : 0.0f;
    const float wb = validB ? weightB_ : 0.0f;
    const float total = std::min(1.0f, wa + wb);
    if (total <= 1e-5f) {
        pose_ = rest_;
    } else {
        pose_.resize(nodeCount);
        const float denominator = wa + wb;
        for (std::size_t i = 0; i < nodeCount; ++i) {
            LocalTRS blended = rest_[i];
            if (validA && validB && denominator > 1e-6f) {
                const float weightA = wa / denominator;
                const float weightB = wb / denominator;
                blended.translation = poseA[i].translation * weightA + poseB[i].translation * weightB;
                blended.rotation = glm::normalize(glm::slerp(poseA[i].rotation, poseB[i].rotation, weightB));
                blended.scale = poseA[i].scale * weightA + poseB[i].scale * weightB;
            } else if (validA) {
                blended = poseA[i];
            } else if (validB) {
                blended = poseB[i];
            }
            if (total < 1.0f) {
                blended.translation = glm::mix(rest_[i].translation, blended.translation, total);
                blended.rotation = glm::normalize(
                    glm::slerp(rest_[i].rotation, blended.rotation, total));
                blended.scale = glm::mix(rest_[i].scale, blended.scale, total);
            }
            pose_[i] = blended;
        }
    }

    // Stance overrides are applied on top of the evaluated clip so crouching
    // and prone poses can bend the existing skeleton instead of scaling the
    // whole model.
    const std::size_t overrideCount = std::min(overrides_.size(), pose_.size());
    for (std::size_t i = 0; i < overrideCount; ++i) {
        const NodeOverride& override = overrides_[i];
        if (override.hasTranslation) pose_[i].translation += override.translationDelta;
        if (override.hasRotation) {
            pose_[i].rotation = glm::normalize(pose_[i].rotation * override.rotationDelta);
        }
    }

    rebuildGlobals();
    poseDirty_ = false;
}

void GltfInstance::rebuildGlobals() {
    if (!model_) return;
    globals_.assign(rest_.size(), glm::mat4(1.0f));
    for (int index : model_->order_) {
        if (index < 0 || static_cast<std::size_t>(index) >= pose_.size()) continue;
        const GltfNode& node = model_->nodes_[static_cast<std::size_t>(index)];
        const glm::mat4 local = localMatrix(pose_[static_cast<std::size_t>(index)]);
        if (node.parent >= 0 && static_cast<std::size_t>(node.parent) < globals_.size()) {
            globals_[static_cast<std::size_t>(index)] =
                globals_[static_cast<std::size_t>(node.parent)] * local;
        } else {
            globals_[static_cast<std::size_t>(index)] = local;
        }
    }
}

const glm::mat4& GltfInstance::globalTransform(int nodeIndex) const {
    static const glm::mat4 identity(1.0f);
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= globals_.size()) return identity;
    return globals_[static_cast<std::size_t>(nodeIndex)];
}
