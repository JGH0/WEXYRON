/**
 * @file renderer.hpp
 * @brief Batched (instanced) OpenGL renderer for the game
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include "shader.hpp"
#include "font_atlas.hpp"

class Map;
class Texture;

/// Alpha-blend mode for submitted instances.
enum class BlendMode : uint8_t {
    Opaque,  ///< depth-tested, writes depth
    Alpha,   ///< depth-tested, blended, no depth write
    Overlay  ///< screen-space UI: no depth test, blended
};

/**
 * @brief Batched renderer.
 *
 * Instead of one draw call per cube (and a uniform lookup per cube), callers
 * *submit* instances. They are grouped into a handful of batches that are
 * drawn with glDrawArraysInstanced:
 *
 *  - World batches are persistent: map geometry is built once (buildWorld) and
 *    drawn every frame without touching the CPU.
 *  - Dynamic world instances (enemies, projectiles, HUD world markers) are
 *    re-uploaded each frame into the tail of the same buffer.
 *  - UI batches are rebuilt each frame and projected with a 2D matrix.
 *
 * GL state (blend, depth) is switched at most a few times per frame: all
 * opaque geometry is queued first, then alpha-blended, then overlays.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    /// Create shaders, cube geometry, instance buffers and the texture array.
    /// Must be called on the GL thread with a current context.
    void init();

    /// Set the viewport in pixels.
    void setViewport(int x, int y, int width, int height);

    // ── Frame setup ──────────────────────────────────────────────────────────

    /// Begin a new frame: clears the screen and resets all per-frame batches.
    void beginFrame();

    /// Set the 3D camera. Instances submitted after this are drawn in 3D.
    void setCamera(const glm::mat4& view, const glm::mat4& projection);

    /// Set a 2D/UI projection (orthographic). Instances submitted after this
    /// are drawn in screen space.
    void setProjection(const glm::mat4& view, const glm::mat4& projection);

    // ── Submission ───────────────────────────────────────────────────────────

    /// Submit an axis-aligned box centred at `center` with size `size`.
    void submitBox(const glm::vec3& center, const glm::vec3& size,
                   const glm::vec3& color, BlendMode mode = BlendMode::Opaque,
                   float alpha = 1.0f);

    /// Submit a box with a yaw/pitch rotation (degrees), matching the old
    /// translate -> rotate X -> rotate Y -> scale convention.
    void submitRotatedBox(const glm::vec3& center, const glm::vec3& size,
                          float yawDeg, float pitchDeg,
                          const glm::vec3& colorOrTint, int textureLayer,
                          float alpha = 1.0f,
                          BlendMode mode = BlendMode::Opaque);

    /// Submit a batched quad in 2D space (screen/UI coordinates).
    void submitQuad(const glm::vec2& center, const glm::vec2& size,
                    const glm::vec3& color, float alpha = 1.0f,
                    BlendMode mode = BlendMode::Overlay);

    /// Submit a full-screen tinted overlay (damage flash, health pulse, ...).
    /// Drawn last, blended, with depth testing off.
    void submitOverlay(const glm::vec3& color, float alpha);

    // ── First-person viewmodel ───────────────────────────────────────────────

    /// Set the camera used for the viewmodel pass. Boxes are submitted in view
    /// space (the game passes an identity view), and this camera may use its
    /// own near plane / FOV independent of the world camera.
    void setViewmodelCamera(const glm::mat4& view, const glm::mat4& projection);

    /// Submit an axis-aligned box in viewmodel (camera) space.
    void submitViewBox(const glm::vec3& center, const glm::vec3& size,
                       const glm::vec3& color);

    /// Submit an oriented box in viewmodel space. `axisX/Y/Z` are the full
    /// edge vectors, so the box can be rotated around any of its own axes.
    void submitViewBoxOriented(const glm::vec3& center,
                               const glm::vec3& axisX, const glm::vec3& axisY,
                               const glm::vec3& axisZ, const glm::vec3& color);

    /// Flush all queued instances to the GPU. Call once at the end of a frame.
    void endFrame();

    // ── Convenience wrappers used by the game code ───────────────────────────

    /// Number of world instances currently uploaded (map geometry).
    size_t worldInstanceCount() const { return worldInstanceCount_; }

    /**
     * @brief Rebuild the static world batch from a map.
     *
     * Safe to call from the main thread whenever the map changes. The work is
     * pure CPU (matrix maths); it uploads once and is drawn thereafter with a
     * single instanced draw call.
     */
    void buildWorld(Map* map);

    // ── Text ─────────────────────────────────────────────────────────────────

    /// Queue a line of 3x5 bitmap text using the built-in font atlas layer.
    void drawText(const std::string& text, float x, float y, float scale);

    /// Draw a line of bitmap text in a colour (glyphs are white in the atlas,
    /// so the colour is the ink).
    void drawTextColored(const std::string& text, float x, float y, float scale,
                         const glm::vec3& color);

    /**
     * @brief Punch a hole in the queued UI geometry (screen-space "scissor").
     *
     * The UI batches are drawn as instanced quads with depth writes disabled
     * and no depth test, so the draw order decides what is visible. To make
     * text readable on top of whatever was queued before it, the text bounds
     * are blanked first; each glyph pixel then paints inside the hole.
     * @param rect x, y, width, height in UI (canvas) coordinates
     */
    void clearRect(float x, float y, float width, float height);

    /// Draw text scaled down to fit `maxWidth`.
    void drawTextFit(const std::string& text, float x, float y,
                     float maxWidth, float scale);
    /// Draw text centred horizontally around `centreX`.
    void drawTextCentered(const std::string& text, float centreX, float y,
                          float scale);
    /**
     * @brief Centred text in a given colour, shrunk to fit `maxWidth`.
     *
     * UI labels are laid out centred inside their button, so they need both a
     * colour (selected/unselected contrast) and a width cap to stay inside it.
     */
    void drawTextCenteredColored(const std::string& text, float centreX, float y,
                                 float scale, const glm::vec3& color,
                                 float maxWidth = 0.0f);
    /// Width in UI units that `text` occupies at `scale`.
    float getTextWidth(const std::string& text, float scale) const;

    // ── Resources ────────────────────────────────────────────────────────────

    /// Rebuild the texture array from a map's textures (call after map load).
    void refreshTextures(Map* map);


    /// @return the texture-array layer for a map texture id, or -1.
    int textureLayer(int textureId) const;

    /// @return the lifetime texture-array layer (garbage/checker).
    int checkerLayer() const { return checkerLayer_; }


    /// Clean up GL resources. Must run on the GL thread.
    void shutdown();

private:
    /// One quad vertex-shader instance: a face of a box (or a UI quad).
    struct InstanceGPU {
        glm::vec3 offset;   ///< face centre
        glm::vec3 right;    ///< half-extent along screen x
        glm::vec3 up;       ///< half-extent along screen y
        glm::vec3 normal;   ///< outward face normal (drives shading)
        glm::vec4 color;    ///< rgb + alpha
        glm::vec4 uv;       ///< tile u0, v0, u1, v1
        glm::vec4 tint;     ///< rgb tint for textured instances
        glm::vec4 atlas;    ///< x = array layer, y = textured flag
    };
    // 12*4 floats for offset/right/up/normal, then colour, uv, tint, atlas.
    static_assert(sizeof(InstanceGPU) == 112, "instance layout must match the vertex attributes");
    static_assert(offsetof(InstanceGPU, color) == 48, "color attribute offset");
    static_assert(offsetof(InstanceGPU, uv) == 64, "uv attribute offset");
    static_assert(offsetof(InstanceGPU, tint) == 80, "tint attribute offset");
    static_assert(offsetof(InstanceGPU, atlas) == 96, "atlas attribute offset");

    /**
     * @brief One instanced draw batch.
     *
     * The first `visible` instances are the ones actually drawn. Instances
     * that land inside a UI clear rect (see clearRect) are moved to the tail,
     * so the GPU can draw [0, visible) with a single glDrawArraysInstanced.
     */
    struct Batch {
        std::vector<InstanceGPU> instances;
        unsigned int vao = 0;
        unsigned int vbo = 0;
        size_t capacity = 0;   ///< instances the VBO can hold
        size_t uploaded = 0;   ///< instances currently in the VBO
        /// The CPU copy changed and must be re-sent before the next draw.
        bool needsUpload = false;
        bool dirty = true;     ///< full (re)allocation needed before drawing
    };

    /// One UI element in submission (painter's) order. UI is composited by
    /// drawing these in order into small consecutive slots, so opaque boxes
    /// hide what is behind them, text sits on top of them, and blended overlays
    /// are applied last over the whole screen.
    struct UiItem {
        InstanceGPU inst;
        bool blended;
        bool covered;   ///< a later text line explicitly cleared this rectangle
    };

    enum BatchId {
        WORLD_OPAQUE = 0,
        WORLD_ALPHA,
        WORLD_VIEWMODEL, ///< held weapon, drawn after the world into cleared depth
        UI_SCRATCH,      ///< rebuilt per UI slot in drawUi()
        BATCH_COUNT
    };

    void initBuffers();
    /// Create/refresh the standalone glyph atlas texture (nearest, 1:1).
    void uploadFontAtlas();
    void uploadBatch(BatchId id);
    void drawBatch(BatchId id);
    /// Composite the queued UI elements in submission order (see UiItem).
    void drawUi();

    /// Whether a UI instance is completely inside UI-rect `r`.
    bool instanceInsideRect(const InstanceGPU& inst, const glm::mat4& invView,
                            const glm::mat4& invProj,
                            float x0, float y0, float x1, float y1) const;
    void applyBlendState(BlendMode mode);
    void pushInstance(BatchId id, const InstanceGPU& inst);

    /// Flat, untextured UI quad (mode 0 in the fragment shader).
    void pushUiFlat(BatchId id, const glm::vec3& centre, const glm::vec3& right,
                    const glm::vec3& up, const glm::vec4& color, float depth = 0.0f);

    /// Append the six faces of an oriented box to a batch.
    void pushBoxFaces(BatchId id, const glm::vec3& centre,
                      const glm::vec3& axisX, const glm::vec3& axisY, const glm::vec3& axisZ,
                      const glm::vec4& color, const glm::vec4& uv,
                      const glm::vec4& tint, const glm::vec4& atlas);
    void pushFace(BatchId id, const glm::vec3& centre2, const glm::vec3& right,
                  const glm::vec3& up, const glm::vec3& normal,
                  const glm::vec4& color, const glm::vec4& uv,
                  const glm::vec4& tint, const glm::vec4& atlas);

public:
    // ── Legacy API (kept so existing call sites still compile) ───────────────
    // These forward to the batched path; new code should use submit*().
    void drawCube(const glm::mat4& model, const glm::vec3& color);
    void drawTexturedCube(const glm::mat4& model, Texture* texture);
    /// Screen-space rectangle from a translate+scale model matrix. `model`'s
    /// scale is the quad's full size (matching the original per-quad renderer).
    void drawColoredQuad(const glm::mat4& model, const glm::vec3& color);
    void renderSpawnPoints(Map* map, bool showSpawnPoints);
    void renderMap(Map* map);
    void clear();
    static glm::mat4 createQuadModel(float x, float y, float w, float h);

private:
    Shader* shader_ = nullptr;

    unsigned int cubeVAO_ = 0;
    unsigned int cubeVBO_ = 0;
    unsigned int frameUBO_ = 0;
    unsigned int textureArray_ = 0;

    Batch batches_[BATCH_COUNT];

    bool inUi_ = false;
    glm::mat4 view_{1.0f};               ///< active (UI while drawing UI)
    glm::mat4 projection_{1.0f};
    glm::mat4 worldView_{1.0f};          ///< last 3D camera view
    glm::mat4 worldProjection_{1.0f};    ///< last 3D camera projection
    glm::mat4 viewmodelView_{1.0f};      ///< camera basis for the held weapon
    glm::mat4 viewmodelProjection_{1.0f};

    std::vector<int> mapTextureLayers_;
    std::vector<std::pair<unsigned int, int>> glTextureLayers_; ///< GL texture id -> array layer
    int checkerLayer_ = -1;
    unsigned int fontTexture_ = 0;   ///< native-resolution 3x5 glyph atlas
    /// Smallest glyph cell size in the atlas; glyphs are drawn from this.
    static constexpr float kFontCellW = static_cast<float>(font_atlas::kCellW);
    static constexpr float kFontCellH = static_cast<float>(font_atlas::kCellH);

    /// UI elements in submission order (see UiItem).
    std::vector<UiItem> uiItems_;
    /// Upper bound on UI draw calls per frame (see endFrame).
    static constexpr int kMaxUiSlots = 12;
    int arrayW_ = 0;
    int arrayH_ = 0;


    // Maps the UI geometry submitted for a frame to the GL pixels it covers, so
    // a caller can test whether a later draw lands inside it (see clearRect).
    float uiOriginX_ = 0.0f, uiOriginY_ = 0.0f;   ///< canvas coords of pixel (0,0)
    float uiPixelX_ = 1.0f, uiPixelY_ = 1.0f;     ///< canvas units per pixel
    int uiPixelsW_ = 800, uiPixelsH_ = 600;       ///< viewport the mapping was made for

    size_t worldInstanceCount_ = 0;
    glm::vec3 lastUiExtent_{800.0f, 600.0f, 1.0f}; ///< ortho canvas size for overlays
    int worldDrawsThisFrame_ = 0;      ///< map batch draws in the current frame
    size_t mapInstancesLastFrame_ = 0; ///< map batch size at frame end
    size_t viewmodelInstancesLastFrame_ = 0; ///< held-weapon faces submitted

public:
    /// How many times the map batch was drawn during the last frame. It must be
    /// exactly 1 (a second draw shows the world twice with different cameras).
    int debugWorldDrawsLastFrame() const { return worldDrawsThisFrame_; }
    /// The map batch instance count at the end of the last frame.
    size_t debugMapInstancesLastFrame() const { return mapInstancesLastFrame_; }
    /// Faces submitted for the first-person weapon in the last frame (0 when
    /// no viewmodel was drawn).
    size_t debugViewmodelInstancesLastFrame() const { return viewmodelInstancesLastFrame_; }
};

#endif // RENDERER_HPP
