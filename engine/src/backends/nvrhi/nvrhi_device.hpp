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

#include <cstdint>
#include <memory>

struct GLFWwindow;

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
};

/// Vulkan device manager. Uses the Vulkan loader through GLFW, so the game has
/// no link-time dependency on it; create() fails cleanly when it is missing.
std::unique_ptr<NvrhiDeviceManager> createVulkanDeviceManager();

/// IRenderBackend on top of a device manager (see nvrhi_backend.cpp).
std::unique_ptr<IRenderBackend> createNvrhiRenderBackend(std::unique_ptr<NvrhiDeviceManager> manager,
                                                         GLFWwindow* window);

#endif // CUBE_ENGINE_NVRHI_DEVICE_HPP
