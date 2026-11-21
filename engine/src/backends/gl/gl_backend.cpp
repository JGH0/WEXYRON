/**
 * @file gl_backend.cpp
 * @brief OpenGL 3.3 core implementation of IRenderBackend (reference backend).
 *
 * This is the GL code that used to live in Renderer, moved here unchanged so
 * the frontend no longer touches GL. The GL call order is kept exactly as it
 * was: frames must stay pixel-identical (tests/render_capture_test.cpp).
 */

#include "engine/render_backend.hpp"
#include "engine/font_atlas.hpp"
#include "engine/gltf_model.hpp"
#include "engine/shader.hpp"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <iostream>

namespace {

/// Unit quad (two triangles) in the XY plane, plus the corner each vertex
/// represents for UV mapping. Every box face is built from this in the shader.
/// Six vertices: (-,-) (+,-) (+,+) / (+,+) (-,+) (-,-)
const float kUnitQuad[] = {
    // aPos.x, aPos.y,  aCorner.x, aCorner.y
    -0.5f, -0.5f,   -0.5f, -0.5f,
     0.5f, -0.5f,    0.5f, -0.5f,
     0.5f,  0.5f,    0.5f,  0.5f,
     0.5f,  0.5f,    0.5f,  0.5f,
    -0.5f,  0.5f,   -0.5f,  0.5f,
    -0.5f, -0.5f,   -0.5f, -0.5f,
};

constexpr int kQuadVertexCount = 6;

// ---------------------------------------------------------------------------
// glTF models: GPU copy created on first draw, released with the model
// ---------------------------------------------------------------------------

GLenum glWrap(GltfWrap wrap) {
    switch (wrap) {
        case GltfWrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case GltfWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
    }
}

GLenum glMinFilter(const GltfImage& image) {
    const bool nearest = image.minFilter == GltfFilter::Nearest;
    if (!image.mipmaps) return nearest ? GL_NEAREST : GL_LINEAR;
    const bool mipNearest = image.mipFilter == GltfFilter::Nearest;
    if (nearest) return mipNearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
    return mipNearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
}

class GlGltfData final : public GltfGpuData {
public:
    struct Primitive {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
    };
    std::vector<std::vector<Primitive>> meshes;  ///< [mesh][primitive]
    std::vector<GLuint> textures;                ///< one per GltfModel::images() entry

    explicit GlGltfData(const GltfModel& model) {
        textures.reserve(model.images().size());
        for (const GltfImage& image : model.images()) textures.push_back(uploadImage(image));

        meshes.resize(model.meshes().size());
        for (std::size_t m = 0; m < model.meshes().size(); ++m) {
            for (const GltfPrimitive& source : model.meshes()[m].primitives) {
                meshes[m].push_back(uploadPrimitive(source));
            }
        }
    }

    ~GlGltfData() override {
        for (const auto& mesh : meshes) {
            for (const Primitive& primitive : mesh) {
                if (primitive.vao) glDeleteVertexArrays(1, &primitive.vao);
                if (primitive.vbo) glDeleteBuffers(1, &primitive.vbo);
                if (primitive.ebo) glDeleteBuffers(1, &primitive.ebo);
            }
        }
        for (GLuint texture : textures) {
            if (texture) glDeleteTextures(1, &texture);
        }
    }

    GLuint texture(int image) const {
        return (image >= 0 && static_cast<std::size_t>(image) < textures.size())
                   ? textures[static_cast<std::size_t>(image)] : 0;
    }

private:
    static GLuint uploadImage(const GltfImage& image) {
        if (image.width <= 0 || image.height <= 0 || image.rgba.empty()) return 0;
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(glWrap(image.wrapS)));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(glWrap(image.wrapT)));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glMinFilter(image)));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        image.magFilter == GltfFilter::Nearest ? GL_NEAREST : GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        return texture;
    }

    static Primitive uploadPrimitive(const GltfPrimitive& source) {
        Primitive out;
        out.indexCount = static_cast<GLsizei>(source.indices.size());
        glGenVertexArrays(1, &out.vao);
        glGenBuffers(1, &out.vbo);
        glGenBuffers(1, &out.ebo);
        glBindVertexArray(out.vao);
        glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(source.vertices.size() * sizeof(GltfVertex)),
                     source.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(source.indices.size() * sizeof(uint32_t)),
                     source.indices.data(), GL_STATIC_DRAW);

        const GLsizei stride = sizeof(GltfVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GltfVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GltfVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GltfVertex, uv));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GltfVertex, color));
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, stride, (void*)offsetof(GltfVertex, joints));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GltfVertex, weights));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return out;
    }
};

/// The model's GL copy, created on first use (and replaced if another
/// backend's copy is attached).
const GlGltfData& glGltfData(const GltfModel& model) {
    auto* data = dynamic_cast<GlGltfData*>(model.gpuData());
    if (!data) {
        model.setGpuData(std::make_unique<GlGltfData>(model));
        data = static_cast<GlGltfData*>(model.gpuData());
    }
    return *data;
}

class GlRenderBackend final : public IRenderBackend {
public:
    explicit GlRenderBackend(GLFWwindow* window) : window_(window) {}
    ~GlRenderBackend() override {
        // GL objects are released explicitly in shutdown() (on the GL thread).
        delete shader_;
        delete gltfShader_;
    }

    bool init() override;
    void shutdown() override;
    void setTextureArray(const TextureArrayDesc& desc) override;
    void renderFrame(const FramePacket& frame) override;
    void present() override { if (window_) glfwSwapBuffers(window_); }
    void setVsync(bool enabled) override {
        // The swap interval belongs to the current context (this window's).
        if (window_ && glfwGetCurrentContext() == window_) glfwSwapInterval(enabled ? 1 : 0);
    }
    RenderBackendInfo describe() const override;

private:
    /// GPU side of one instance stream.
    struct Batch {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        size_t capacity = 0;   ///< instances the VBO can hold
    };
    enum BatchId { WORLD_STATIC = 0, WORLD_DYNAMIC, VIEWMODEL, UI_SCRATCH, BATCH_COUNT };

    void initBuffers();
    void uploadFontAtlas();
    /// Upload `instances` into a batch's VBO (growing it with slack).
    void upload(BatchId id, const std::vector<BoxInstance>* instances, size_t first, size_t count);
    void draw(BatchId id, size_t count);
    void drawModels(const std::vector<ModelDraw>* draws, const glm::mat4& viewProjection);
    void drawModel(const ModelDraw& draw, const glm::mat4& viewProjection);
    std::vector<GltfNodeDraw> nodeDraws_;  ///< scratch, reused between models
    void drawUi(const FramePacket& frame);
    void bindBoxPipeline();
    void setFrameMatrix(const glm::mat4& vp);

    GLFWwindow* window_ = nullptr;
    Shader* shader_ = nullptr;
    Shader* gltfShader_ = nullptr;
    unsigned int cubeVAO_ = 0;
    unsigned int cubeVBO_ = 0;
    unsigned int frameUBO_ = 0;
    unsigned int textureArray_ = 0;
    unsigned int fontTexture_ = 0;
    Batch batches_[BATCH_COUNT];
    /// staticRevision of the map geometry currently in the WORLD_STATIC VBO.
    std::uint64_t uploadedStaticRevision_ = ~std::uint64_t(0);
};

bool GlRenderBackend::init() {
    shader_ = new Shader("assets/shaders/glow_vertex.glsl",
                         "assets/shaders/glow_fragment.glsl");
    if (!shader_->valid()) {
        std::cerr << "Renderer: instanced shader failed to compile; "
                     "falling back to flat-colour output." << std::endl;
    }

    gltfShader_ = new Shader("assets/shaders/gltf_vertex.glsl",
                             "assets/shaders/gltf_fragment.glsl");
    if (!gltfShader_->valid()) {
        std::cerr << "Renderer: glTF shader failed to compile; "
                     "glTF models will not be drawn." << std::endl;
    }

    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuad), kUnitQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Frame block: view-projection matrix shared by every instance batch.
    glGenBuffers(1, &frameUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, frameUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), nullptr, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    initBuffers();
    return true;
}

void GlRenderBackend::initBuffers() {
    for (int b = 0; b < BATCH_COUNT; ++b) {
        Batch& batch = batches_[b];
        glGenVertexArrays(1, &batch.vao);
        glGenBuffers(1, &batch.vbo);
        glBindVertexArray(batch.vao);

        // Locations 0-1 are the shared unit quad; GL records the buffer bound
        // to each attribute in the VAO, so bind it while setting them up.
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribDivisor(0, 0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));
        glVertexAttribDivisor(1, 0);

        // Locations 2-9 are the per-instance face data.
        glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);

        const GLsizei stride = sizeof(BoxInstance);
        auto instanceAttrib = [&](GLuint loc, GLint size, size_t offset) {
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, size, GL_FLOAT, GL_FALSE, stride,
                                  (void*)offset);
            glVertexAttribDivisor(loc, 1);
        };
        instanceAttrib(2, 3, offsetof(BoxInstance, offset));
        instanceAttrib(3, 3, offsetof(BoxInstance, right));
        instanceAttrib(4, 3, offsetof(BoxInstance, up));
        instanceAttrib(5, 3, offsetof(BoxInstance, normal));
        instanceAttrib(6, 4, offsetof(BoxInstance, color));
        instanceAttrib(7, 4, offsetof(BoxInstance, uv));
        instanceAttrib(8, 4, offsetof(BoxInstance, tint));
        instanceAttrib(9, 4, offsetof(BoxInstance, atlas));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Default state (the frame restores the same state at its end).
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    // Blending stays off for opaque geometry. Leaving it enabled made map pixels
    // disappear at random (roughly half of each frame was dropped).
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// Textures: one texture array holds every map texture + checker layer; the
// glyph atlas is a separate native-resolution texture.
// ---------------------------------------------------------------------------

void GlRenderBackend::uploadFontAtlas() {
    std::vector<unsigned char> glyphs(static_cast<size_t>(font_atlas::kAtlasW) *
                                      font_atlas::kAtlasH * 4);
    font_atlas::buildAtlas(glyphs.data());

    if (fontTexture_ == 0) glGenTextures(1, &fontTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    // Native 1:1 resolution, no scaling or filtering: 3x5 glyph pixels stay
    // square, exactly like the original per-pixel text renderer.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, font_atlas::kAtlasW, font_atlas::kAtlasH,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, glyphs.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlRenderBackend::setTextureArray(const TextureArrayDesc& desc) {
    const int layerCount = static_cast<int>(desc.layers.size());
    if (textureArray_ == 0) glGenTextures(1, &textureArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, desc.width, desc.height, layerCount,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    uploadFontAtlas();

    // One pass over the array: bind it on the sampled unit (texture creation may
    // have left another unit active) and fill every layer.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int layer = 0; layer < layerCount; ++layer) {
        if (!desc.layers[layer]) continue;
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, desc.width, desc.height, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, desc.layers[layer]);
    }

    // The game art is 64x64 pixel textures; nearest keeps it crisp.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void GlRenderBackend::bindBoxPipeline() {
    if (!shader_) return;
    shader_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glActiveTexture(GL_TEXTURE0);
    shader_->setInt("uTextures", 0);
    shader_->setInt("uFont", 1);
}

void GlRenderBackend::setFrameMatrix(const glm::mat4& vp) {
    // Each space has its own view-projection; the UBO is rewritten between passes.
    glBindBuffer(GL_UNIFORM_BUFFER, frameUBO_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(vp));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void GlRenderBackend::upload(BatchId id, const std::vector<BoxInstance>* instances,
                             size_t first, size_t count) {
    if (!instances || count == 0) return;
    Batch& batch = batches_[id];
    glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    if (count > batch.capacity) {
        // Grow with slack so occasional spikes don't reallocate every frame.
        batch.capacity = count * 2;
        glBufferData(GL_ARRAY_BUFFER, batch.capacity * sizeof(BoxInstance),
                     nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(BoxInstance), instances->data() + first);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlRenderBackend::draw(BatchId id, size_t count) {
    if (count == 0) return;
    glBindVertexArray(batches_[id].vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, kQuadVertexCount, static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

void GlRenderBackend::drawModels(const std::vector<ModelDraw>* draws,
                                 const glm::mat4& viewProjection) {
    if (!gltfShader_ || !gltfShader_->valid()) return;
    if (draws) {
        for (const ModelDraw& d : *draws) {
            if (!d.model || !d.instance) continue;
            drawModel(d, viewProjection);
        }
    }
    glBindVertexArray(0);
}

void GlRenderBackend::drawModel(const ModelDraw& d, const glm::mat4& viewProjection) {
    const GltfModel& model = *d.model;
    model.collectNodeDraws(*d.instance, d.transform, nodeDraws_);
    if (!model.isLoaded()) return;
    const GlGltfData& gpu = glGltfData(model);

    Shader& shader = *gltfShader_;
    shader.use();
    shader.setMat4("uViewProjection", glm::value_ptr(viewProjection));
    shader.setVec3("uTint", d.tint);
    shader.setVec3("uLightDir", glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f)));
    shader.setVec3("uLightColor", glm::vec3(0.95f));
    shader.setInt("uBaseColorTexture", 0);
    shader.setInt("uEmissiveTexture", 1);

    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
    const glm::vec4 camera = inverseViewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shader.setVec3("uCameraPos", camera.w != 0.0f ? glm::vec3(camera) / camera.w
                                                  : glm::vec3(0.0f));

    const GltfMaterial fallback = GltfModel::defaultMaterial();
    const GLboolean cullingWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);

    const std::vector<GltfMaterial>& materials = model.materials();
    for (const GltfNodeDraw& node : nodeDraws_) {
        if (node.skinned) {
            shader.setMat4Array("uBones", glm::value_ptr(node.bones[0]), kGltfMaxBones);
            shader.setInt("uHasSkin", 1);
        } else {
            shader.setInt("uHasSkin", 0);
        }
        shader.setMat4("uModel", glm::value_ptr(node.model));

        const auto& primitives = model.meshes()[static_cast<std::size_t>(node.mesh)].primitives;
        const auto& gpuPrimitives = gpu.meshes[static_cast<std::size_t>(node.mesh)];
        for (std::size_t p = 0; p < primitives.size(); ++p) {
            const int index = primitives[p].material;
            const GltfMaterial& material =
                (index >= 0 && static_cast<std::size_t>(index) < materials.size())
                    ? materials[static_cast<std::size_t>(index)] : fallback;

            shader.setVec4("uBaseColorFactor", material.baseColorFactor);
            shader.setVec3("uEmissiveFactor", material.emissiveFactor);
            shader.setFloat("uMetallic", material.metallicFactor);
            shader.setFloat("uRoughness", material.roughnessFactor);
            shader.setInt("uAlphaMode", material.alphaMode);
            shader.setFloat("uAlphaCutoff", material.alphaCutoff);
            shader.setInt("uHasBaseColorTexture", material.hasBaseColorTexture ? 1 : 0);
            shader.setInt("uHasEmissiveTexture", material.hasEmissiveTexture ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gpu.texture(material.baseColorTexture));
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gpu.texture(material.emissiveTexture));
            glActiveTexture(GL_TEXTURE0);

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

            glBindVertexArray(gpuPrimitives[p].vao);
            glDrawElements(GL_TRIANGLES, gpuPrimitives[p].indexCount, GL_UNSIGNED_INT, nullptr);
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

void GlRenderBackend::drawUi(const FramePacket& frame) {
    // Screen space always paints on top of the world, and within the UI the
    // draw order is the answer (there is no depth information in 2D), so the
    // depth test stays off for the whole UI pass.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Each range is a run of consecutive elements: an instanced draw renders in
    // buffer order, so it keeps the painter's order inside the range.
    for (const UiRange& range : *frame.uiRanges) {
        if (range.count == 0) continue;
        upload(UI_SCRATCH, frame.uiInstances, range.first, range.count);
        if (range.blended) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        draw(UI_SCRATCH, range.count);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void GlRenderBackend::renderFrame(const FramePacket& frame) {
    glViewport(frame.viewportX, frame.viewportY, frame.viewportW, frame.viewportH);
    glClearColor(frame.clearColor.r, frame.clearColor.g, frame.clearColor.b, frame.clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!shader_) return;

    bindBoxPipeline();

    // ── 3D: opaque map, then the per-frame world geometry ──
    // Both world streams hold solid geometry: opaque and depth-writing. Only
    // true translucent overlays are blended, on the UI pass.
    const size_t staticCount = frame.worldStatic ? frame.worldStatic->size() : 0;
    if (staticCount > 0 && frame.staticRevision != uploadedStaticRevision_) {
        upload(WORLD_STATIC, frame.worldStatic, 0, staticCount);
        uploadedStaticRevision_ = frame.staticRevision;
    }
    const size_t dynamicCount = frame.worldDynamic ? frame.worldDynamic->size() : 0;
    upload(WORLD_DYNAMIC, frame.worldDynamic, 0, dynamicCount);
    const size_t viewmodelCount = frame.viewmodelBoxes ? frame.viewmodelBoxes->size() : 0;
    upload(VIEWMODEL, frame.viewmodelBoxes, 0, viewmodelCount);
    setFrameMatrix(frame.worldViewProjection);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Box faces are built with a consistent outward winding, so back faces are
    // culled. (UI quads are single-sided planes; culling stays off for them.)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    draw(WORLD_STATIC, staticCount);
    draw(WORLD_DYNAMIC, dynamicCount);

    // glTF models: same view-projection and depth buffer as the boxes.
    drawModels(frame.worldModels, frame.worldViewProjection);
    bindBoxPipeline();

    // ── First-person weapon ──
    // Its own camera space, drawn into a cleared depth buffer so the gun is
    // never swallowed by a wall the camera is close to.
    const bool hasViewmodelModels = frame.viewmodelModels && !frame.viewmodelModels->empty();
    if (viewmodelCount > 0 || hasViewmodelModels) {
        glClear(GL_DEPTH_BUFFER_BIT);
        setFrameMatrix(frame.viewmodelViewProjection);
        draw(VIEWMODEL, viewmodelCount);
        drawModels(frame.viewmodelModels, frame.viewmodelViewProjection);
        bindBoxPipeline();
    }
    glDisable(GL_CULL_FACE);

    // ── 2D: composite the UI in submission order ──
    setFrameMatrix(frame.uiViewProjection);
    glDisable(GL_CULL_FACE);
    if (frame.uiRanges && !frame.uiRanges->empty()) drawUi(frame);

    // Restore a sane default for code that still issues raw GL.
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glUseProgram(0);
}

RenderBackendInfo GlRenderBackend::describe() const {
    auto str = [](GLenum name) {
        const auto* s = reinterpret_cast<const char*>(glGetString(name));
        return std::string(s ? s : "?");
    };
    return RenderBackendInfo{"OpenGL", str(GL_VERSION), str(GL_RENDERER), str(GL_VENDOR)};
}

void GlRenderBackend::shutdown() {
    for (int b = 0; b < BATCH_COUNT; ++b) {
        if (batches_[b].vao) glDeleteVertexArrays(1, &batches_[b].vao);
        if (batches_[b].vbo) glDeleteBuffers(1, &batches_[b].vbo);
        batches_[b] = Batch{};
    }
    if (cubeVAO_) glDeleteVertexArrays(1, &cubeVAO_);
    if (cubeVBO_) glDeleteBuffers(1, &cubeVBO_);
    if (frameUBO_) glDeleteBuffers(1, &frameUBO_);
    if (textureArray_) glDeleteTextures(1, &textureArray_);
    if (fontTexture_) glDeleteTextures(1, &fontTexture_);
    cubeVAO_ = cubeVBO_ = frameUBO_ = textureArray_ = fontTexture_ = 0;
    uploadedStaticRevision_ = ~std::uint64_t(0);
    delete shader_;
    shader_ = nullptr;
    delete gltfShader_;
    gltfShader_ = nullptr;
}

} // namespace

std::unique_ptr<IRenderBackend> createGlRenderBackend(GLFWwindow* window) {
    return std::make_unique<GlRenderBackend>(window);
}
