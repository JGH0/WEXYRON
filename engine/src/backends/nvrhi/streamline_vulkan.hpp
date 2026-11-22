/**
 * @file streamline_vulkan.hpp
 * @brief Internal: NVIDIA DLSS frame generation + Reflex through Streamline
 *        (Vulkan, Windows).
 *
 * Streamline is an interposer: the game reaches Vulkan through
 * sl.interposer.dll's vkGetInstanceProcAddr instead of the loader's, so
 * Streamline sees instance, device and swapchain creation, adds what DLSS-G
 * needs (queues, extensions) and, while DLSS-G is loaded, hands out a swapchain
 * that presents real and generated frames from its own queue.
 *
 * This class owns sl.interposer.dll (signature checked, loaded from the
 * executable's folder) and drives the per-frame API:
 *   - Reflex: frame token, sleep and latency markers (PCL) every frame
 *   - DLSS-G: common constants, tagged inputs (depth, motion vectors, HUD-less
 *     colour, UI) and options (on/off, frames to generate)
 *
 * Only created on machines with an NVIDIA GPU (see the device manager), so
 * other vendors keep reaching the Vulkan loader directly.
 */

#ifndef CUBE_ENGINE_STREAMLINE_VULKAN_HPP
#define CUBE_ENGINE_STREAMLINE_VULKAN_HPP

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

class StreamlineVulkan {
public:
    /// A Vulkan image as Streamline wants it described.
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        const VkImageCreateInfo* info = nullptr;
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };
    /// One rendered frame's DLSS-G inputs.
    struct FrameInputs {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;   ///< open, game queue
        Image depth, motion, hudless, ui;
        uint32_t width = 0, height = 0;
        bool reset = false;             ///< camera cut
        bool generate = true;           ///< false in menus: present real frames only
        uint32_t framesToGenerate = 1;  ///< 1 = 2X, 3 = 4X (clamped to the GPU's maximum)
        /// OpenGL-style matrices (clip z in [-1, 1]); converted here.
        glm::mat4 view{1.0f}, projection{1.0f}, prevView{1.0f}, prevProjection{1.0f};
    };

    /// Load and initialise Streamline. Returns nullptr and sets `error` when
    /// it is not installed, not genuine, or this build has no Streamline.
    static std::unique_ptr<StreamlineVulkan> load(std::string& error);
    ~StreamlineVulkan();

    /// Use this instead of the loader's (before creating the instance).
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const;

    /// After the GPU is chosen: whether DLSS-G can run on it (else `why`).
    bool checkDlssg(VkPhysicalDevice gpu, std::string& why);
    /// Load/unload DLSS-G. Only between swapchains: a swapchain created while
    /// it is loaded is Streamline's (frame generation), otherwise the driver's.
    void setDlssgLoaded(bool loaded);
    bool dlssgLoaded() const { return dlssgLoaded_; }
    /// Reflex low-latency mode (markers run regardless).
    void setLowLatency(bool enabled);

    // ── Per frame, in this order ────────────────────────────────────────────
    /// New frame: Reflex sleep, then the simulation (input, game update).
    void beginSimulation();
    /// Simulation done, GPU work recording starts (starts a frame if needed).
    void beginRenderSubmit();
    /// DLSS-G constants, tags and options (while the swapchain is Streamline's).
    bool frame(const FrameInputs& inputs, std::string& error);
    void endRenderSubmit();
    void beginPresent();
    /// After vkQueuePresentKHR: updates the presented frame count.
    void endPresent();

    /// Frames shown by the DLSS-G swapchain (rendered + generated).
    uint64_t presentedFrames() const { return presented_; }
    /// The GPU's maximum generated frames per rendered frame (1 on RTX 40).
    uint32_t maxFramesToGenerate() const { return maxFrames_; }
    /// Frames DLSS-G generates per rendered frame right now (0 = off).
    uint32_t generatingFrames() const { return generating_; }
    /// Last DLSS-G runtime problem (empty when it runs fine).
    const std::string& runtimeError() const { return runtimeError_; }
    void resetPresentedFrames() { presented_ = 0; }

    /// slShutdown (before the Vulkan device is destroyed). The interposer DLL
    /// stays loaded until this object goes away (the instance still uses it).
    void shutdown();

private:
    StreamlineVulkan() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool dlssgLoaded_ = true;     ///< slInit loads every requested feature
    uint64_t presented_ = 0;
    uint32_t maxFrames_ = 1;
    uint32_t generating_ = 0;
    std::string runtimeError_;
};

#endif // CUBE_ENGINE_STREAMLINE_VULKAN_HPP
