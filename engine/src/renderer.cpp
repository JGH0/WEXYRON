/**
 * @file renderer.cpp
 * @brief Batched (instanced) OpenGL renderer for the game
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "engine/renderer.hpp"
#include "engine/map.hpp"
#include "engine/texture.hpp"
#include "engine/gltf_model.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <glm/gtc/type_ptr.hpp>

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
constexpr int kMaxTextureLayers = 64;

/// One quad face of a box.
struct FaceVertices {
    glm::vec3 right;   ///< half-extent along screen x
    glm::vec3 up;      ///< half-extent along screen y
    glm::vec3 normal;  ///< outward face normal
};


/// Rotation basis for a yaw (Y) then pitch (X) rotation, in the same order the
/// old glm::rotate(1, radians(pitch), X) then rotate(radians(yaw), Y) used.
inline void rotationBasis(float yawDeg, float pitchDeg,
                          glm::vec3& ax, glm::vec3& ay, glm::vec3& az) {
    const float yaw = glm::radians(yawDeg);
    const float pit = glm::radians(pitchDeg);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pit), sp = std::sin(pit);
    // Y * X
    ax = glm::vec3( cy,      0.0f, -sy);
    ay = glm::vec3( sy * sp, cp,    cy * sp);
    az = glm::vec3( sy * cp, -sp,   cy * cp);
}

/// Extracts the rotation basis from a model matrix (legacy draw paths).
inline void basisFromMatrix(const glm::mat4& m, glm::vec3& ax, glm::vec3& ay, glm::vec3& az) {
    ax = glm::vec3(m[0]);
    ay = glm::vec3(m[1]);
    az = glm::vec3(m[2]);
}

/// Extracts per-axis scale from a model matrix (length of each basis vector).
inline glm::vec3 scaleFromMatrix(const glm::mat4& m) {
    return glm::vec3(glm::length(glm::vec3(m[0])),
                     glm::length(glm::vec3(m[1])),
                     glm::length(glm::vec3(m[2])));
}

inline void normalizeBasis(glm::vec3& ax, glm::vec3& ay, glm::vec3& az) {
    const float lx = glm::length(ax), ly = glm::length(ay), lz = glm::length(az);
    ax = lx > 1e-6f ? ax / lx : glm::vec3(1, 0, 0);
    ay = ly > 1e-6f ? ay / ly : glm::vec3(0, 1, 0);
    az = lz > 1e-6f ? az / lz : glm::vec3(0, 0, 1);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Renderer::Renderer() = default;

Renderer::~Renderer() {
    // GL objects are released explicitly in shutdown() (on the GL thread);
    // deleting GL handles from the destructor on another thread is undefined.
    delete shader_;
    delete gltfShader_;
}

void Renderer::init() {
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
    refreshTextures(nullptr);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    // Blending stays off for opaque geometry. Leaving it enabled made map pixels
    // disappear at random (roughly half of each frame was dropped).
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    // Face winding is not reliable across the hand-built map faces, and culling
    // dropped whole faces, so it stays off for the world pass too.
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
}

void Renderer::initBuffers() {
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

        const GLsizei stride = sizeof(InstanceGPU);
        auto instanceAttrib = [&](GLuint loc, GLint size, size_t offset) {
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, size, GL_FLOAT, GL_FALSE, stride,
                                  (void*)offset);
            glVertexAttribDivisor(loc, 1);
        };
        instanceAttrib(2, 3, offsetof(InstanceGPU, offset));
        instanceAttrib(3, 3, offsetof(InstanceGPU, right));
        instanceAttrib(4, 3, offsetof(InstanceGPU, up));
        instanceAttrib(5, 3, offsetof(InstanceGPU, normal));
        instanceAttrib(6, 4, offsetof(InstanceGPU, color));
        instanceAttrib(7, 4, offsetof(InstanceGPU, uv));
        instanceAttrib(8, 4, offsetof(InstanceGPU, tint));
        instanceAttrib(9, 4, offsetof(InstanceGPU, atlas));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

// ---------------------------------------------------------------------------
// Textures: one texture array holds every map texture + font + checker layer
// ---------------------------------------------------------------------------

void Renderer::uploadFontAtlas() {
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

void Renderer::refreshTextures(Map* map) {
    // Collect sources for the array, in a stable layer order.
    std::vector<const Texture*> sources;
    std::vector<int> mapLayers;
    if (map) {
        mapLayers.assign(8, -1);
        for (int id = 0; id < 8; ++id) {
            Texture* tex = map->getTexture(id);
            if (tex && tex->ID) {
                mapLayers[id] = static_cast<int>(sources.size());
                sources.push_back(tex);
            }
        }
    }

    // Layer for the built-in checker ("missing texture").
    checkerLayer_ = static_cast<int>(sources.size());
    sources.push_back(nullptr);

    mapTextureLayers_ = mapLayers;
    glTextureLayers_.clear();
    for (size_t layer = 0; layer < sources.size(); ++layer) {
        if (sources[layer] && sources[layer]->ID)
            glTextureLayers_.emplace_back(sources[layer]->ID, static_cast<int>(layer));
    }

    if (static_cast<int>(sources.size()) > kMaxTextureLayers) {
        std::cerr << "Renderer: too many textures (" << sources.size()
                  << "), the array holds " << kMaxTextureLayers << std::endl;
        sources.resize(kMaxTextureLayers);
    }

    // Assumes all map textures share the same pixel size (they do); the array
    // needs one uniform size. Fall back to 64x64 for the synthetic layers.
    int texW = 64, texH = 64;
    for (const Texture* t : sources) {
        if (t && t->width > 0 && t->height > 0) { texW = t->width; texH = t->height; break; }
    }

    arrayW_ = texW;
    arrayH_ = texH;

    const int layerCount = static_cast<int>(sources.size());
    if (textureArray_ == 0) glGenTextures(1, &textureArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, texW, texH, layerCount,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Synthetic layer built in CPU memory: the "missing texture" checker.
    std::vector<unsigned char> checker(static_cast<size_t>(texW) * texH * 4);
    const int cell = std::max(1, texW / 8);
    for (int y = 0; y < texH; ++y) {
        for (int x = 0; x < texW; ++x) {
            const bool dark = ((x / cell) + (y / cell)) & 1;
            unsigned char* p = &checker[(static_cast<size_t>(y) * texW + x) * 4];
            p[0] = p[1] = p[2] = dark ? 205 : 255;
            p[3] = 255;
        }
    }

    uploadFontAtlas();

    // One pass over the array: bind it on the sampled unit (texture creation may
    // have left another unit active) and fill every layer.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // GL has no plain texture -> array copy, and reading a texture back (FBO or
    // glGetTexImage) is slow and crashed inside the Mesa driver, so upload from
    // the RGBA source pixels each Texture keeps.
    int uploaded = 0;
    for (int layer = 0; layer < layerCount; ++layer) {
        const Texture* src = sources[layer];
        if (src && src->pixels && src->width == texW && src->height == texH) {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, texW, texH, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, src->pixels);
            ++uploaded;
        }
    }

    // The "missing texture" checker always lives on its own layer.
    if (checkerLayer_ >= 0 && checkerLayer_ < layerCount) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, checkerLayer_, texW, texH, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, checker.data());
    }
    if (uploaded != static_cast<int>(glTextureLayers_.size())) {
        std::cerr << "Renderer: uploaded " << uploaded << " of "
                  << glTextureLayers_.size() << " map textures (size or pixel mismatch)"
                  << std::endl;
    }

    // The game art is 64x64 pixel textures; nearest keeps it crisp.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

int Renderer::textureLayer(int textureId) const {
    // -1 means "this slot has no image"; callers fall back to the checker tile.
    if (textureId >= 0 && textureId < static_cast<int>(mapTextureLayers_.size()))
        return mapTextureLayers_[textureId];
    return -1;
}

// ---------------------------------------------------------------------------
// World building
// ---------------------------------------------------------------------------

void Renderer::buildWorld(Map* map) {
    Batch& batch = batches_[WORLD_OPAQUE];
    batch.instances.clear();
    if (map) {
        const auto& blocks = map->getBlocks();
        // Six faces per block. clear() keeps the old capacity, so reserve()
        // is a no-op unless the new map is bigger.
        batch.instances.reserve(blocks.size() * 6);
        for (const auto& block : blocks) {
            glm::vec3 ax, ay, az;
            rotationBasis(block.rotation, block.pitch, ax, ay, az);

            const glm::vec3 ex = ax * block.scale.x;
            const glm::vec3 ey = ay * block.scale.y;
            const glm::vec3 ez = az * block.scale.z;

            const int layer = textureLayer(block.textureId);
            glm::vec4 color, tint, atlas;
            if (layer >= 0) {
                // Textured: sample the material layer, tinted by its colour is
                // not needed (the image already carries the look).
                color = glm::vec4(1.0f);
                tint  = glm::vec4(block.color, 1.0f);   // a=1: lit   // a=1: lit
                atlas = glm::vec4((float)layer, 1.0f, 0.0f, 0.0f);
            } else {
                // Untextured: checker layer multiplied by the block colour.
                color = glm::vec4(1.0f);
                tint  = glm::vec4(block.color, 1.0f);
                atlas = glm::vec4((float)checkerLayer_, 1.0f, 0.0f, 0.0f);
            }
            pushBoxFaces(WORLD_OPAQUE, block.position, ex, ey, ez,
                         color, glm::vec4(0, 0, 1, 1), tint, atlas);
        }
    }
    worldInstanceCount_ = batch.instances.size();

    batch.dirty = true;
    batch.needsUpload = true;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void Renderer::beginFrame() {
    // The per-frame world batch and the UI list are rebuilt from scratch.
    worldDrawsThisFrame_ = 0;
    batches_[WORLD_ALPHA].needsUpload = true;
    uiItems_.clear();
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    batches_[WORLD_ALPHA].instances.clear();
    batches_[WORLD_ALPHA].dirty = true;

    // The held weapon is rebuilt every frame (bob/sway/recoil animate it).
    batches_[WORLD_VIEWMODEL].instances.clear();
    batches_[WORLD_VIEWMODEL].needsUpload = true;
    batches_[WORLD_VIEWMODEL].dirty = true;

    worldModels_.clear();
    viewmodelModels_.clear();

    inUi_ = false;
    setCamera(glm::mat4(1.0f), glm::mat4(1.0f));
}

void Renderer::setCamera(const glm::mat4& view, const glm::mat4& projection) {
    worldView_ = view;
    worldProjection_ = projection;
    view_ = view;
    projection_ = projection;
    inUi_ = false;
}

void Renderer::setProjection(const glm::mat4& view, const glm::mat4& projection) {
    view_ = view;
    projection_ = projection;
    inUi_ = true;

    // Canvas <-> pixel mapping for clearRect(). Canvas (0,0) lands at NDC
    // (n0x, n0y); the projection maps one canvas unit onto (dx, dy) in NDC.
    const glm::mat4 mvp = projection_ * view_;
    const glm::vec4 o = mvp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 ux = mvp * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 uy = mvp * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    if (std::abs(o.w) > 1e-6f && std::abs(ux.w) > 1e-6f && std::abs(uy.w) > 1e-6f) {
        const float n0x = o.x / o.w, n0y = o.y / o.w;
        const float dx = ux.x / ux.w - n0x;
        const float dy = uy.y / uy.w - n0y;
        if (std::abs(dx) > 1e-6f && std::abs(dy) > 1e-6f) {
            // Canvas coordinate of the viewport's k-th pixel centre.
            GLint vp[4] = {0, 0, 800, 600};
            glGetIntegerv(GL_VIEWPORT, vp);
            const int pixelsW = vp[2] > 0 ? vp[2] : 800;
            const int pixelsH = vp[3] > 0 ? vp[3] : 600;
            const float ndcPerPixelX = 2.0f / static_cast<float>(pixelsW);
            const float ndcPerPixelY = 2.0f / static_cast<float>(pixelsH);
            uiPixelX_ = ndcPerPixelX / dx;
            uiPixelY_ = ndcPerPixelY / dy;
            uiOriginX_ = (-1.0f + 0.5f * ndcPerPixelX - n0x) / dx;
            uiOriginY_ = (-1.0f + 0.5f * ndcPerPixelY - n0y) / dy;
            uiPixelsW_ = pixelsW;
            uiPixelsH_ = pixelsH;
        }
    }
    // Recover the ortho canvas extent from the projection so overlays can cover
    // it without every caller recomputing screen bounds.
    // An ortho matrix stores x = 2/(r-l) and y = 2/(t-b); the UI projection is
    // Y-flipped (glm::ortho(bottom, top) with top > bottom), which makes
    // m[1][1] negative. Only the magnitude is the canvas size — using the signed
    // value built overlays with a negative height (inverted geometry).
    const glm::mat4& p = projection;
    if (std::abs(p[0][0]) > 1e-6f && std::abs(p[1][1]) > 1e-6f) {
        lastUiExtent_ = glm::vec3(std::abs(2.0f / p[0][0]),
                                  std::abs(2.0f / p[1][1]), 1.0f);
    }
}

void Renderer::setViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------

void Renderer::pushInstance(BatchId id, const InstanceGPU& inst) {
    Batch& batch = batches_[id];
    InstanceGPU copy = inst;
    batch.instances.push_back(copy);
    batch.needsUpload = true;
}

// ---------------------------------------------------------------------------
// Box submission: expand one box into its six faces
// ---------------------------------------------------------------------------

void Renderer::pushFace(BatchId id, const glm::vec3& centre, const glm::vec3& right,
                        const glm::vec3& up, const glm::vec3& normal,
                        const glm::vec4& color, const glm::vec4& uv,
                        const glm::vec4& tint, const glm::vec4& atlas) {
    // The shared quad is emitted as two CCW triangles over (right, up), i.e. its
    // geometric normal is cross(right, up). Back-face culling needs that to point
    // the same way as the face's outward normal, otherwise the face vanishes.
    // Mirror `right` when they disagree; for flat UI quads (normal +Z) this is a
    // no-op. Doing it here keeps every face's winding correct by construction.
    glm::vec3 rightAxis = right;
    if (glm::dot(glm::cross(right, up), normal) < 0.0f) rightAxis = -right;

    InstanceGPU inst{};
    inst.offset = centre;
    inst.right  = rightAxis;
    inst.up     = up;
    inst.normal = normal;
    inst.color  = color;
    inst.uv     = uv;
    inst.tint   = tint;
    inst.atlas  = atlas;
    pushInstance(id, inst);
}

void Renderer::pushBoxFaces(BatchId id, const glm::vec3& centre,
                            const glm::vec3& axisX, const glm::vec3& axisY, const glm::vec3& axisZ,
                            const glm::vec4& color, const glm::vec4& uv,
                            const glm::vec4& tint, const glm::vec4& atlas) {
    // axisX/Y/Z are the full-size axes; pushFace takes half-extents. Mirroring
    // the u axis on the negative faces keeps every face's triangle winding
    // facing outwards.
    const glm::vec3 ex = axisX * 0.5f;
    const glm::vec3 ey = axisY * 0.5f;
    const glm::vec3 ez = axisZ * 0.5f;

    // Callers legitimately pass a degenerate axis for flat geometry (e.g. the
    // ground quad of a bomb site). normalize() of that is NaN, which would wipe
    // out the whole world pass on one bad batch, so substitute a unit axis.
    auto unit = [](const glm::vec3& v, const glm::vec3& fallback) {
        const float len = glm::length(v);
        return len > 1e-6f ? v / len : fallback;
    };

    // Build the faces of the oriented box explicitly: for each local axis, the
    // other two axes span the face.
    const glm::vec3 dirX = unit(axisX, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 dirY = unit(axisY, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 dirZ = unit(axisZ, glm::vec3(0.0f, 0.0f, 1.0f));

    // +Z / -Z faces span X and Y.
    pushFace(id, centre + ez,  ex, ey,  dirZ, color, uv, tint, atlas);
    pushFace(id, centre - ez, -ex, ey, -dirZ, color, uv, tint, atlas);
    // +X / -X faces span Z and Y.
    pushFace(id, centre + ex,  ez, ey,  dirX, color, uv, tint, atlas);
    pushFace(id, centre - ex, -ez, ey, -dirX, color, uv, tint, atlas);
    // +Y / -Y faces span X and Z.
    pushFace(id, centre + ey,  ex, ez,  dirY, color, uv, tint, atlas);
    pushFace(id, centre - ey,  ex, ez, -dirY, color, uv, tint, atlas);
}

void Renderer::submitBox(const glm::vec3& center, const glm::vec3& size,
                         const glm::vec3& color, BlendMode mode, float alpha) {
    const BatchId id = inUi_ ? UI_SCRATCH
        : (mode == BlendMode::Opaque ? WORLD_OPAQUE : WORLD_ALPHA);
    pushBoxFaces(id, center,
                 glm::vec3(size.x, 0, 0), glm::vec3(0, size.y, 0), glm::vec3(0, 0, size.z),
                 glm::vec4(color, alpha), glm::vec4(0, 0, 1, 1),
                 glm::vec4(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
}

void Renderer::submitRotatedBox(const glm::vec3& center, const glm::vec3& size,
                                float yawDeg, float pitchDeg,
                                const glm::vec3& colorOrTint, int textureLayerId,
                                float alpha, BlendMode mode) {
    glm::vec3 ax, ay, az;
    rotationBasis(yawDeg, pitchDeg, ax, ay, az);
    const glm::vec3 ex = ax * size.x, ey = ay * size.y, ez = az * size.z;
    const BatchId id = mode == BlendMode::Opaque ? WORLD_OPAQUE : WORLD_ALPHA;

    if (textureLayerId >= 0) {
        pushBoxFaces(id, center, ex, ey, ez,
                     glm::vec4(1.0f, 1.0f, 1.0f, alpha), glm::vec4(0, 0, 1, 1),
                     glm::vec4(colorOrTint, 1.0f), glm::vec4((float)textureLayerId, 1, 0, 0));
    } else {
        pushBoxFaces(id, center, ex, ey, ez,
                     glm::vec4(colorOrTint, alpha), glm::vec4(0, 0, 1, 1),
                     glm::vec4(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
}

void Renderer::submitOverlay(const glm::vec3& color, float alpha) {
    // One quad covering the whole visible UI canvas. The bounds come from the
    // canvas <-> pixel mapping computed by setProjection(): the canvas can be
    // larger than the 800x600 design area (wide windows expand it), and an
    // overlay built from the design size alone covered only part of the screen.
    const float pad = std::abs(uiPixelX_) * 0.5f;
    const float x0 = uiOriginX_;
    const float y0 = uiOriginY_;
    const float x1 = uiOriginX_ + static_cast<float>(uiPixelsW_) * uiPixelX_;
    const float y1 = uiOriginY_ + static_cast<float>(uiPixelsH_) * uiPixelY_;
    const glm::vec2 centre((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    const glm::vec2 half(std::abs(x1 - x0) * 0.5f + pad, std::abs(y1 - y0) * 0.5f + pad);

    pushUiFlat(UI_SCRATCH, glm::vec3(centre, 0.0f),
               glm::vec3(half.x, 0.0f, 0.0f),
               glm::vec3(0.0f, half.y, 0.0f), glm::vec4(color, alpha));
}

void Renderer::setViewmodelCamera(const glm::mat4& view, const glm::mat4& projection) {
    viewmodelView_ = view;
    viewmodelProjection_ = projection;
}

void Renderer::submitViewBox(const glm::vec3& center, const glm::vec3& size,
                             const glm::vec3& color) {
    submitViewBoxOriented(center,
                          glm::vec3(size.x, 0.0f, 0.0f),
                          glm::vec3(0.0f, size.y, 0.0f),
                          glm::vec3(0.0f, 0.0f, size.z),
                          color);
}

void Renderer::submitViewBoxOriented(const glm::vec3& center,
                                     const glm::vec3& axisX, const glm::vec3& axisY,
                                     const glm::vec3& axisZ, const glm::vec3& color) {
    pushBoxFaces(WORLD_VIEWMODEL, center, axisX, axisY, axisZ,
                 glm::vec4(color, 1.0f), glm::vec4(0, 0, 1, 1),
                 glm::vec4(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
}

void Renderer::submitModel(GltfModel* model, GltfInstance* instance,
                           const glm::mat4& transform, const glm::vec3& tint) {
    if (!model || !instance) return;
    worldModels_.push_back(ModelDraw{model, instance, transform, tint});
}

void Renderer::submitViewModelModel(GltfModel* model, GltfInstance* instance,
                                    const glm::mat4& transform, const glm::vec3& tint) {
    if (!model || !instance) return;
    viewmodelModels_.push_back(ModelDraw{model, instance, transform, tint});
}

void Renderer::bindBoxPipeline() {
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

void Renderer::drawModels(const std::vector<ModelDraw>& draws,
                          const glm::mat4& viewProjection) {
    if (!gltfShader_ || !gltfShader_->valid()) return;
    for (const ModelDraw& draw : draws) {
        if (!draw.model || !draw.instance) continue;
        draw.model->draw(*draw.instance, *gltfShader_, draw.transform,
                         draw.tint, viewProjection);
    }
    glBindVertexArray(0);
}

void Renderer::submitQuad(const glm::vec2& center, const glm::vec2& size,
                          const glm::vec3& color, float alpha, BlendMode mode) {
    const BatchId id = UI_SCRATCH;
    pushUiFlat(id, glm::vec3(center, 0.0f),
               glm::vec3(size.x * 0.5f, 0.0f, 0.0f),
               glm::vec3(0.0f, size.y * 0.5f, 0.0f), glm::vec4(color, alpha));
}

void Renderer::applyBlendState(BlendMode mode) {
    switch (mode) {
        case BlendMode::Opaque:
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Overlay:
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void Renderer::uploadBatch(BatchId id) {
    Batch& batch = batches_[id];
    if (batch.instances.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    const size_t bytes = batch.instances.size() * sizeof(InstanceGPU);
    if (batch.instances.size() > batch.capacity) {
        // Grow with slack so occasional spikes don't reallocate every frame.
        batch.capacity = batch.instances.size() * 2;
        glBufferData(GL_ARRAY_BUFFER, batch.capacity * sizeof(InstanceGPU),
                     nullptr, GL_STREAM_DRAW);
    }
    if (id != WORLD_OPAQUE || batch.dirty || batch.needsUpload) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, batch.instances.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    batch.uploaded = batch.instances.size();
    batch.needsUpload = false;
    batch.dirty = false;
}

void Renderer::drawBatch(BatchId id) {
    Batch& batch = batches_[id];
    if (batch.instances.empty()) return;
    if (id == WORLD_OPAQUE) {
        ++worldDrawsThisFrame_;
        mapInstancesLastFrame_ = batch.instances.size();
    }
    glBindVertexArray(batch.vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, kQuadVertexCount,
                          static_cast<GLsizei>(batch.instances.size()));
    glBindVertexArray(0);
}

void Renderer::drawUi() {
    // Draw order is the submission order, minus elements a later text line
    // covered. Consecutive elements are drawn in small slots: an instanced draw
    // call renders in buffer order, so a slot is exactly a run of consecutive
    // elements. Opaque elements are drawn first (they hide what is behind them),
    // then every blended element in submission order, which makes the
    // damage/flash overlays cover whatever was drawn before them.
    std::vector<size_t> opaque, blended;
    opaque.reserve(uiItems_.size());
    blended.reserve(uiItems_.size());
    for (size_t i = 0; i < uiItems_.size(); ++i) {
        if (uiItems_[i].covered) continue;
        (uiItems_[i].blended ? blended : opaque).push_back(i);
    }
    if (opaque.empty() && blended.empty()) return;

    auto drawRange = [&](const std::vector<size_t>& list, size_t first, size_t last, bool blend) {
        if (last <= first) return;
        Batch& batch = batches_[UI_SCRATCH];
        batch.instances.clear();
        batch.instances.reserve(last - first);
        for (size_t k = first; k < last; ++k) batch.instances.push_back(uiItems_[list[k]].inst);
        batch.needsUpload = true;
        uploadBatch(UI_SCRATCH);

        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        glBindVertexArray(batch.vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, kQuadVertexCount,
                              static_cast<GLsizei>(batch.instances.size()));
        glBindVertexArray(0);
    };

    // Screen space always paints on top of the world, and within the UI the
    // draw order is the answer (there is no depth information in 2D), so the
    // depth test stays off for the whole UI pass. With it on, the boxed menu
    // panels (drawn on the same plane as the background) failed the depth test
    // and only the glyph pixels survived.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Opaque elements, in order, in up to kMaxUiSlots draws.
    const size_t perSlot = std::max<size_t>(
        1, (opaque.size() + kMaxUiSlots - 1) / std::max(1, kMaxUiSlots));
    for (size_t k = 0; k < opaque.size(); k += perSlot) {
        drawRange(opaque, k, std::min(k + perSlot, opaque.size()), false);
    }

    // Then everything translucent (damage flash, low-health pulse, fade-outs).
    const size_t perBlendSlot = std::max<size_t>(
        1, (blended.size() + kMaxUiSlots - 1) / std::max(1, kMaxUiSlots));
    for (size_t k = 0; k < blended.size(); k += perBlendSlot) {
        drawRange(blended, k, std::min(k + perBlendSlot, blended.size()), true);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::endFrame() {
    if (!shader_) return;

    // The frame UBO is updated between draw passes, so its contents must be
    // written back from the buffer that was actually bound (see setFrameMatrix).
    bindBoxPipeline();

    // Each space has its own view-projection. UI states only call
    // setProjection(), which must not disturb the 3D camera, so keep them apart.
    auto setFrameMatrix = [&](const glm::mat4& vp) {
        glBindBuffer(GL_UNIFORM_BUFFER, frameUBO_);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(vp));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    };

    // ── 3D: opaque, then alpha ──
    // Both world batches hold solid geometry (map, enemies, bomb, projectiles).
    // They used to be drawn with blending on and depth writes off, which made
    // every object see-through: its back faces painted over the ones in front.
    // Solid objects are opaque and depth-writing; only true translucent
    // overlays (the damage/flash screens) are blended afterwards on the UI pass.
    uploadBatch(WORLD_OPAQUE);
    uploadBatch(WORLD_ALPHA);
    uploadBatch(WORLD_VIEWMODEL);
    setFrameMatrix(worldProjection_ * worldView_);

    applyBlendState(BlendMode::Opaque);
    // Box faces are built with a consistent outward winding, so the back faces of
    // every block, enemy and prop are culled instead of being shaded and
    // depth-tested only to lose. (Screen-space UI quads are single-sided planes
    // whose winding depends on their data, so culling stays off for the UI pass.)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    drawBatch(WORLD_OPAQUE);
    drawBatch(WORLD_ALPHA);

    // glTF models: drawn after the box batches, with the same world
    // view-projection and depth buffer so they occlude/interleave correctly.
    worldModelsLastFrame_ = worldModels_.size();
    drawModels(worldModels_, worldProjection_ * worldView_);
    bindBoxPipeline();

    // ── First-person weapon ──
    // Its camera space is independent of where the player stands, and the depth
    // buffer is cleared first so the gun is always visible instead of being
    // swallowed by a wall the camera is close to (the near plane would also cut
    // it in half). Nothing else is drawn after this point, so clearing depth is
    // safe.
    viewmodelInstancesLastFrame_ = 0;
    viewmodelModelsLastFrame_ = 0;
    if (!batches_[WORLD_VIEWMODEL].instances.empty() || !viewmodelModels_.empty()) {
        viewmodelInstancesLastFrame_ = batches_[WORLD_VIEWMODEL].instances.size();
        viewmodelModelsLastFrame_ = viewmodelModels_.size();
        glClear(GL_DEPTH_BUFFER_BIT);
        setFrameMatrix(viewmodelProjection_ * viewmodelView_);
        drawBatch(WORLD_VIEWMODEL);
        drawModels(viewmodelModels_, viewmodelProjection_ * viewmodelView_);
        bindBoxPipeline();
    }
    glDisable(GL_CULL_FACE);

    // ── 2D: composite the UI in submission order ──
    // One instanced batch cannot express painter's order for opaque *and*
    // blended quads (that would need per-quad sorting plus flush-on-blend), so
    // the ordered list is cut into a few consecutive slots and drawn as a few
    // instanced calls. Opaque slots first (they hide what is behind them),
    // then every blended element, so damage/flash overlays cover the whole
    // screen regardless of draw order.
    setFrameMatrix(projection_ * view_);

    // Screen-space quads are single-sided planes: their winding depends on the
    // data, not on a consistent triangle order, so culling would drop them.
    glDisable(GL_CULL_FACE);
    if (!uiItems_.empty()) drawUi();

    // Restore a sane default for code that still issues raw GL.
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

float Renderer::getTextWidth(const std::string& text, float scale) const {
    // Same metrics as the original per-pixel implementation.
    const float pixelSize   = 8.0f * scale;
    const float charWidth   = pixelSize * 4.0f;
    const float charSpacing = pixelSize * 2.0f;
    if (text.empty()) return 0.0f;
    return static_cast<float>(text.size()) * (charWidth + charSpacing) - charSpacing;
}

void Renderer::drawText(const std::string& text, float x, float y, float scale) {
    drawTextColored(text, x, y, scale, glm::vec3(1.0f));
}

void Renderer::drawTextColored(const std::string& text, float x, float y, float scale,
                               const glm::vec3& color) {
    if (text.empty()) return;

    // Original metrics: 8px glyph pixels, 4px advance, 2px spacing.
    const float pixelSize   = 8.0f * scale;
    const float charWidth   = pixelSize * 4.0f;
    const float charSpacing = pixelSize * 2.0f;

    // Blank whatever is behind this line first; the glyphs are emitted after it.
    clearRect(x, y, getTextWidth(text, scale), pixelSize * static_cast<float>(font_atlas::kCellH));

    // Each lit pixel of the 3x5 glyph becomes one solid quad, like the original
    // renderer. Pixels are laid out edge to edge (no gutter): shrinking them by
    // a unit merged neighbouring columns into blobs at small scales.
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ' ') continue;

        const uint16_t pattern = font_atlas::glyphPattern(c);
        const float charX = x + static_cast<float>(i) * (charWidth + charSpacing);

        for (int row = 0; row < font_atlas::kCellH; ++row) {
            for (int col = 0; col < font_atlas::kCellW; ++col) {
                if (!(pattern & (1 << (14 - (row * font_atlas::kCellW + col))))) continue;
                const float cx = charX + static_cast<float>(col) * pixelSize + pixelSize * 0.5f;
                const float cy = y + static_cast<float>(row) * pixelSize + pixelSize * 0.5f;
                pushUiFlat(UI_SCRATCH,
                           glm::vec3(cx, cy, 0.0f),
                           glm::vec3(pixelSize * 0.5f, 0.0f, 0.0f),
                           glm::vec3(0.0f, pixelSize * 0.5f, 0.0f),
                           glm::vec4(color, 1.0f), 0.0f);
            }
        }
    }
}

void Renderer::clearRect(float x, float y, float width, float height) {
    // Blank the UI queued inside this canvas rectangle: a text line calls this
    // before it emits its glyphs, so its background disappears and the glyphs
    // paint on whatever is behind. Elements are stored in canvas coordinates,
    // so their screen bounds are their extents.
    if (width <= 0.0f || height <= 0.0f) return;

    const float x0 = std::min(x, x + width);
    const float x1 = std::max(x, x + width);
    const float y0 = std::min(y, y + height);
    const float y1 = std::max(y, y + height);

    for (UiItem& item : uiItems_) {
        if (item.covered) continue;
        const InstanceGPU& inst = item.inst;
        const glm::vec2 lo(inst.offset.x - std::abs(inst.right.x),
                           inst.offset.y - std::abs(inst.up.y));
        const glm::vec2 hi(inst.offset.x + std::abs(inst.right.x),
                           inst.offset.y + std::abs(inst.up.y));

        // Only elements fully inside the cleared rectangle are removed (a panel
        // exactly under a label). Anything that merely overlaps - neighbouring
        // buttons, long labels - stays.
        if (lo.x >= x0 && hi.x <= x1 && lo.y >= y0 && hi.y <= y1) item.covered = true;
    }
}

void Renderer::drawTextFit(const std::string& text, float x, float y,
                           float maxWidth, float scale) {
    if (text.empty()) return;
    const float needed = getTextWidth(text, scale);
    if (needed > maxWidth && needed > 0.0f) scale *= maxWidth / needed;
    drawText(text, x, y, scale);
}

void Renderer::drawTextCentered(const std::string& text, float centreX, float y,
                                float scale) {
    if (text.empty()) return;
    const float w = getTextWidth(text, scale);
    drawText(text, centreX - w * 0.5f, y, scale);
}

void Renderer::drawTextCenteredColored(const std::string& text, float centreX, float y,
                                       float scale, const glm::vec3& color,
                                       float maxWidth) {
    if (text.empty()) return;
    if (maxWidth > 0.0f) {
        const float needed = getTextWidth(text, scale);
        if (needed > maxWidth) scale *= maxWidth / needed;
    }
    const float w = getTextWidth(text, scale);
    drawTextColored(text, centreX - w * 0.5f, y, scale, color);
}

// ---------------------------------------------------------------------------
// Legacy API: accept a full model matrix and forward to the batched path, so
// existing call sites keep working while every draw is still instanced.
// ---------------------------------------------------------------------------

void Renderer::drawCube(const glm::mat4& model, const glm::vec3& color) {
    // Extract the oriented axes and full sizes straight from the model matrix.
    const glm::vec3 ax = glm::vec3(model[0]);
    const glm::vec3 ay = glm::vec3(model[1]);
    const glm::vec3 az = glm::vec3(model[2]);
    const glm::vec3 centre = glm::vec3(model[3]);
    // A cube stays a cube in both spaces: UI code draws flat, thin cubes
    // (thickness 1 along Z) and expects their front face to show.
    // Dynamic world objects (enemies, projectiles, FX) go to the per-frame
    // batch: appending them to WORLD_OPAQUE used to grow the "static" map
    // buffer every frame and re-upload over the map geometry.
    const BatchId id = inUi_ ? UI_SCRATCH : WORLD_ALPHA;
    pushBoxFaces(id, centre, ax, ay, az,
                 glm::vec4(color, 1.0f), glm::vec4(0, 0, 1, 1),
                 glm::vec4(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
}

void Renderer::drawTexturedCube(const glm::mat4& model, Texture* texture) {
    if (!texture) return;
    int layer = -1;
    for (const auto& entry : glTextureLayers_) {
        if (entry.first == texture->ID) { layer = entry.second; break; }
    }
    if (layer < 0) return;  // not part of the shared array: cannot sample it

    const glm::vec3 ax = glm::vec3(model[0]);
    const glm::vec3 ay = glm::vec3(model[1]);
    const glm::vec3 az = glm::vec3(model[2]);
    pushBoxFaces(WORLD_OPAQUE, glm::vec3(model[3]), ax, ay, az,
                 glm::vec4(1.0f), glm::vec4(0, 0, 1, 1),
                 glm::vec4(1.0f), glm::vec4((float)layer, 1, 0, 0));
}

void Renderer::pushUiFlat(BatchId id, const glm::vec3& centre, const glm::vec3& right,
                          const glm::vec3& up, const glm::vec4& color, float depth) {
    // UI is composited in submission order by endFrame() (see UiItem): there is
    // no separate opaque/alpha batch to sort into, because paint order is what
    // makes boxes hide what is behind them and text sit on top of boxes.
    (void)id;
    (void)depth;

    InstanceGPU inst{};
    inst.offset = centre;
    inst.right  = right;
    inst.up     = up;
    inst.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    inst.color  = color;
    inst.uv     = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    inst.tint   = glm::vec4(1.0f);                    // unlit
    inst.atlas  = glm::vec4(0.0f);                    // flat colour (mode 0)

    UiItem item;
    item.inst = inst;
    // Only genuinely translucent elements go to the blended pass. A margin keeps
    // float rounding from classifying an opaque panel as translucent.
    item.blended = color.a < 0.998f;
    item.covered = false;
    uiItems_.push_back(item);
}

void Renderer::drawColoredQuad(const glm::mat4& model, const glm::vec3& color) {
    const glm::vec3 ax = glm::vec3(model[0]);
    const glm::vec3 ay = glm::vec3(model[1]);
    const glm::vec3 centre = glm::vec3(model[3]);
    // Solid colour: the old 2D path derived alpha from the brightest channel,
    // which made every menu button translucent (0.8) so the background and the
    // labels of other rows showed through it.
    const float alpha = 1.0f;

    // Callers scale the model to the quad's *full* size (the menu builds
    // translate + scale(menuWidth, menuHeight, 1)). pushUiFlat takes half
    // extents: the vertex shader maps the unit quad (-0.5..0.5) to +/-right and
    // +/-up, so passing the full-scale axes here drew every rectangle twice its
    // requested size.
    pushUiFlat(UI_SCRATCH, centre, ax * 0.5f, ay * 0.5f, glm::vec4(color, alpha));
}

void Renderer::renderMap(Map* map) {
    // Rebuild the persistent world batch when the map instance changes.
    static Map* lastMap = nullptr;
    if (map != lastMap) {
        buildWorld(map);
        lastMap = map;
    }
}

void Renderer::renderSpawnPoints(Map* map, bool showSpawnPoints) {
    if (!map || !showSpawnPoints) return;

    // These are world-space markers: submit them back to the 3D camera if a UI
    // projection is still active from the previous frame.
    if (inUi_) setCamera(worldView_, worldProjection_);

    for (const auto& spawn : map->getSpawnPoints()) {
        submitBox(spawn.position, glm::vec3(0.5f), glm::vec3(1.0f, 1.0f, 0.0f));
        const float dirRad = glm::radians(spawn.yaw);
        submitBox(spawn.position + glm::vec3(std::sin(dirRad) * 0.5f, 0.25f, std::cos(dirRad) * 0.5f),
                  glm::vec3(0.15f), glm::vec3(1.0f, 0.8f, 0.0f));
    }
    for (const auto& spawn : map->getTeamSpawns()) {
        const glm::vec3 teamColor = (spawn.teamId == 0) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        submitBox(spawn.position, glm::vec3(0.5f), teamColor);
        const float dirRad = glm::radians(spawn.yaw);
        submitBox(spawn.position + glm::vec3(std::sin(dirRad) * 0.5f, 0.25f, std::cos(dirRad) * 0.5f),
                  glm::vec3(0.15f), teamColor * 1.3f);
    }
    for (const auto& site : map->getBombSites()) {
        if (!site.active) continue;
        const glm::vec3 color = (site.label == 'A') ? glm::vec3(1.0f, 0.85f, 0.0f)
                                                    : glm::vec3(1.0f, 0.5f, 0.0f);
        submitBox(site.position + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(1.5f, 0.2f, 1.5f), color);
        for (int h = 1; h <= 4; ++h) {
            const float fade = 1.0f - (h - 1) * 0.15f;
            submitBox(site.position + glm::vec3(0.0f, h * 0.8f + 0.2f, 0.0f),
                      glm::vec3(0.4f, 0.6f, 0.4f), color * fade);
        }
        submitBox(site.position + glm::vec3(0.0f, 4.6f, 0.0f), glm::vec3(0.8f, 0.2f, 0.8f), color);
        submitBox(site.position + glm::vec3(0.0f, 5.2f, 0.0f), glm::vec3(0.5f, 0.2f, 0.5f), color * 0.9f);
        submitBox(site.position + glm::vec3(0.0f, 5.6f, 0.0f), glm::vec3(0.25f, 0.2f, 0.25f), color * 0.8f);
    }
}

void Renderer::clear() {
    beginFrame();
}

glm::mat4 Renderer::createQuadModel(float x, float y, float w, float h) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));
    return model;
}

void Renderer::shutdown() {
    for (int b = 0; b < BATCH_COUNT; ++b) {
        if (batches_[b].vao) glDeleteVertexArrays(1, &batches_[b].vao);
        if (batches_[b].vbo) glDeleteBuffers(1, &batches_[b].vbo);
    }
    if (cubeVAO_) glDeleteVertexArrays(1, &cubeVAO_);
    if (cubeVBO_) glDeleteBuffers(1, &cubeVBO_);
    if (frameUBO_) glDeleteBuffers(1, &frameUBO_);
    if (textureArray_) glDeleteTextures(1, &textureArray_);
    if (fontTexture_) glDeleteTextures(1, &fontTexture_);
    delete shader_;
    shader_ = nullptr;
    delete gltfShader_;
    gltfShader_ = nullptr;
    worldModels_.clear();
    viewmodelModels_.clear();
}
