/**
 * @file renderer.hpp
 * @brief Batched (instanced) renderer frontend for the game
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <cstdint>
#include <cstddef>
#include "font_atlas.hpp"
#include "render_backend.hpp"

class Map;
class Texture;
struct GLFWwindow;
class GltfModel;
class GltfInstance;

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
 *
 * The Renderer is API-neutral: it only builds CPU-side batches. endFrame()
 * hands them to an IRenderBackend (render_backend.hpp), which owns every GPU
 * object; OpenGL is the default backend.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    /// Create the backend's pipelines, buffers and the texture array. Without
    /// an explicit backend (setBackend) the OpenGL one is used for `window`,
    /// which must come from createRenderWindow(RenderApi::OpenGL, ...).
    void init(GLFWwindow* window);
    /// Use `backend` instead of the default OpenGL backend. Call before init().
    /// Pass initialized = true when backend->init() already succeeded.
    void setBackend(std::unique_ptr<IRenderBackend> backend, bool initialized = false);
    /// The active backend (nullptr before init()).
    IRenderBackend* backend() const { return backend_.get(); }

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

    // ── glTF models ──────────────────────────────────────────────────────────

    /// Queue a glTF model instance for the 3D world pass. The instance pose must
    /// already have been updated (see GltfInstance::update()).
    void submitModel(GltfModel* model, GltfInstance* instance,
                     const glm::mat4& transform,
                     const glm::vec3& tint = glm::vec3(1.0f));

    /// Queue a glTF model for the first-person viewmodel pass (drawn into a
    /// cleared depth buffer, in viewmodel camera space).
    void submitViewModelModel(GltfModel* model, GltfInstance* instance,
                              const glm::mat4& transform,
                              const glm::vec3& tint = glm::vec3(1.0f));

    /// Start of a frame, before input and the game update (latency markers,
    /// low-latency sleep). Optional: render-only loops may skip it.
    void beginSimulation() { if (backend_) backend_->beginSimulation(); }
    /// Flush all queued instances to the GPU. Call once at the end of a frame.
    void endFrame();
    /// Show the finished frame (after endFrame).
    void present();
    /// Vsync preference; kept until the backend exists, then applied.
    void setVsync(bool enabled);
    /// Frame generation request; kept until the backend exists, then applied.
    void setFrameGeneration(const FrameGenSettings& settings);
    FrameGenStatus frameGenerationStatus() const;
    /// The camera jumps this frame (map change, respawn, teleport): motion
    /// vectors and frame generation must not blend across it. Switching from a
    /// menu to 3D is detected automatically.
    void markCameraCut() { cameraCutPending_ = true; }
    /// Read back the next frame (tests/screenshots); fetch it with takeCapture().
    void requestCapture() { if (backend_) backend_->requestCapture(); }
    bool takeCapture(CapturedImage& out) { return backend_ && backend_->takeCapture(out); }

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
    using InstanceGPU = BoxInstance;

    /// CPU side of one instanced draw stream.
    struct Batch {
        std::vector<InstanceGPU> instances;
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
        UI_SCRATCH,      ///< UI submissions (collected in uiItems_)
        BATCH_COUNT
    };

    /// Order the queued UI elements into draw ranges (see UiItem).
    void buildUiRanges();
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
    /// Screen-space rectangle from a translate+scale model matrix. `model`'s
    /// scale is the quad's full size (matching the original per-quad renderer).
    void drawColoredQuad(const glm::mat4& model, const glm::vec3& color);
    void renderSpawnPoints(Map* map, bool showSpawnPoints);
    void renderMap(Map* map);
    void clear();
    static glm::mat4 createQuadModel(float x, float y, float w, float h);

private:
    std::unique_ptr<IRenderBackend> backend_;
    std::vector<ModelDraw> worldModels_;
    std::vector<ModelDraw> viewmodelModels_;

    Batch batches_[BATCH_COUNT];
    /// Bumped whenever the persistent WORLD_OPAQUE batch changes, so the
    /// backend re-uploads the map geometry only then.
    std::uint64_t staticRevision_ = 0;
    /// UI instances in draw order and their draw ranges (built in endFrame).
    std::vector<InstanceGPU> uiDrawList_;
    std::vector<UiRange> uiRanges_;

    int viewportX_ = 0, viewportY_ = 0, viewportW_ = 800, viewportH_ = 600;
    bool vsync_ = true;
    FrameGenSettings frameGen_;
    bool backendInitialized_ = false;
    bool cameraCutPending_ = true;   ///< the first 3D frame has no history
    bool lastFrameHad3D_ = false;
    double lastEndFrameTime_ = 0.0;  ///< seconds (steady clock)

    bool inUi_ = false;
    glm::mat4 view_{1.0f};               ///< active (UI while drawing UI)
    glm::mat4 projection_{1.0f};
    glm::mat4 worldView_{1.0f};          ///< last 3D camera view
    glm::mat4 worldProjection_{1.0f};    ///< last 3D camera projection
    glm::mat4 viewmodelView_{1.0f};      ///< camera basis for the held weapon
    glm::mat4 viewmodelProjection_{1.0f};

    std::vector<int> mapTextureLayers_;
    int checkerLayer_ = -1;

    /// UI elements in submission order (see UiItem).
    std::vector<UiItem> uiItems_;
    /// Upper bound on UI draw calls per frame (see endFrame).
    static constexpr int kMaxUiSlots = 12;


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
    size_t worldModelsLastFrame_ = 0;        ///< glTF draws in the world pass
    size_t viewmodelModelsLastFrame_ = 0;    ///< glTF draws in the viewmodel pass

public:
    /// How many times the map batch was drawn during the last frame. It must be
    /// exactly 1 (a second draw shows the world twice with different cameras).
    int debugWorldDrawsLastFrame() const { return worldDrawsThisFrame_; }
    /// The map batch instance count at the end of the last frame.
    size_t debugMapInstancesLastFrame() const { return mapInstancesLastFrame_; }
    /// Faces submitted for the first-person weapon in the last frame (0 when
    /// no viewmodel was drawn).
    size_t debugViewmodelInstancesLastFrame() const { return viewmodelInstancesLastFrame_; }
    /// glTF model draws in the world / viewmodel passes last frame.
    size_t debugWorldModelsLastFrame() const { return worldModelsLastFrame_; }
    size_t debugViewmodelModelsLastFrame() const { return viewmodelModelsLastFrame_; }
};

#endif // RENDERER_HPP
