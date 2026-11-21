/**
 * @file render_backend.hpp
 * @brief API-neutral interface between the Renderer frontend and a GPU backend.
 *
 * The Renderer (renderer.hpp) is the CPU frontend: it batches boxes, builds the
 * painter's-order UI list and collects glTF draws. Once per frame it hands a
 * FramePacket to an IRenderBackend, which owns every GPU object and issues the
 * actual draws. OpenGL is the reference backend; Vulkan and D3D12 backends
 * implement the same interface.
 *
 * Nothing in this header may depend on a graphics API.
 */

#ifndef CUBE_ENGINE_RENDER_BACKEND_HPP
#define CUBE_ENGINE_RENDER_BACKEND_HPP

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GltfModel;
class GltfInstance;
struct GLFWwindow;

/// One quad of a box face or a UI quad; the box shader expands it from a unit
/// quad. The layout is the GPU vertex layout (instance stream), so backends
/// upload these arrays as-is.
struct BoxInstance {
    glm::vec3 offset;   ///< face centre
    glm::vec3 right;    ///< half-extent along the quad's x
    glm::vec3 up;       ///< half-extent along the quad's y
    glm::vec3 normal;   ///< outward face normal (drives shading)
    glm::vec4 color;    ///< rgb + alpha
    glm::vec4 uv;       ///< tile u0, v0, u1, v1
    glm::vec4 tint;     ///< rgb tint for textured instances
    glm::vec4 atlas;    ///< x = array layer, y = textured flag
};
static_assert(sizeof(BoxInstance) == 112, "instance layout must match the vertex attributes");
static_assert(offsetof(BoxInstance, color) == 48, "color attribute offset");
static_assert(offsetof(BoxInstance, uv) == 64, "uv attribute offset");
static_assert(offsetof(BoxInstance, tint) == 80, "tint attribute offset");
static_assert(offsetof(BoxInstance, atlas) == 96, "atlas attribute offset");

/// One queued glTF draw. `instance` holds the evaluated skeleton pose.
struct ModelDraw {
    GltfModel* model = nullptr;
    GltfInstance* instance = nullptr;
    glm::mat4 transform{1.0f};
    glm::vec3 tint{1.0f};
};

/// A run of consecutive UI instances drawn with one blend state.
struct UiRange {
    std::size_t first = 0;
    std::size_t count = 0;
    bool blended = false;
};

/// Everything the backend needs to draw one frame. Pointers stay valid for
/// the duration of IRenderBackend::renderFrame only.
struct FramePacket {
    int viewportX = 0, viewportY = 0, viewportW = 800, viewportH = 600;
    glm::vec4 clearColor{0.1f, 0.2f, 0.3f, 1.0f};

    // ── World pass (depth-tested, back faces culled) ────────────────────────
    glm::mat4 worldViewProjection{1.0f};
    /// The same camera split up (frame generation needs view and projection).
    glm::mat4 worldView{1.0f};
    glm::mat4 worldProjection{1.0f};
    /// Persistent map geometry. Re-upload only when `staticRevision` changes.
    const std::vector<BoxInstance>* worldStatic = nullptr;
    std::uint64_t staticRevision = 0;
    /// Per-frame world geometry (enemies, projectiles, markers).
    const std::vector<BoxInstance>* worldDynamic = nullptr;
    const std::vector<ModelDraw>* worldModels = nullptr;

    // ── First-person viewmodel (cleared depth, own camera) ───────────────────
    glm::mat4 viewmodelViewProjection{1.0f};
    const std::vector<BoxInstance>* viewmodelBoxes = nullptr;
    const std::vector<ModelDraw>* viewmodelModels = nullptr;

    // ── UI (no depth, painter's order) ───────────────────────────────────────
    glm::mat4 uiViewProjection{1.0f};
    const std::vector<BoxInstance>* uiInstances = nullptr;
    const std::vector<UiRange>* uiRanges = nullptr;

    // ── Frame history (motion vectors, frame generation) ─────────────────────
    /// True when this frame shows 3D (a pure menu frame has none).
    bool has3D = false;
    /// The camera jumped (map change, respawn, teleport, menu -> game): there
    /// is no motion history across this frame.
    bool cameraCut = false;
    /// Seconds since the previous frame was submitted.
    float frameTime = 0.0f;
};

/// Source data for the shared texture array (map textures + checker layer).
struct TextureArrayDesc {
    int width = 64;
    int height = 64;
    /// RGBA8 pixels per layer (width * height * 4), or nullptr to leave the
    /// layer undefined. The vector size is the layer count.
    std::vector<const unsigned char*> layers;
};

/// A finished frame read back from the backend (tests, screenshots).
struct CapturedImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;   ///< RGB8, top row first
    /// Motion vectors (2 floats per pixel, UV units, previous - current
    /// position, top row first). Empty when the backend has none (OpenGL).
    std::vector<float> motion;
};

/// Device information for logs (adapter, API version, vendor).
struct RenderBackendInfo {
    std::string api;       ///< "OpenGL", "Vulkan", "Direct3D 12"
    std::string version;
    std::string renderer;  ///< adapter / device name
    std::string vendor;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /// Create pipelines and static buffers. Returns false if the backend
    /// cannot draw at all (the frontend then skips frames).
    virtual bool init() = 0;
    /// Release every GPU object. Must run on the thread that owns the device.
    virtual void shutdown() = 0;

    /// (Re)build the texture array and the built-in glyph atlas.
    virtual void setTextureArray(const TextureArrayDesc& desc) = 0;
    /// Draw one frame into the current render target.
    virtual void renderFrame(const FramePacket& frame) = 0;
    /// Show the finished frame (swap / queue present).
    virtual void present() = 0;
    /// Wait for vertical blank when presenting (true) or present immediately.
    virtual void setVsync(bool enabled) = 0;

    /// Read back the next frame drawn by renderFrame (before it is presented).
    virtual void requestCapture() = 0;
    /// The captured frame, once; false if none is ready.
    virtual bool takeCapture(CapturedImage& out) = 0;

    virtual RenderBackendInfo describe() const = 0;
};

/// Vulkan backend (NVRHI) for a window made by
/// createRenderWindow(RenderApi::Vulkan, ...). nullptr when the engine was
/// built without Vulkan; init() fails when no usable Vulkan driver exists.
std::unique_ptr<IRenderBackend> createVulkanRenderBackend(GLFWwindow* window);
/// Whether this build contains the Vulkan backend.
bool vulkanBackendCompiledIn();

/// The OpenGL 3.3 core reference backend for a window made by
/// createRenderWindow(RenderApi::OpenGL, ...); its context must be current.
std::unique_ptr<IRenderBackend> createGlRenderBackend(GLFWwindow* window);

#endif // CUBE_ENGINE_RENDER_BACKEND_HPP
