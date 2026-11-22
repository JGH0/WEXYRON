/**
 * @file vulkan_device.cpp
 * @brief Vulkan device manager for the NVRHI backend (see nvrhi_device.hpp).
 *
 * The Vulkan loader is reached through GLFW (glfwGetInstanceProcAddress), so
 * the executable does not link against vulkan-1 / libvulkan: on a machine
 * without Vulkan, create() fails and the game keeps using OpenGL.
 *
 * Requirements (NVRHI): Vulkan 1.2 with timeline semaphores, plus dynamic
 * rendering and synchronization2 (core in 1.3, KHR extensions on 1.2-only
 * drivers such as older MoltenVK). macOS needs portability enumeration.
 *
 * CUBE_VULKAN_VALIDATION=1 enables the Khronos validation layer (when
 * installed) and NVRHI's own validation; messages go to stderr.
 *
 * Frame generation: AMD FSR replaces the swapchain (fsr_vulkan.cpp) and needs
 * queues of its own (async compute, present, image acquire), so the device is
 * created with them whenever the GPU has enough queues.
 *
 * NVIDIA DLSS frame generation (streamline_vulkan.cpp): on machines with an
 * NVIDIA GPU and the Streamline files, Vulkan is reached through Streamline's
 * interposer instead (it adds what DLSS-G needs at device creation). DLSS-G is
 * loaded only while its swapchain exists; switching it on or off rebuilds the
 * swapchain. Reflex markers run every frame while Streamline is loaded.
 */

#if defined(CUBE_WITH_VULKAN)

#include "nvrhi_device.hpp"
#include "fsr_vulkan.hpp"
#include "streamline_vulkan.hpp"

// Must match NVRHI's vulkan-backend.h; the dispatcher storage lives here.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/vulkan.h>
#include <nvrhi/validation.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>   // Vulkan helpers are declared because vulkan.h came first
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

#define VKD VULKAN_HPP_DEFAULT_DISPATCHER

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kAcquireSemaphores = kFramesInFlight + 1;

bool envFlag(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

struct NvrhiLog final : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity == nvrhi::MessageSeverity::Info) return;
        std::cerr << "[nvrhi] " << text << std::endl;
    }
};
NvrhiLog g_nvrhiLog;

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessenger(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage) {
        std::cerr << "[vulkan] " << data->pMessage << std::endl;
    }
    return VK_FALSE;
}

std::string vendorName(uint32_t id) {
    switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x106B: return "Apple";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x10005: return "Mesa";
        default: return "vendor 0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%X", id); return std::string(b); }();
    }
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

#if defined(_WIN32)
/// Whether any Vulkan GPU is an NVIDIA one (a throwaway instance through the
/// plain loader): only then is Streamline put between the game and Vulkan.
bool hasNvidiaGpu(PFN_vkGetInstanceProcAddr gipa) {
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    if (!createInstance) return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&info, nullptr, &instance) != VK_SUCCESS) return false;
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        gipa(instance, "vkGetPhysicalDeviceProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
    bool nvidia = false;
    if (enumerate && properties) {
        uint32_t count = 0;
        enumerate(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> gpus(count);
        enumerate(instance, &count, gpus.data());
        for (VkPhysicalDevice gpu : gpus) {
            VkPhysicalDeviceProperties props;
            properties(gpu, &props);
            nvidia = nvidia || props.vendorID == 0x10DE;
        }
    }
    if (destroy) destroy(instance, nullptr);
    return nvidia;
}
#endif

class VulkanDeviceManager final : public NvrhiDeviceManager {
public:
    ~VulkanDeviceManager() override { destroy(); }

    bool create(GLFWwindow* window) override;
    void destroy() override;

    nvrhi::IDevice* device() const override { return nvrhiDevice_; }
    bool beginFrame() override;
    void present() override;
    void setVsync(bool enabled) override {
        if (enabled != vsync_) { vsync_ = enabled; swapchainDirty_ = true; }
    }

    nvrhi::ITexture* backBuffer() const override {
        return imageIndex_ < backBuffers_.size() ? backBuffers_[imageIndex_].Get() : nullptr;
    }
    uint32_t backBufferIndex() const override { return imageIndex_; }
    uint32_t backBufferCount() const override { return static_cast<uint32_t>(backBuffers_.size()); }
    uint32_t width() const override { return extent_.width; }
    uint32_t height() const override { return extent_.height; }
    uint64_t swapchainGeneration() const override { return generation_; }
    RenderBackendInfo describe() const override { return info_; }

private:
    bool fail(const char* what) {
        std::cerr << "Vulkan: " << what << std::endl;
        return false;
    }
    bool createInstance();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    void releaseSwapchainImages();   ///< back buffer handles + present semaphores
    bool recreateSwapchain();

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProps_{};
    bool core13_ = false;
    uint32_t queueFamily_ = 0;
    /// Extra queues for frame generation (queue == VK_NULL_HANDLE if missing).
    FsrVulkan::Queue fgAsyncCompute_, fgPresent_, fgImageAcquire_;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::vector<const char*> instanceExtensions_;
    std::vector<const char*> deviceExtensions_;
    bool validation_ = false;

    nvrhi::vulkan::DeviceHandle vkDevice_;
    nvrhi::DeviceHandle nvrhiDevice_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};
    std::vector<nvrhi::TextureHandle> backBuffers_;
    std::vector<VkSemaphore> presentSemaphores_;   ///< one per swapchain image
    VkSemaphore acquireSemaphores_[kAcquireSemaphores] = {};
    uint32_t acquireIndex_ = 0;
    uint32_t imageIndex_ = 0;
    bool vsync_ = true;
    bool swapchainDirty_ = false;
    uint64_t generation_ = 0;

    nvrhi::EventQueryHandle frameQueries_[kFramesInFlight];
    bool frameQuerySet_[kFramesInFlight] = {};
    uint32_t frameSlot_ = 0;

    RenderBackendInfo info_;

    // ── Frame generation ─────────────────────────────────────────────────────
public:
    void setFrameGeneration(const FrameGenSettings& settings) override;
    FrameGenStatus frameGenerationStatus() const override;
    bool frameGenerationComposesUi() const override { return fsrSwapchain_; }
    bool frameGenerationWantsInputs() const override { return fsrSwapchain_ || dlssSwapchain_; }
    void recordFrameGeneration(const FrameGenFrame& frame) override;
    void beginSimulation() override {
        if (sl_) sl_->beginSimulation();
    }

private:
    /// NVIDIA DLSS is what the player asked for (DLSS, or AUTO where it runs).
    bool wantsDlss() const {
        return frameGen_.tech == FrameGenTech::DLSS || (frameGen_.tech == FrameGenTech::Auto && dlssAvailable_);
    }
    /// AMD FSR is what the player asked for (FSR, or AUTO without DLSS).
    bool wantsFsr() const {
        return frameGen_.tech == FrameGenTech::FSR || (frameGen_.tech == FrameGenTech::Auto && !dlssAvailable_);
    }
    void applyLowLatency() {
        // DLSS-G needs Reflex on; otherwise it follows LOW LATENCY.
        if (sl_) sl_->setLowLatency(dlssSwapchain_ || (frameGen_.lowLatency && frameGen_.tech != FrameGenTech::Off));
    }
    FrameGenStatus dlssStatus() const;
    FrameGenSettings frameGen_;
    std::unique_ptr<FsrVulkan> fsr_;
    std::string fsrError_;      ///< why FSR is unavailable (empty when it works)
    bool fsrLoadTried_ = false;
    bool fsrSwapchain_ = false; ///< the current swapchain is FSR's
    std::unique_ptr<StreamlineVulkan> sl_;   ///< NVIDIA GPUs with Streamline installed
    std::string dlssError_;     ///< why DLSS-G is unavailable
    bool dlssAvailable_ = false;
    bool dlssSwapchain_ = false; ///< the current swapchain is Streamline's (DLSS-G)
};

bool VulkanDeviceManager::create(GLFWwindow* window) {
    window_ = window;
    if (!glfwVulkanSupported()) return fail("no Vulkan loader or driver found");
    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) return fail("vkGetInstanceProcAddr unavailable");

    // NVIDIA DLSS: Streamline must see the instance and device being created.
    dlssError_ = "NVIDIA DLSS frame generation needs an NVIDIA GeForce RTX 40 series or newer GPU";
#if defined(_WIN32)
    if (hasNvidiaGpu(getInstanceProcAddr)) {
        sl_ = StreamlineVulkan::load(dlssError_);
        if (sl_) getInstanceProcAddr = sl_->getInstanceProcAddr();
        else std::cerr << "Vulkan: " << dlssError_ << std::endl;
    }
#else
    dlssError_ = "NVIDIA DLSS frame generation needs Windows";
#endif
    VKD.init(getInstanceProcAddr);

    if (!createInstance() || !pickPhysicalDevice() || !createDevice()) {
        destroy();
        return false;
    }
    if (sl_) {
        dlssAvailable_ = sl_->checkDlssg(gpu_, dlssError_);
        if (!dlssAvailable_) std::cerr << "Vulkan: " << dlssError_ << std::endl;
        // Loaded for device creation (queues, extensions); from now on only
        // while its swapchain exists.
        sl_->setDlssgLoaded(false);
    }

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& s : acquireSemaphores_) {
        if (VKD.vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &s) != VK_SUCCESS) {
            destroy();
            return fail("vkCreateSemaphore failed");
        }
    }
    for (auto& q : frameQueries_) q = nvrhiDevice_->createEventQuery();

    if (!createSwapchain()) {
        destroy();
        return false;
    }

    char version[64];
    std::snprintf(version, sizeof version, "%u.%u.%u", VK_API_VERSION_MAJOR(gpuProps_.apiVersion),
                  VK_API_VERSION_MINOR(gpuProps_.apiVersion), VK_API_VERSION_PATCH(gpuProps_.apiVersion));
    info_ = RenderBackendInfo{"Vulkan", version, gpuProps_.deviceName, vendorName(gpuProps_.vendorID)};
    return true;
}

bool VulkanDeviceManager::createInstance() {
    uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    if (!glfwExtensions) return fail("no Vulkan surface support for this window system");

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (VKD.vkEnumerateInstanceVersion) VKD.vkEnumerateInstanceVersion(&loaderVersion);
    if (VK_API_VERSION_MINOR(loaderVersion) < 2 && VK_API_VERSION_MAJOR(loaderVersion) == 1)
        return fail("Vulkan 1.2 or newer is required");

    uint32_t count = 0;
    VKD.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    VKD.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    instanceExtensions_.assign(glfwExtensions, glfwExtensions + glfwCount);
    VkInstanceCreateFlags flags = 0;
    // MoltenVK (macOS) is a "portability" driver: the loader hides it unless
    // the application opts in.
    if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instanceExtensions_.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char*> layers;
    validation_ = envFlag("CUBE_VULKAN_VALIDATION");
    if (validation_) {
        uint32_t layerCount = 0;
        VKD.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layerProps(layerCount);
        VKD.vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
        bool found = false;
        for (const auto& l : layerProps) found = found || std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        if (found) layers.push_back("VK_LAYER_KHRONOS_validation");
        else std::cerr << "Vulkan: validation layer not installed (Vulkan SDK)" << std::endl;
        if (hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            instanceExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "WEXYRON";
    app.pEngineName = "Cube Engine";
    app.apiVersion = (VK_API_VERSION_MINOR(loaderVersion) >= 3) ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions_.size());
    info.ppEnabledExtensionNames = instanceExtensions_.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (VKD.vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) return fail("vkCreateInstance failed");
    VKD.init(vk::Instance(instance_));

    if (validation_ && VKD.vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = debugMessenger;
        VKD.vkCreateDebugUtilsMessengerEXT(instance_, &m, nullptr, &messenger_);
    }

#if defined(_WIN32)
    if (sl_) {
        // Through Streamline (not GLFW's loader): it maps the surface to the
        // window it presents generated frames to.
        VkWin32SurfaceCreateInfoKHR s{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        s.hinstance = GetModuleHandleW(nullptr);
        s.hwnd = glfwGetWin32Window(window_);
        if (!VKD.vkCreateWin32SurfaceKHR || VKD.vkCreateWin32SurfaceKHR(instance_, &s, nullptr, &surface_) != VK_SUCCESS)
            return fail("cannot create a surface for the window");
        return true;
    }
#endif
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS)
        return fail("cannot create a surface for the window");
    return true;
}

bool VulkanDeviceManager::pickPhysicalDevice() {
    uint32_t count = 0;
    VKD.vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    VKD.vkEnumeratePhysicalDevices(instance_, &count, gpus.data());

    int bestScore = -1;
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props;
        VKD.vkGetPhysicalDeviceProperties(gpu, &props);
        if (VK_API_VERSION_MAJOR(props.apiVersion) == 1 && VK_API_VERSION_MINOR(props.apiVersion) < 2) continue;
        const bool core13 = VK_API_VERSION_MINOR(props.apiVersion) >= 3 || VK_API_VERSION_MAJOR(props.apiVersion) > 1;

        uint32_t extCount = 0;
        VKD.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        VKD.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, exts.data());
        if (!hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        if (!core13 && (!hasExtension(exts, "VK_KHR_dynamic_rendering") ||
                        !hasExtension(exts, "VK_KHR_synchronization2"))) continue;

        // Features NVRHI needs.
        VkPhysicalDeviceDynamicRenderingFeatures dyn{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceSynchronization2Features sync2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        f12.pNext = &dyn;
        dyn.pNext = &sync2;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &f12;
        VKD.vkGetPhysicalDeviceFeatures2(gpu, &features);
        if (!f12.timelineSemaphore || !dyn.dynamicRendering || !sync2.synchronization2) continue;
        // The scene pass blends colour but not motion vectors (two targets).
        if (!features.features.independentBlend) continue;

        uint32_t familyCount = 0;
        VKD.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        VKD.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
        int family = -1;
        for (uint32_t i = 0; i < familyCount && family < 0; ++i) {
            VkBool32 present = VK_FALSE;
            VKD.vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = static_cast<int>(i);
        }
        if (family < 0) continue;

        const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (score > bestScore) {
            bestScore = score;
            gpu_ = gpu;
            gpuProps_ = props;
            core13_ = core13;
            queueFamily_ = static_cast<uint32_t>(family);
        }
    }
    if (!gpu_) return fail("no GPU supports Vulkan 1.2 with dynamic rendering, synchronization2 "
                           "and independent blending");
    return true;
}

bool VulkanDeviceManager::createDevice() {
    uint32_t extCount = 0;
    VKD.vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    VKD.vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &extCount, exts.data());

    deviceExtensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (!core13_) {
        deviceExtensions_.push_back("VK_KHR_dynamic_rendering");
        deviceExtensions_.push_back("VK_KHR_synchronization2");
    }
    if (hasExtension(exts, "VK_KHR_portability_subset")) deviceExtensions_.push_back("VK_KHR_portability_subset");

    // Compute shader derivatives: FSR frame generation's shaders use them.
    // Enabled with whatever the GPU supports (KHR, or the older NV variant).
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    bool enableDerivatives = false;
    for (const char* name : {"VK_KHR_compute_shader_derivatives", "VK_NV_compute_shader_derivatives"}) {
        if (!hasExtension(exts, name)) continue;
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &derivatives;
        VKD.vkGetPhysicalDeviceFeatures2(gpu_, &query);
        derivatives.pNext = nullptr;
        if (derivatives.computeDerivativeGroupLinear || derivatives.computeDerivativeGroupQuads) {
            deviceExtensions_.push_back(name);
            enableDerivatives = true;
        }
        break;
    }

    VkPhysicalDeviceSynchronization2Features sync2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync2.synchronization2 = VK_TRUE;
    VkPhysicalDeviceDynamicRenderingFeatures dyn{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dyn.dynamicRendering = VK_TRUE;
    dyn.pNext = &sync2;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.timelineSemaphore = VK_TRUE;
    f12.pNext = core13_ ? static_cast<void*>(&f13) : static_cast<void*>(&dyn);

    // NVIDIA Streamline: the Reflex extension it adds (VK_NV_low_latency2)
    // needs present ids, and it keeps per-object data in private data slots.
    VkPhysicalDevicePresentIdFeaturesKHR presentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    bool enablePresentId = false;
    if (sl_) {
        VkPhysicalDeviceVulkan13Features q13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &presentId;
        presentId.pNext = core13_ ? &q13 : nullptr;
        VKD.vkGetPhysicalDeviceFeatures2(gpu_, &query);
        presentId.pNext = nullptr;
        if (hasExtension(exts, VK_KHR_PRESENT_ID_EXTENSION_NAME) && presentId.presentId) {
            deviceExtensions_.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            enablePresentId = true;
        }
        if (core13_) f13.privateData = q13.privateData;
    }

    // Queues: the game queue, plus three for frame generation where the GPU
    // has them (same preferences as the FSR SDK sample): a present queue
    // (graphics/compute family that can present), an image-acquire queue (no
    // capabilities needed) and an async-compute queue.
    uint32_t familyCount = 0;
    VKD.vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    VKD.vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &familyCount, families.data());
    std::vector<uint32_t> used(familyCount, 0);
    used[queueFamily_] = 1;   // game queue: index 0 of its family
    auto has = [&](uint32_t f, VkQueueFlags flags) { return (families[f].queueFlags & flags) == flags; };
    auto available = [&](uint32_t f) { return used[f] < families[f].queueCount; };
    auto presents = [&](uint32_t f) {
        VkBool32 ok = VK_FALSE;
        VKD.vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, f, surface_, &ok);
        return ok == VK_TRUE;
    };
    // Pick a family; returns its index and reserves a queue in it, or -1.
    auto take = [&](auto&& accept) -> int {
        for (uint32_t f = 0; f < familyCount; ++f) {
            if (available(f) && accept(f)) { return static_cast<int>(f); }
        }
        return -1;
    };
    struct Slot { int family = -1; uint32_t index = 0; };
    auto reserve = [&](int family) {
        Slot slot;
        if (family >= 0) { slot.family = family; slot.index = used[family]++; }
        return slot;
    };
    const VkQueueFlags gfxCompute = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    Slot present = reserve(take([&](uint32_t f) { return has(f, gfxCompute) && presents(f); }));
    if (present.family < 0) present = reserve(take([&](uint32_t f) { return has(f, VK_QUEUE_TRANSFER_BIT) && presents(f); }));
    Slot acquire = reserve(take([&](uint32_t f) {
        return !has(f, VK_QUEUE_COMPUTE_BIT) && !has(f, VK_QUEUE_GRAPHICS_BIT) && !has(f, VK_QUEUE_TRANSFER_BIT); }));
    if (acquire.family < 0) acquire = reserve(take([&](uint32_t f) {
        return !has(f, VK_QUEUE_COMPUTE_BIT) && !has(f, VK_QUEUE_GRAPHICS_BIT); }));
    if (acquire.family < 0) acquire = reserve(take([&](uint32_t f) { return !has(f, VK_QUEUE_GRAPHICS_BIT); }));
    Slot compute = reserve(take([&](uint32_t f) { return has(f, VK_QUEUE_COMPUTE_BIT) && !has(f, VK_QUEUE_GRAPHICS_BIT); }));
    if (compute.family < 0) compute = reserve(take([&](uint32_t f) { return has(f, VK_QUEUE_COMPUTE_BIT); }));

    std::vector<float> priorities(64, 1.0f);
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t f = 0; f < familyCount; ++f) {
        if (!used[f]) continue;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = f;
        q.queueCount = std::min<uint32_t>(used[f], 64);
        q.pQueuePriorities = priorities.data();
        queueInfos.push_back(q);
    }

    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.independentBlend = VK_TRUE;
    features.pNext = &f12;
    if (enableDerivatives) {
        // Chained in front of the 1.2 features (the struct layout is shared
        // between the KHR and NV extensions).
        derivatives.pNext = &f12;
        features.pNext = &derivatives;
    }
    if (enablePresentId) {
        presentId.pNext = features.pNext;
        features.pNext = &presentId;
    }

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    info.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    info.ppEnabledExtensionNames = deviceExtensions_.data();
    if (VKD.vkCreateDevice(gpu_, &info, nullptr, &device_) != VK_SUCCESS) return fail("vkCreateDevice failed");
    VKD.init(vk::Device(device_));
    VKD.vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    auto fetch = [&](const Slot& slot, FsrVulkan::Queue& out) {
        out = FsrVulkan::Queue{};
        if (slot.family < 0) return;
        out.family = static_cast<uint32_t>(slot.family);
        VKD.vkGetDeviceQueue(device_, out.family, slot.index, &out.queue);
    };
    fetch(present, fgPresent_);
    fetch(acquire, fgImageAcquire_);
    fetch(compute, fgAsyncCompute_);

    nvrhi::vulkan::DeviceDesc desc;
    desc.errorCB = &g_nvrhiLog;
    desc.instance = instance_;
    desc.physicalDevice = gpu_;
    desc.device = device_;
    desc.graphicsQueue = queue_;
    desc.graphicsQueueIndex = static_cast<int>(queueFamily_);
    desc.instanceExtensions = instanceExtensions_.data();
    desc.numInstanceExtensions = instanceExtensions_.size();
    desc.deviceExtensions = deviceExtensions_.data();
    desc.numDeviceExtensions = deviceExtensions_.size();
    vkDevice_ = nvrhi::vulkan::createDevice(desc);
    if (!vkDevice_) return fail("NVRHI device creation failed");
    nvrhiDevice_ = validation_ ? nvrhi::validation::createValidationLayer(vkDevice_)
                               : nvrhi::DeviceHandle(vkDevice_.Get());
    return true;
}

bool VulkanDeviceManager::createSwapchain() {
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return true;   // minimised: created on the next frame

    VkSurfaceCapabilitiesKHR caps;
    VKD.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);

    uint32_t formatCount = 0;
    VKD.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VKD.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &formatCount, formats.data());
    // UNORM like the OpenGL default framebuffer (the shaders write display
    // values directly; no sRGB conversion).
    VkSurfaceFormatKHR chosen{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (const auto& f : formats) {
        if (f.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = f; break; }
    }
    if (chosen.format == VK_FORMAT_UNDEFINED) return fail("no 8-bit UNORM swapchain format");

    uint32_t modeCount = 0;
    VKD.vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    VKD.vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &modeCount, modes.data());
    // DLSS-G on Vulkan cannot present with vsync (Streamline guide, 22.5).
    const bool dlss = sl_ && dlssAvailable_ && wantsDlss() && !fsrSwapchain_;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;   // always available = vsync
    if (!vsync_ || dlss) {
        // DLSS-G paces its own presents: no vsync interval at all (IMMEDIATE).
        const VkPresentModeKHR order[2] = {dlss ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_MAILBOX_KHR,
                                           dlss ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR};
        for (VkPresentModeKHR m : order) {
            if (std::find(modes.begin(), modes.end(), m) != modes.end()) { mode = m; break; }
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(static_cast<uint32_t>(fbW), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(fbH), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return true;

    uint32_t imageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    // AMD FSR frame generation: its swapchain replaces ours (it presents real
    // and generated frames). Anything missing falls back to a plain one.
    if (fsrSwapchain_ && swapchain_) {
        std::string error;
        if (!fsr_->recreateSwapchain(info, &swapchain_, error)) {
            std::cerr << "Vulkan: " << error << " - rebuilding the swapchain" << std::endl;
            fsr_->destroySwapchain(swapchain_);
            swapchain_ = VK_NULL_HANDLE;
            fsrSwapchain_ = false;
            fsrError_ = error;
        }
    } else if (dlss) {
        // Streamline hands out its own swapchain while DLSS-G is loaded.
        sl_->setDlssgLoaded(true);
        dlssSwapchain_ = true;
    } else if (wantsFsr()) {
        if (!fsrLoadTried_) {
            fsrLoadTried_ = true;
            fsr_ = FsrVulkan::load(fsrError_);
        }
        if (fsr_ && (!fgPresent_.queue || !fgImageAcquire_.queue || !fgAsyncCompute_.queue)) {
            fsrError_ = "This GPU has too few queues for AMD FSR";
        } else if (fsr_) {
            FsrVulkan::Device d;
            d.physicalDevice = gpu_;
            d.device = device_;
            d.getDeviceProcAddr = VKD.vkGetDeviceProcAddr;
            d.game = FsrVulkan::Queue{queue_, queueFamily_};
            d.asyncCompute = fgAsyncCompute_;
            d.present = fgPresent_;
            d.imageAcquire = fgImageAcquire_;
            swapchain_ = VK_NULL_HANDLE;
            std::string error;
            if (fsr_->createSwapchain(d, info, &swapchain_, error)) {
                fsrSwapchain_ = true;
                fsrError_.clear();
            } else {
                fsrError_ = error;
                std::cerr << "Vulkan: " << error << " - presenting without frame generation" << std::endl;
                swapchain_ = VK_NULL_HANDLE;
            }
        }
    }
    if (!fsrSwapchain_ && VKD.vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
        if (dlssSwapchain_) {
            dlssError_ = "NVIDIA DLSS swapchain creation failed";
            std::cerr << "Vulkan: " << dlssError_ << " - presenting without frame generation" << std::endl;
            dlssAvailable_ = false;
            dlssSwapchain_ = false;
            sl_->setDlssgLoaded(false);
            swapchain_ = VK_NULL_HANDLE;
            return createSwapchain();
        }
        return fail("vkCreateSwapchainKHR failed");
    }
    applyLowLatency();
    swapchainFormat_ = chosen.format;
    extent_ = extent;

    uint32_t count = 0;
    if (fsrSwapchain_) {
        fsr_->getSwapchainImages(swapchain_, &count, nullptr);
    } else {
        VKD.vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    }
    std::vector<VkImage> images(count);
    if (fsrSwapchain_) {
        fsr_->getSwapchainImages(swapchain_, &count, images.data());
    } else {
        VKD.vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());
    }

    const nvrhi::Format format = swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ? nvrhi::Format::BGRA8_UNORM
                                                                               : nvrhi::Format::RGBA8_UNORM;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage image : images) {
        nvrhi::TextureDesc desc;
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = format;
        desc.debugName = "swapchain";
        desc.isRenderTarget = true;
        desc.initialState = nvrhi::ResourceStates::Present;
        desc.keepInitialState = true;
        backBuffers_.push_back(nvrhiDevice_->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc));
        VkSemaphore s = VK_NULL_HANDLE;
        VKD.vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &s);
        presentSemaphores_.push_back(s);
    }
    swapchainDirty_ = false;
    ++generation_;
    return true;
}

void VulkanDeviceManager::releaseSwapchainImages() {
    backBuffers_.clear();
    for (VkSemaphore s : presentSemaphores_) if (s) VKD.vkDestroySemaphore(device_, s, nullptr);
    presentSemaphores_.clear();
}

void VulkanDeviceManager::destroySwapchain() {
    releaseSwapchainImages();
    if (swapchain_) {
        if (fsrSwapchain_) fsr_->destroySwapchain(swapchain_);
        else VKD.vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    fsrSwapchain_ = false;
    if (dlssSwapchain_) sl_->setDlssgLoaded(false);
    dlssSwapchain_ = false;
    swapchain_ = VK_NULL_HANDLE;
    extent_ = {0, 0};
}

bool VulkanDeviceManager::recreateSwapchain() {
    nvrhiDevice_->waitForIdle();
    VKD.vkDeviceWaitIdle(device_);   // also the frame generation queues
    if (fsrSwapchain_ && wantsFsr() && swapchain_) {
        // Resize / vsync with FSR on: FSR recreates its swapchain in place
        // (a full rebuild leaks FSR-internal objects every time).
        releaseSwapchainImages();
        return createSwapchain();
    }
    destroySwapchain();
    return createSwapchain();
}

bool VulkanDeviceManager::beginFrame() {
    if (!nvrhiDevice_) return false;
    if (sl_) sl_->beginRenderSubmit();

    // Frame pacing: at most kFramesInFlight frames queued on the GPU.
    if (frameQuerySet_[frameSlot_]) {
        nvrhiDevice_->waitEventQuery(frameQueries_[frameSlot_]);
        nvrhiDevice_->resetEventQuery(frameQueries_[frameSlot_]);
        frameQuerySet_[frameSlot_] = false;
    }

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return false;   // minimised
    if (!swapchain_ || swapchainDirty_ ||
        static_cast<uint32_t>(fbW) != extent_.width || static_cast<uint32_t>(fbH) != extent_.height) {
        if (!recreateSwapchain() || !swapchain_) return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        VkSemaphore semaphore = acquireSemaphores_[acquireIndex_];
        const VkResult result = fsrSwapchain_
            ? fsr_->acquireNextImage(swapchain_, UINT64_MAX, semaphore, &imageIndex_)
            : VKD.vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex_);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreateSwapchain() || !swapchain_) return false;
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return false;
        if (result == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;   // recreate next frame
        acquireIndex_ = (acquireIndex_ + 1) % kAcquireSemaphores;
        // The frame's first submission waits until the image is really free.
        vkDevice_->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
        return true;
    }
    return false;
}

void VulkanDeviceManager::present() {
    if (!swapchain_ || imageIndex_ >= presentSemaphores_.size()) return;
    VkSemaphore done = presentSemaphores_[imageIndex_];
    // NVRHI attaches pending semaphores to its next submission: an empty
    // command list carries the "rendering finished" signal.
    vkDevice_->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, done, 0);
    nvrhi::CommandListHandle signal = nvrhiDevice_->createCommandList();
    signal->open();
    signal->close();
    nvrhiDevice_->executeCommandList(signal);

    if (sl_) {
        sl_->endRenderSubmit();
        sl_->beginPresent();
    }
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &done;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &imageIndex_;
    const VkResult result = fsrSwapchain_ ? fsr_->present(queue_, &info) : VKD.vkQueuePresentKHR(queue_, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;
    if (sl_) sl_->endPresent();

    nvrhiDevice_->setEventQuery(frameQueries_[frameSlot_], nvrhi::CommandQueue::Graphics);
    frameQuerySet_[frameSlot_] = true;
    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;
    nvrhiDevice_->runGarbageCollection();
}

void VulkanDeviceManager::destroy() {
    if (nvrhiDevice_) nvrhiDevice_->waitForIdle();
    if (device_) VKD.vkDeviceWaitIdle(device_);
    for (auto& q : frameQueries_) q = nullptr;
    if (device_) {
        destroySwapchain();
        for (VkSemaphore& s : acquireSemaphores_) {
            if (s) VKD.vkDestroySemaphore(device_, s, nullptr);
            s = VK_NULL_HANDLE;
        }
    }
    fsr_.reset();
    nvrhiDevice_ = nullptr;
    vkDevice_ = nullptr;
    if (sl_) sl_->shutdown();   // before the device goes (Streamline guide)
    if (device_) VKD.vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (surface_) VKD.vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_) VKD.vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    if (instance_) VKD.vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    sl_.reset();   // the interposer DLL, after the last Vulkan call through it
    dlssAvailable_ = false;
}

void VulkanDeviceManager::setFrameGeneration(const FrameGenSettings& settings) {
    const bool hadFsr = wantsFsr();
    const bool hadDlss = wantsDlss() && dlssAvailable_;
    frameGen_ = settings;
    // Switching FSR or DLSS on or off swaps the swapchain (recreated on the
    // next frame); changes within one (multiplier, generation off) do not.
    if (hadFsr != wantsFsr() || hadDlss != (wantsDlss() && dlssAvailable_)) swapchainDirty_ = true;
    applyLowLatency();
}

FrameGenStatus VulkanDeviceManager::dlssStatus() const {
    FrameGenStatus status;
    if (!dlssAvailable_) {
        status.note = dlssError_;
        return status;
    }
    if (!dlssSwapchain_) {
        status.note = "NVIDIA DLSS starts with the next frame";
        return status;
    }
    if (!sl_->runtimeError().empty()) {
        status.note = sl_->runtimeError();
        return status;
    }
    const uint32_t maxFrames = std::max<uint32_t>(1, sl_->maxFramesToGenerate());
    const uint32_t frames = std::clamp<uint32_t>(static_cast<uint32_t>(std::max(1, frameGen_.generatedFrames)), 1,
                                                 maxFrames);
    status.active = "NVIDIA DLSS " + std::to_string(frames + 1) + "X";
    status.presentedFrames = sl_->presentedFrames();
    if (static_cast<uint32_t>(frameGen_.generatedFrames) > maxFrames)
        status.note = "This GPU generates up to " + std::to_string(maxFrames + 1) + "X with NVIDIA DLSS";
    else if (sl_->generatingFrames() == 0)
        status.note = "Runs while the game is on screen (off in full-screen menus)";
    else if (vsync_)
        status.note = "VSync is off while NVIDIA DLSS frame generation runs (Vulkan)";
    else if (!frameGen_.lowLatency)
        status.note = "NVIDIA Reflex stays on with DLSS frame generation";
    return status;
}

FrameGenStatus VulkanDeviceManager::frameGenerationStatus() const {
    FrameGenStatus status;
    switch (frameGen_.tech) {
        case FrameGenTech::Off:
            return status;
        case FrameGenTech::DLSS:
            return dlssStatus();
        case FrameGenTech::XeSS:
            status.note = "Intel XeSS frame generation needs the Direct3D 12 renderer";
            return status;
        case FrameGenTech::Auto:
            if (dlssAvailable_) return dlssStatus();
            break;
        case FrameGenTech::FSR:
            break;
    }
    if (!fsrSwapchain_) {
        status.note = fsrError_.empty() ? std::string("AMD FSR starts with the next frame") : fsrError_;
        return status;
    }
    status.active = "AMD FSR 2X";
    status.presentedFrames = fsr_->presentCount(swapchain_);
    if (frameGen_.generatedFrames > 1) status.note = "AMD FSR generates one frame per rendered frame (2X)";
    else if (frameGen_.lowLatency && gpuProps_.vendorID != 0x1002) status.note = "Anti-Lag 2 needs an AMD GPU";
    return status;
}

void VulkanDeviceManager::recordFrameGeneration(const FrameGenFrame& frame) {
    if (!frame.commandList) return;
    const auto commandBuffer = static_cast<VkCommandBuffer>(
        frame.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer).pointer);
    if (dlssSwapchain_ && sl_) {
        auto image = [](nvrhi::ITexture* t) {
            StreamlineVulkan::Image im;
            im.image = static_cast<VkImage>(t->getNativeObject(nvrhi::ObjectTypes::VK_Image).pointer);
            im.memory = static_cast<VkDeviceMemory>(t->getNativeObject(nvrhi::ObjectTypes::VK_DeviceMemory).pointer);
            im.view = static_cast<VkImageView>(t->getNativeView(nvrhi::ObjectTypes::VK_ImageView).pointer);
            im.info = static_cast<const VkImageCreateInfo*>(
                t->getNativeObject(nvrhi::ObjectTypes::VK_ImageCreateInfo).pointer);
            im.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;   // NVRHI ShaderResource
            return im;
        };
        StreamlineVulkan::FrameInputs in;
        in.commandBuffer = commandBuffer;
        in.depth = image(frame.depth);
        in.motion = image(frame.motion);
        in.hudless = image(frame.sceneColor);
        in.ui = image(frame.ui);
        in.width = frame.width;
        in.height = frame.height;
        in.reset = frame.reset;
        in.generate = !frame.inMenu;
        in.framesToGenerate = static_cast<uint32_t>(std::max(1, frameGen_.generatedFrames));
        in.view = frame.view;
        in.projection = frame.projection;
        in.prevView = frame.prevView;
        in.prevProjection = frame.prevProjection;
        std::string error;
        if (!sl_->frame(in, error)) {
            if (dlssError_ != error) std::cerr << "Vulkan: " << error << std::endl;
            dlssError_ = error;
        }
        return;
    }
    if (!fsrSwapchain_ || !fsr_) return;
    FsrVulkan::FrameInputs in;
    in.commandBuffer = commandBuffer;
    in.depth = static_cast<VkImage>(frame.depth->getNativeObject(nvrhi::ObjectTypes::VK_Image).pointer);
    in.motion = static_cast<VkImage>(frame.motion->getNativeObject(nvrhi::ObjectTypes::VK_Image).pointer);
    in.ui = static_cast<VkImage>(frame.ui->getNativeObject(nvrhi::ObjectTypes::VK_Image).pointer);
    in.width = frame.width;
    in.height = frame.height;
    in.frameTimeMs = frame.frameTimeMs;
    in.reset = frame.reset;
    in.cameraNear = frame.cameraNear;
    in.cameraFar = frame.cameraFar;
    in.fovY = frame.fovY;
    in.position = frame.position;
    in.up = frame.up;
    in.right = frame.right;
    in.forward = frame.forward;
    std::string error;
    if (!fsr_->frame(swapchain_, swapchainFormat_, in, frameGen_.tech != FrameGenTech::Off, error)) {
        if (fsrError_ != error) std::cerr << "Vulkan: " << error << std::endl;
        fsrError_ = error;
    }
}

} // namespace

std::unique_ptr<NvrhiDeviceManager> createVulkanDeviceManager() {
    return std::make_unique<VulkanDeviceManager>();
}

#endif // CUBE_WITH_VULKAN
