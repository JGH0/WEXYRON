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
 */

#if defined(CUBE_WITH_VULKAN)

#include "nvrhi_device.hpp"

// Must match NVRHI's vulkan-backend.h; the dispatcher storage lives here.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/vulkan.h>
#include <nvrhi/validation.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>   // Vulkan helpers are declared because vulkan.h came first

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
    bool recreateSwapchain();

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProps_{};
    bool core13_ = false;
    uint32_t queueFamily_ = 0;
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
};

bool VulkanDeviceManager::create(GLFWwindow* window) {
    window_ = window;
    if (!glfwVulkanSupported()) return fail("no Vulkan loader or driver found");
    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) return fail("vkGetInstanceProcAddr unavailable");
    VKD.init(getInstanceProcAddr);

    if (!createInstance() || !pickPhysicalDevice() || !createDevice()) {
        destroy();
        return false;
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
    if (!gpu_) return fail("no GPU supports Vulkan 1.2 with dynamic rendering and synchronization2");
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

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &f12;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    info.ppEnabledExtensionNames = deviceExtensions_.data();
    if (VKD.vkCreateDevice(gpu_, &info, nullptr, &device_) != VK_SUCCESS) return fail("vkCreateDevice failed");
    VKD.init(vk::Device(device_));
    VKD.vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

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
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;   // always available = vsync
    if (!vsync_) {
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
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
    if (VKD.vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS)
        return fail("vkCreateSwapchainKHR failed");
    swapchainFormat_ = chosen.format;
    extent_ = extent;

    uint32_t count = 0;
    VKD.vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    std::vector<VkImage> images(count);
    VKD.vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());

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

void VulkanDeviceManager::destroySwapchain() {
    backBuffers_.clear();
    for (VkSemaphore s : presentSemaphores_) if (s) VKD.vkDestroySemaphore(device_, s, nullptr);
    presentSemaphores_.clear();
    if (swapchain_) VKD.vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    extent_ = {0, 0};
}

bool VulkanDeviceManager::recreateSwapchain() {
    nvrhiDevice_->waitForIdle();
    VKD.vkQueueWaitIdle(queue_);
    destroySwapchain();
    return createSwapchain();
}

bool VulkanDeviceManager::beginFrame() {
    if (!nvrhiDevice_) return false;

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
        const VkResult result = VKD.vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, semaphore,
                                                          VK_NULL_HANDLE, &imageIndex_);
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

    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &done;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &imageIndex_;
    const VkResult result = VKD.vkQueuePresentKHR(queue_, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;

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
    nvrhiDevice_ = nullptr;
    vkDevice_ = nullptr;
    if (device_) VKD.vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (surface_) VKD.vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_) VKD.vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    if (instance_) VKD.vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

} // namespace

std::unique_ptr<NvrhiDeviceManager> createVulkanDeviceManager() {
    return std::make_unique<VulkanDeviceManager>();
}

#endif // CUBE_WITH_VULKAN
