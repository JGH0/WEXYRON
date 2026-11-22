/**
 * @file graphics_capabilities.cpp
 * @brief OS + GPU probe behind graphics_capabilities.hpp
 *
 * Vulkan is the only cross-vendor API this build can query without a window,
 * so the probe creates a throwaway Vulkan instance (only when the loader is
 * present) and inspects the adapters. That also tells the menu which GPU
 * vendors are installed. On a machine without Vulkan the regular OpenGL path
 * stays available, and every Vulkan-only option is reported as unavailable.
 */

#include "engine/graphics_capabilities.hpp"

#if defined(CUBE_WITH_VULKAN)
// Vulkan before GLFW: GLFW's Vulkan declarations need VK_VERSION_1_0.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace {

std::string osName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
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
        default: return "Unknown";
    }
}

/// "NVIDIA GeForce RTX 4070" -> 40 (0 when the name has no RTX number).
int nvidiaRtxGeneration(const std::string& name) {
    std::string upper = name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const size_t rtx = upper.find("RTX ");
    if (rtx == std::string::npos) return 0;
    size_t pos = rtx + 4;
    while (pos < upper.size() && upper[pos] == ' ') ++pos;
    // GeForce names start with the series number ("RTX 4070"); workstation
    // "RTX A4000" / "RTX PRO 6000" names are not parsed (they are not the
    // consumer RTX 40/50 series this check is for).
    if (pos >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[pos]))) return 0;
    int generation = 0;
    while (pos < upper.size() && std::isdigit(static_cast<unsigned char>(upper[pos]))) {
        generation = generation * 10 + (upper[pos] - '0');
        ++pos;
    }
    // Names are "4070", "4080 Laptop", "5090", ...; the first two digits are
    // the series. Keep the whole number so a future 60 series still works.
    return generation >= 100 ? generation / 100 : generation;
}

#if defined(CUBE_WITH_VULKAN)

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& extension : list) {
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    }
    return false;
}

void probeVulkan(GraphicsCapabilities& caps) {
    if (!glfwVulkanSupported()) {
        caps.vulkanNote = "No Vulkan loader or compatible driver found";
        return;
    }

    auto getInstanceProcAddress = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddress) {
        caps.vulkanNote = "Vulkan loader is incomplete (vkGetInstanceProcAddr)";
        return;
    }

    uint32_t requiredCount = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&requiredCount);
    std::vector<const char*> extensions;
    if (required && requiredCount > 0) extensions.assign(required, required + requiredCount);

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        getInstanceProcAddress(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerateInstanceVersion) enumerateInstanceVersion(&loaderVersion);
    if (VK_API_VERSION_MAJOR(loaderVersion) == 1 && VK_API_VERSION_MINOR(loaderVersion) < 2) {
        caps.vulkanNote = "Vulkan 1.2 or newer is required";
        return;
    }

    // Portability enumeration (MoltenVK) is an instance extension; enable it
    // when the loader offers it, as the real backend does.
    uint32_t instanceExtensionCount = 0;
    auto enumerateInstanceExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        getInstanceProcAddress(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (enumerateInstanceExtensions) {
        enumerateInstanceExtensions(nullptr, &instanceExtensionCount, nullptr);
        std::vector<VkExtensionProperties> available(instanceExtensionCount);
        if (instanceExtensionCount > 0) {
            enumerateInstanceExtensions(nullptr, &instanceExtensionCount, available.data());
            if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                const bool alreadyListed =
                    std::find_if(extensions.begin(), extensions.end(), [](const char* extension) {
                        return extension &&
                               std::strcmp(extension, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
                    }) != extensions.end();
                if (!alreadyListed) extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            }
        }
    }
    VkInstanceCreateFlags flags = 0;
    for (const char* extension : extensions) {
        if (extension && std::strcmp(extension, "VK_KHR_portability_enumeration") == 0) {
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "WEXYRON capability probe";
    app.applicationVersion = 1;
    app.pEngineName = "Cube Engine";
    app.engineVersion = 1;
    // Match the real backend: core feature queries (timeline semaphores,
    // dynamic rendering) need an instance of at least the matching version.
    app.apiVersion = VK_API_VERSION_MINOR(loaderVersion) >= 3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.flags = flags;
    createInfo.pApplicationInfo = &app;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        getInstanceProcAddress(nullptr, "vkCreateInstance"));
    if (!createInstance) {
        caps.vulkanNote = "Vulkan loader is incomplete (vkCreateInstance)";
        return;
    }
    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        caps.vulkanNote = "The Vulkan driver could not create an instance";
        return;
    }

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        getInstanceProcAddress(instance, "vkEnumeratePhysicalDevices"));
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        getInstanceProcAddress(instance, "vkGetPhysicalDeviceProperties"));
    auto getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        getInstanceProcAddress(instance, "vkGetPhysicalDeviceFeatures2"));
    auto enumerateExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        getInstanceProcAddress(instance, "vkEnumerateDeviceExtensionProperties"));
    auto getQueueFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        getInstanceProcAddress(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        getInstanceProcAddress(instance, "vkDestroyInstance"));

    std::vector<VkPhysicalDevice> gpus;
    if (enumerate && getProperties) {
        uint32_t count = 0;
        if (enumerate(instance, &count, nullptr) == VK_SUCCESS) {
            gpus.resize(count);
            if (enumerate(instance, &count, gpus.data()) != VK_SUCCESS) gpus.clear();
        }
    }

    bool suitable = false;
    int bestScore = -1;
    int bestSuitableScore = -1;
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props{};
        getProperties(gpu, &props);

        GraphicsAdapterInfo adapter;
        adapter.name = props.deviceName;
        adapter.vendorId = props.vendorID;
        adapter.vendor = vendorName(props.vendorID);
        adapter.discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        caps.adapters.push_back(adapter);

        caps.hasNvidia = caps.hasNvidia || props.vendorID == 0x10DE;
        caps.hasAmd = caps.hasAmd || props.vendorID == 0x1002;
        caps.hasIntel = caps.hasIntel || props.vendorID == 0x8086;
        if (props.vendorID == 0x10DE) {
            const int generation = nvidiaRtxGeneration(adapter.name);
            if (generation >= 40) caps.hasNvidiaRtx40 = true;
            if (generation >= 50) caps.hasNvidiaRtx50 = true;
        }

        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                  : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                  : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (score > bestScore) {
            bestScore = score;
            caps.primaryGpu = adapter.name;
        }

        // Same requirements as VulkanDeviceManager::pickPhysicalDevice(), so
        // the menu does not offer Vulkan for a GPU the backend will reject.
        if (VK_API_VERSION_MAJOR(props.apiVersion) == 1 &&
            VK_API_VERSION_MINOR(props.apiVersion) < 2) continue;
        if (!enumerateExtensions || !getFeatures || !getQueueFamilies) continue;

        uint32_t extensionCount = 0;
        enumerateExtensions(gpu, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
        if (extensionCount > 0) {
            enumerateExtensions(gpu, nullptr, &extensionCount, deviceExtensions.data());
        }
        if (!hasExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        const bool core13 = VK_API_VERSION_MAJOR(props.apiVersion) > 1 ||
                            VK_API_VERSION_MINOR(props.apiVersion) >= 3;
        if (!core13 && (!hasExtension(deviceExtensions, "VK_KHR_dynamic_rendering") ||
                        !hasExtension(deviceExtensions, "VK_KHR_synchronization2"))) continue;

        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceSynchronization2Features synchronization2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        features12.pNext = &dynamicRendering;
        dynamicRendering.pNext = &synchronization2;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features12;
        getFeatures(gpu, &features);

        if (!features.features.independentBlend) continue;
        if (!features12.timelineSemaphore) continue;
        if (!core13 && (!dynamicRendering.dynamicRendering || !synchronization2.synchronization2)) continue;

        uint32_t familyCount = 0;
        getQueueFamilies(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        if (familyCount > 0) getQueueFamilies(gpu, &familyCount, families.data());
        bool hasGraphicsQueue = false;
        for (const VkQueueFamilyProperties& family : families) {
            hasGraphicsQueue = hasGraphicsQueue || (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        }
        if (!hasGraphicsQueue) continue;

        suitable = true;
        if (score > bestSuitableScore) {
            bestSuitableScore = score;
            caps.primaryGpu = adapter.name;
        }
    }

    if (destroyInstance) destroyInstance(instance, nullptr);

    caps.vulkanAvailable = suitable;
    if (suitable) {
        caps.vulkanNote = "Available";
    } else if (caps.adapters.empty()) {
        caps.vulkanNote = "No Vulkan graphics adapter found";
    } else {
        caps.vulkanNote = "No GPU supports the Vulkan 1.2 features the renderer needs";
    }
}

#endif // CUBE_WITH_VULKAN

void describeFrameGeneration(GraphicsCapabilities& caps) {
    const bool windows = caps.os == "Windows";

#if defined(CUBE_WITH_FSR)
    constexpr bool fsrCompiled = true;
#else
    constexpr bool fsrCompiled = false;
#endif
#if defined(CUBE_WITH_STREAMLINE)
    constexpr bool streamlineCompiled = true;
#else
    constexpr bool streamlineCompiled = false;
#endif

    caps.frameGenFsr = caps.vulkanAvailable && fsrCompiled && windows;
    if (!caps.frameGenFsr) {
        if (!caps.vulkanAvailable) caps.fsrNote = "Needs the Vulkan renderer";
        else if (!windows) caps.fsrNote = "AMD FSR frame generation is Windows-only in this build";
        else caps.fsrNote = "AMD FSR is not part of this build (run tools/fetch_sdks.py)";
    }

    caps.frameGenDlss = caps.vulkanAvailable && streamlineCompiled && windows && caps.hasNvidiaRtx40;
    if (!caps.frameGenDlss) {
        if (!caps.vulkanAvailable) caps.dlssNote = "Needs the Vulkan renderer";
        else if (!windows) caps.dlssNote = "NVIDIA DLSS frame generation needs Windows";
        else if (!streamlineCompiled) caps.dlssNote = "NVIDIA DLSS is not part of this build (run tools/fetch_sdks.py)";
        else caps.dlssNote = "NVIDIA DLSS needs an NVIDIA GeForce RTX 40 series or newer";
    }
    caps.dlssMultiFrame = caps.frameGenDlss && caps.hasNvidiaRtx50;

    // XeSS frame generation (XeLL) is a Direct3D 12 feature; the Direct3D 12
    // renderer is not part of this branch yet.
    caps.frameGenXess = false;
    caps.xessNote = "Intel XeSS frame generation needs the Direct3D 12 renderer (not available yet)";
}

} // namespace

const GraphicsCapabilities& graphicsCapabilities() {
    static const GraphicsCapabilities capabilities = [] {
        GraphicsCapabilities caps;
        caps.os = osName();
#if defined(CUBE_WITH_VULKAN)
        caps.vulkanBackendBuilt = true;
        probeVulkan(caps);
#else
        caps.vulkanBackendBuilt = false;
        caps.vulkanNote = "This build has no Vulkan backend (run 'git submodule update --init')";
#endif
        describeFrameGeneration(caps);
        return caps;
    }();
    return capabilities;
}
