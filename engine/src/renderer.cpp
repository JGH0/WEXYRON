/**
 * @file renderer.cpp
 * @brief Batched (instanced) renderer frontend; GPU work is done by an IRenderBackend
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

namespace {

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

// GPU objects belong to the backend and are released in shutdown(), on the
// thread that owns the device.
Renderer::~Renderer() = default;

void Renderer::setBackend(std::unique_ptr<IRenderBackend> backend) {
    backend_ = std::move(backend);
}

void Renderer::init() {
    if (!backend_) backend_ = createGlRenderBackend();
    if (!backend_->init()) {
        std::cerr << "Renderer: backend failed to initialise; frames will be empty." << std::endl;
    }
    refreshTextures(nullptr);
}

// ---------------------------------------------------------------------------
// Textures: one texture array holds every map texture + checker layer
// ---------------------------------------------------------------------------

void Renderer::refreshTextures(Map* map) {
    // Collect sources for the array, in a stable layer order.
    std::vector<const Texture*> sources;
    std::vector<int> mapLayers;
    if (map) {
        mapLayers.assign(8, -1);
        for (int id = 0; id < 8; ++id) {
            Texture* tex = map->getTexture(id);
            if (tex && tex->valid()) {
                mapLayers[id] = static_cast<int>(sources.size());
                sources.push_back(tex);
            }
        }
    }

    // Layer for the built-in checker ("missing texture").
    checkerLayer_ = static_cast<int>(sources.size());
    sources.push_back(nullptr);

    mapTextureLayers_ = mapLayers;

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

    // Upload from the RGBA source pixels each Texture keeps (reading a GPU
    // texture back is slow and crashed inside the Mesa driver).
    TextureArrayDesc desc;
    desc.width = texW;
    desc.height = texH;
    desc.layers.assign(sources.size(), nullptr);
    int expected = 0, uploaded = 0;
    for (size_t layer = 0; layer < sources.size(); ++layer) {
        const Texture* src = sources[layer];
        if (!src) continue;
        ++expected;
        if (src->pixels && src->width == texW && src->height == texH) {
            desc.layers[layer] = src->pixels;
            ++uploaded;
        }
    }
    // The "missing texture" checker always lives on its own layer.
    if (checkerLayer_ >= 0 && checkerLayer_ < static_cast<int>(desc.layers.size())) {
        desc.layers[static_cast<size_t>(checkerLayer_)] = checker.data();
    }
    if (uploaded != expected) {
        std::cerr << "Renderer: uploaded " << uploaded << " of "
                  << expected << " map textures (size or pixel mismatch)"
                  << std::endl;
    }
    if (backend_) backend_->setTextureArray(desc);
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
    ++staticRevision_;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void Renderer::beginFrame() {
    // The per-frame world batch and the UI list are rebuilt from scratch.
    // The screen is cleared by the backend when the frame is drawn (endFrame).
    worldDrawsThisFrame_ = 0;
    uiItems_.clear();
    batches_[WORLD_ALPHA].instances.clear();

    // The held weapon is rebuilt every frame (bob/sway/recoil animate it).
    batches_[WORLD_VIEWMODEL].instances.clear();
    // Boxes submitted while a UI projection is active land here. The UI pass
    // only draws uiItems_ (the old GL path cleared this scratch batch before
    // drawing, so they were never shown); clear it so it cannot grow.
    batches_[UI_SCRATCH].instances.clear();

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
            const int pixelsW = viewportW_ > 0 ? viewportW_ : 800;
            const int pixelsH = viewportH_ > 0 ? viewportH_ : 600;
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
    viewportX_ = x;
    viewportY_ = y;
    viewportW_ = width;
    viewportH_ = height;
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------

void Renderer::pushInstance(BatchId id, const InstanceGPU& inst) {
    batches_[id].instances.push_back(inst);
    // The map batch is persistent: any change must reach the GPU copy.
    if (id == WORLD_OPAQUE) ++staticRevision_;
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

void Renderer::submitQuad(const glm::vec2& center, const glm::vec2& size,
                          const glm::vec3& color, float alpha, BlendMode mode) {
    const BatchId id = UI_SCRATCH;
    pushUiFlat(id, glm::vec3(center, 0.0f),
               glm::vec3(size.x * 0.5f, 0.0f, 0.0f),
               glm::vec3(0.0f, size.y * 0.5f, 0.0f), glm::vec4(color, alpha));
}

void Renderer::buildUiRanges() {
    // Draw order is the submission order, minus elements a later text line
    // covered. Consecutive elements are drawn in small slots: an instanced draw
    // call renders in buffer order, so a slot is exactly a run of consecutive
    // elements. Opaque elements are drawn first (they hide what is behind them),
    // then every blended element in submission order, which makes the
    // damage/flash overlays cover whatever was drawn before them.
    uiDrawList_.clear();
    uiRanges_.clear();
    std::vector<size_t> opaque, blended;
    opaque.reserve(uiItems_.size());
    blended.reserve(uiItems_.size());
    for (size_t i = 0; i < uiItems_.size(); ++i) {
        if (uiItems_[i].covered) continue;
        (uiItems_[i].blended ? blended : opaque).push_back(i);
    }

    auto addRanges = [&](const std::vector<size_t>& list, bool blend) {
        // At most kMaxUiSlots draws per list (one instanced draw each).
        const size_t perSlot = std::max<size_t>(
            1, (list.size() + kMaxUiSlots - 1) / std::max(1, kMaxUiSlots));
        for (size_t k = 0; k < list.size(); k += perSlot) {
            const size_t last = std::min(k + perSlot, list.size());
            UiRange range;
            range.first = uiDrawList_.size();
            range.count = last - k;
            range.blended = blend;
            for (size_t j = k; j < last; ++j) uiDrawList_.push_back(uiItems_[list[j]].inst);
            uiRanges_.push_back(range);
        }
    };
    addRanges(opaque, false);
    addRanges(blended, true);
}

void Renderer::endFrame() {
    if (!backend_) return;

    const Batch& mapBatch = batches_[WORLD_OPAQUE];
    if (!mapBatch.instances.empty()) {
        ++worldDrawsThisFrame_;
        mapInstancesLastFrame_ = mapBatch.instances.size();
    }
    worldModelsLastFrame_ = worldModels_.size();
    const bool hasViewmodel = !batches_[WORLD_VIEWMODEL].instances.empty() || !viewmodelModels_.empty();
    viewmodelInstancesLastFrame_ = hasViewmodel ? batches_[WORLD_VIEWMODEL].instances.size() : 0;
    viewmodelModelsLastFrame_ = hasViewmodel ? viewmodelModels_.size() : 0;

    buildUiRanges();

    // Each space has its own view-projection. UI states only call
    // setProjection(), which must not disturb the 3D camera, so keep them apart.
    FramePacket frame;
    frame.viewportX = viewportX_;
    frame.viewportY = viewportY_;
    frame.viewportW = viewportW_;
    frame.viewportH = viewportH_;
    frame.worldViewProjection = worldProjection_ * worldView_;
    frame.worldStatic = &mapBatch.instances;
    frame.staticRevision = staticRevision_;
    frame.worldDynamic = &batches_[WORLD_ALPHA].instances;
    frame.worldModels = &worldModels_;
    frame.viewmodelViewProjection = viewmodelProjection_ * viewmodelView_;
    frame.viewmodelBoxes = &batches_[WORLD_VIEWMODEL].instances;
    frame.viewmodelModels = &viewmodelModels_;
    frame.uiViewProjection = projection_ * view_;
    frame.uiInstances = &uiDrawList_;
    frame.uiRanges = &uiRanges_;
    backend_->renderFrame(frame);
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

    // Markers are per-frame geometry. They must go into the alpha batch (which
    // beginFrame clears) rather than the persistent opaque world batch: opaque
    // submissions survive until the map is rebuilt, so toggling SPAWNS off used
    // to leave the markers visible and re-added them every frame.
    auto marker = [&](const glm::vec3& centre, const glm::vec3& size,
                      const glm::vec3& color) {
        submitBox(centre, size, color, BlendMode::Alpha, 1.0f);
    };

    for (const auto& spawn : map->getSpawnPoints()) {
        marker(spawn.position, glm::vec3(0.5f), glm::vec3(1.0f, 1.0f, 0.0f));
        const float dirRad = glm::radians(spawn.yaw);
        marker(spawn.position + glm::vec3(std::sin(dirRad) * 0.5f, 0.25f, std::cos(dirRad) * 0.5f),
               glm::vec3(0.15f), glm::vec3(1.0f, 0.8f, 0.0f));
    }
    for (const auto& spawn : map->getTeamSpawns()) {
        const glm::vec3 teamColor = (spawn.teamId == 0) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        marker(spawn.position, glm::vec3(0.5f), teamColor);
        const float dirRad = glm::radians(spawn.yaw);
        marker(spawn.position + glm::vec3(std::sin(dirRad) * 0.5f, 0.25f, std::cos(dirRad) * 0.5f),
               glm::vec3(0.15f), teamColor * 1.3f);
    }
    for (const auto& site : map->getBombSites()) {
        if (!site.active) continue;
        const glm::vec3 color = (site.label == 'A') ? glm::vec3(1.0f, 0.85f, 0.0f)
                                                    : glm::vec3(1.0f, 0.5f, 0.0f);
        marker(site.position + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(1.5f, 0.2f, 1.5f), color);
        for (int h = 1; h <= 4; ++h) {
            const float fade = 1.0f - (h - 1) * 0.15f;
            marker(site.position + glm::vec3(0.0f, h * 0.8f + 0.2f, 0.0f),
                   glm::vec3(0.4f, 0.6f, 0.4f), color * fade);
        }
        marker(site.position + glm::vec3(0.0f, 4.6f, 0.0f), glm::vec3(0.8f, 0.2f, 0.8f), color);
        marker(site.position + glm::vec3(0.0f, 5.2f, 0.0f), glm::vec3(0.5f, 0.2f, 0.5f), color * 0.9f);
        marker(site.position + glm::vec3(0.0f, 5.6f, 0.0f), glm::vec3(0.25f, 0.2f, 0.25f), color * 0.8f);
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
    if (backend_) backend_->shutdown();
    worldModels_.clear();
    viewmodelModels_.clear();
}
