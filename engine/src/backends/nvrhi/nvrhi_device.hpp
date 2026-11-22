/**
 * @file nvrhi_device.hpp
 * @brief Internal: graphics device + swapchain behind the NVRHI render backend.
 *
 * The NVRHI backend (nvrhi_backend.cpp) is API-neutral: it records frames with
 * an nvrhi::IDevice. A device manager owns everything API-specific around it:
 * instance/device creation, the swapchain, presenting, vsync and frame pacing.
 * Vulkan is implemented in vulkan_device.cpp; Direct3D 12 will be another
 * implementation of this interface.
 */

#ifndef CUBE_ENGINE_NVRHI_DEVICE_HPP
#define CUBE_ENGINE_NVRHI_DEVICE_HPP

#include "engine/render_backend.hpp"

#include <nvrhi/nvrhi.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

struct GLFWwindow;

/// One rendered frame's inputs for vendor frame generation.
struct FrameGenFrame {
    nvrhi::ICommandList* commandList = nullptr;   ///< open; inputs already in shader-read state
    nvrhi::ITexture* depth = nullptr;             ///< D32, whole image
    nvrhi::ITexture* motion = nullptr;            ///< RG16F, UV units, previous - current
    nvrhi::ITexture* ui = nullptr;                ///< RGBA8 premultiplied HUD (render-target state)
    uint32_t width = 0, height = 0;
    float frameTimeMs = 16.6f;
    bool reset = false;                           ///< camera cut
    float cameraNear = 0.1f, cameraFar = 100.0f, fovY = 1.0f;   ///< radians
    glm::vec3 position{0.0f}, up{0.0f, 1.0f, 0.0f}, right{1.0f, 0.0f, 0.0f}, forward{0.0f, 0.0f, -1.0f};
};

class NvrhiDeviceManager {
public:
    virtual ~NvrhiDeviceManager() = default;

    /// Create the device and swapchain for `window`. Returns false (and logs
    /// why) when the API is unusable on this machine.
    virtual bool create(GLFWwindow* window) = 0;
    /// Wait for the GPU and release everything. Safe to call twice.
    virtual void destroy() = 0;

    virtual nvrhi::IDevice* device() const = 0;

    /// Wait until a frame slot is free and acquire the next swapchain image.
    /// Recreates the swapchain when the window size changed. Returns false when
    /// nothing can be drawn this frame (minimised window).
    virtual bool beginFrame() = 0;
    /// Present the image acquired by beginFrame (after the frame was executed).
    virtual void present() = 0;

    virtual void setVsync(bool enabled) = 0;

    /// Current swapchain image (valid between beginFrame and present).
    virtual nvrhi::ITexture* backBuffer() const = 0;
    virtual uint32_t backBufferIndex() const = 0;
    virtual uint32_t backBufferCount() const = 0;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    /// Bumped whenever the swapchain (and so every back buffer) was recreated.
    virtual uint64_t swapchainGeneration() const = 0;

    virtual RenderBackendInfo describe() const = 0;

    // ── Vendor frame generation (lives with the swapchain) ──────────────────
    virtual void setFrameGeneration(const FrameGenSettings& settings) { (void)settings; }
    virtual FrameGenStatus frameGenerationStatus() const { return FrameGenStatus{}; }
    /// True while a frame-generation swapchain composes the HUD itself: the
    /// back buffer then gets the HUD-less scene and the HUD goes in `ui`.
    virtual bool frameGenerationComposesUi() const { return false; }
    /// Record frame generation for this frame (only when the above is true).
    virtual void recordFrameGeneration(const FrameGenFrame& frame) { (void)frame; }
};

/// Vulkan device manager. Uses the Vulkan loader through GLFW, so the game has
/// no link-time dependency on it; create() fails cleanly when it is missing.
std::unique_ptr<NvrhiDeviceManager> createVulkanDeviceManager();

/// IRenderBackend on top of a device manager (see nvrhi_backend.cpp).
std::unique_ptr<IRenderBackend> createNvrhiRenderBackend(std::unique_ptr<NvrhiDeviceManager> manager,
                                                         GLFWwindow* window);

#endif // CUBE_ENGINE_NVRHI_DEVICE_HPP
