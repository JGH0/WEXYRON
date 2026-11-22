/**
 * @file fsr_vulkan.hpp
 * @brief Internal: AMD FSR frame generation on Vulkan (FSR SDK 2.4, Windows).
 *
 * FSR frame generation replaces the application's VkSwapchainKHR: the FSR
 * swapchain presents real and generated frames from its own threads, on its
 * own queues, and composes the HUD onto every frame. This class owns:
 *   - the loader DLL (amd_fidelityfx_loader_vk.dll, loaded at runtime from
 *     the executable's folder; it loads amd_fidelityfx_framegeneration_vk.dll)
 *   - the frame generation swapchain context and its replacement functions
 *   - the frame generation context (created for the swapchain size)
 *
 * The device manager routes swapchain create/acquire/present/destroy through
 * it while FSR is selected; the backend calls frame() once per rendered frame.
 */

#ifndef CUBE_ENGINE_FSR_VULKAN_HPP
#define CUBE_ENGINE_FSR_VULKAN_HPP

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

class FsrVulkan {
public:
    /// One queue handed to FSR (must not be used by anyone else).
    struct Queue {
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t family = 0;
    };
    struct Device {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
        Queue game;           ///< the queue the game renders and presents on
        Queue asyncCompute;
        Queue present;
        Queue imageAcquire;
    };
    /// One frame's inputs (images in the states documented per field).
    struct FrameInputs {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;   ///< open, game queue
        VkImage depth = VK_NULL_HANDLE;    ///< D32, shader-read
        VkImage motion = VK_NULL_HANDLE;   ///< RG16F UV motion, shader-read
        VkImage ui = VK_NULL_HANDLE;       ///< RGBA8 premultiplied HUD, render-target layout
        uint32_t width = 0, height = 0;
        float frameTimeMs = 16.6f;
        bool reset = false;
        float cameraNear = 0.1f, cameraFar = 100.0f, fovY = 1.0f;
        glm::vec3 position{0.0f}, up{0.0f, 1.0f, 0.0f}, right{1.0f, 0.0f, 0.0f}, forward{0.0f, 0.0f, -1.0f};
    };

    /// Load the FSR loader DLL. Returns nullptr and sets `error` when FSR is
    /// not installed or this build has no FSR support.
    static std::unique_ptr<FsrVulkan> load(std::string& error);
    ~FsrVulkan();

    /// Create the frame generation swapchain (replaces the app swapchain).
    bool createSwapchain(const Device& device, const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* swapchain,
                         std::string& error);
    /// Recreate the frame generation swapchain in place (resize, vsync).
    bool recreateSwapchain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* swapchain, std::string& error);
    /// Destroy the frame generation contexts and the swapchain.
    void destroySwapchain(VkSwapchainKHR swapchain);

    VkResult getSwapchainImages(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images);
    VkResult acquireNextImage(VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                              uint32_t* imageIndex);
    VkResult present(VkQueue queue, const VkPresentInfoKHR* info);

    /// Configure + prepare frame generation for this frame (records into the
    /// command buffer). `enabled` switches generation on or off without
    /// touching the swapchain. Returns false (and sets `error`) on failure.
    bool frame(VkSwapchainKHR swapchain, VkFormat backBufferFormat, const FrameInputs& inputs, bool enabled,
               std::string& error);

    /// Frames presented by the FSR swapchain (real + generated).
    uint64_t presentCount(VkSwapchainKHR swapchain) const;

private:
    FsrVulkan() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // CUBE_ENGINE_FSR_VULKAN_HPP
