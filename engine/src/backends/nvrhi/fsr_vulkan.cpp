/**
 * @file fsr_vulkan.cpp
 * @brief AMD FSR frame generation on Vulkan (see fsr_vulkan.hpp).
 *
 * Follows the FSR SDK 2.4 Vulkan sample (Samples/Upscalers/FidelityFX_FSR/vk):
 * the frame generation swapchain is created with ffxCreateContext and used
 * through its replacement functions; every frame the game configures frame
 * generation, records the "prepare" dispatch with depth + motion vectors and
 * registers the HUD for composition.
 *
 * Built only with the SDK files from tools/fetch_sdks.py (CUBE_WITH_FSR) on
 * Windows; everywhere else load() reports why FSR is unavailable.
 */

#include "fsr_vulkan.hpp"

#if defined(CUBE_WITH_VULKAN) && defined(CUBE_WITH_FSR) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <ffx_api.h>
#include <ffx_api_types.h>
#include <vk/ffx_api_vk.h>
#include <ffx_framegeneration.h>
#include <vk/ffx_api_framegeneration_vk.h>

#include <cstring>
#include <iostream>

struct FsrVulkan::Impl {
    HMODULE module = nullptr;
    PfnFfxCreateContext createContext = nullptr;
    PfnFfxDestroyContext destroyContext = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;

    Device device;
    ffxContext swapchainContext = nullptr;
    ffxContext frameGenContext = nullptr;
    ffxQueryDescSwapchainReplacementFunctionsVK functions{};
    uint32_t contextWidth = 0, contextHeight = 0;
    VkFormat contextFormat = VK_FORMAT_UNDEFINED;
    uint64_t frameId = 0;

    /// As in the SDK sample: switch generation off and unregister the HUD so
    /// the swapchain only presents the back buffer, wait for every queued
    /// present, then destroy the frame generation context.
    void destroyFrameGenContext(VkSwapchainKHR swapchain) {
        if (frameGenContext) {
            ffxConfigureDescFrameGeneration off{};
            off.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            off.swapChain = reinterpret_cast<void*>(swapchain);
            off.frameGenerationEnabled = false;
            off.frameID = frameId;
            configure(&frameGenContext, &off.header);
        }
        if (swapchainContext) {
            ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK noUi{};
            noUi.header.type = FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;
            configure(&swapchainContext, &noUi.header);
            ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};
            wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
            dispatch(&swapchainContext, &wait.header);
        }
        if (frameGenContext) destroyContext(&frameGenContext, nullptr);
        frameGenContext = nullptr;
        contextWidth = contextHeight = 0;
    }
};

namespace {

void ffxMessage(uint32_t type, const wchar_t* message) {
    std::wcerr << L"[fsr] " << (type == FFX_API_MESSAGE_TYPE_ERROR ? L"error: " : L"warning: ")
               << (message ? message : L"") << std::endl;
}

/// The folder of the running executable (the FSR DLLs are shipped there).
std::wstring exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, length);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
}

FfxApiResource image(VkImage handle, uint32_t format, uint32_t width, uint32_t height, uint32_t usage,
                     uint32_t state) {
    FfxApiResource r{};
    r.resource = reinterpret_cast<void*>(handle);
    r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r.description.format = format;
    r.description.width = width;
    r.description.height = height;
    r.description.depth = 1;
    r.description.mipCount = 1;
    r.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    r.description.usage = usage;
    r.state = state;
    return r;
}

VkQueueInfoFFXAPI queueInfo(const FsrVulkan::Queue& q) {
    VkQueueInfoFFXAPI info{};
    info.queue = q.queue;
    info.familyIndex = q.family;
    info.submitFunc = nullptr;   // FSR submits to its own queues directly
    return info;
}

} // namespace

std::unique_ptr<FsrVulkan> FsrVulkan::load(std::string& error) {
    // Full path next to the executable: never pick up a DLL from the working
    // directory. The loader finds amd_fidelityfx_framegeneration_vk.dll there.
    const std::wstring path = exeDirectory() + L"amd_fidelityfx_loader_vk.dll";
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        error = "AMD FSR files not installed (amd_fidelityfx_loader_vk.dll)";
        return nullptr;
    }
    std::unique_ptr<FsrVulkan> fsr(new FsrVulkan());
    fsr->impl_ = std::make_unique<Impl>();
    Impl& i = *fsr->impl_;
    i.module = module;
    i.createContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module, "ffxCreateContext"));
    i.destroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module, "ffxDestroyContext"));
    i.configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(module, "ffxConfigure"));
    i.query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"));
    i.dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module, "ffxDispatch"));
    if (!i.createContext || !i.destroyContext || !i.configure || !i.query || !i.dispatch) {
        error = "AMD FSR loader is incomplete";
        return nullptr;
    }

    ffxConfigureDescGlobalDebug debug{};
    debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
    debug.fpMessage = ffxMessage;
    debug.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_ERRORS;
    i.configure(nullptr, &debug.header);
    return fsr;
}

FsrVulkan::~FsrVulkan() {
    if (!impl_) return;
    impl_->destroyFrameGenContext(VK_NULL_HANDLE);
    if (impl_->swapchainContext) impl_->destroyContext(&impl_->swapchainContext, nullptr);
    if (impl_->module) FreeLibrary(impl_->module);
}

bool FsrVulkan::createSwapchain(const Device& device, const VkSwapchainCreateInfoKHR& info,
                                VkSwapchainKHR* swapchain, std::string& error) {
    Impl& i = *impl_;
    i.device = device;

    ffxCreateContextDescFrameGenerationSwapChainVK desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    desc.physicalDevice = device.physicalDevice;
    desc.device = device.device;
    desc.swapchain = swapchain;   // in: old swapchain (or null), out: FSR's
    desc.allocator = nullptr;
    desc.createInfo = info;
    desc.gameQueue = queueInfo(device.game);
    desc.asyncComputeQueue = queueInfo(device.asyncCompute);
    desc.presentQueue = queueInfo(device.present);
    desc.imageAcquireQueue = queueInfo(device.imageAcquire);

    ffxCreateContextDescFrameGenerationSwapChainVersionVK version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK;
    version.version = FFX_FRAMEGENERATION_SWAPCHAIN_VK_VERSION;
    desc.header.pNext = &version.header;

    const ffxReturnCode_t rc = i.createContext(&i.swapchainContext, &desc.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !*swapchain) {
        error = "AMD FSR swapchain creation failed (code " + std::to_string(rc) + ")";
        i.swapchainContext = nullptr;
        return false;
    }

    i.functions = {};
    i.functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
    if (i.query(&i.swapchainContext, &i.functions.header) != FFX_API_RETURN_OK ||
        !i.functions.pOutAcquireNextImageKHR || !i.functions.pOutQueuePresentKHR ||
        !i.functions.pOutGetSwapchainImagesKHR || !i.functions.pOutDestroySwapchainFFXAPI) {
        error = "AMD FSR swapchain functions unavailable";
        i.destroyContext(&i.swapchainContext, nullptr);
        i.swapchainContext = nullptr;
        return false;
    }
    i.frameId = 0;
    return true;
}

bool FsrVulkan::recreateSwapchain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR* swapchain,
                                  std::string& error) {
    Impl& i = *impl_;
    if (!i.functions.pOutCreateSwapchainFFXAPI) {
        error = "AMD FSR cannot recreate its swapchain";
        return false;
    }
    // As the SDK sample: the existing swapchain is both the handle to update
    // and the context argument.
    VkSwapchainKHR out = *swapchain;
    const VkResult result = i.functions.pOutCreateSwapchainFFXAPI(i.device.device, &info, nullptr, &out,
                                                                  reinterpret_cast<void*>(*swapchain));
    if (result != VK_SUCCESS) {
        error = "AMD FSR swapchain recreation failed (VkResult " + std::to_string(result) + ")";
        return false;
    }
    *swapchain = out;
    return true;
}

void FsrVulkan::destroySwapchain(VkSwapchainKHR swapchain) {
    Impl& i = *impl_;
    // Order as in the SDK sample: contexts first, then the swapchain itself.
    i.destroyFrameGenContext(swapchain);
    if (i.swapchainContext) i.destroyContext(&i.swapchainContext, nullptr);
    i.swapchainContext = nullptr;
    if (swapchain && i.functions.pOutDestroySwapchainFFXAPI)
        i.functions.pOutDestroySwapchainFFXAPI(i.device.device, swapchain, nullptr, nullptr);
}

VkResult FsrVulkan::getSwapchainImages(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {
    return impl_->functions.pOutGetSwapchainImagesKHR(impl_->device.device, swapchain, count, images);
}

VkResult FsrVulkan::acquireNextImage(VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                     uint32_t* imageIndex) {
    return impl_->functions.pOutAcquireNextImageKHR(impl_->device.device, swapchain, timeout, semaphore,
                                                    VK_NULL_HANDLE, imageIndex);
}

VkResult FsrVulkan::present(VkQueue queue, const VkPresentInfoKHR* info) {
    return impl_->functions.pOutQueuePresentKHR(queue, info);
}

uint64_t FsrVulkan::presentCount(VkSwapchainKHR swapchain) const {
    return impl_->functions.pOutGetLastPresentCountFFXAPI
               ? impl_->functions.pOutGetLastPresentCountFFXAPI(swapchain) : 0;
}

bool FsrVulkan::frame(VkSwapchainKHR swapchain, VkFormat backBufferFormat, const FrameInputs& in, bool enabled,
                      std::string& error) {
    Impl& i = *impl_;
    if (!i.swapchainContext) {
        error = "AMD FSR swapchain missing";
        return false;
    }

    // The frame generation context matches the display size.
    if (!i.frameGenContext || i.contextWidth != in.width || i.contextHeight != in.height ||
        i.contextFormat != backBufferFormat) {
        if (i.frameGenContext) {   // size change: the swapchain was recreated
            i.destroyContext(&i.frameGenContext, nullptr);
            i.frameGenContext = nullptr;
        }
        ffxCreateContextDescFrameGeneration create{};
        create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        create.flags = FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        create.displaySize = {in.width, in.height};
        create.maxRenderSize = {in.width, in.height};
        create.backBufferFormat = ffxApiGetSurfaceFormatVK(backBufferFormat);

        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext.vkDevice = i.device.device;
        backend.deviceContext.vkPhysicalDevice = i.device.physicalDevice;
        backend.deviceContext.vkDeviceProcAddr = i.device.getDeviceProcAddr;

        ffxCreateContextDescFrameGenerationVersion version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        version.version = FFX_FRAMEGENERATION_VERSION;

        create.header.pNext = &backend.header;
        backend.header.pNext = &version.header;
        const ffxReturnCode_t rc = i.createContext(&i.frameGenContext, &create.header, nullptr);
        if (rc != FFX_API_RETURN_OK) {
            error = "AMD FSR frame generation context failed (code " + std::to_string(rc) + ")";
            i.frameGenContext = nullptr;
            return false;
        }
        i.contextWidth = in.width;
        i.contextHeight = in.height;
        i.contextFormat = backBufferFormat;
    }

    // Configure: on/off, swapchain, generation through the default callback.
    ffxConfigureDescFrameGeneration config{};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = reinterpret_cast<void*>(swapchain);
    config.presentCallback = nullptr;          // FSR composes the HUD itself
    config.presentCallbackUserContext = nullptr;
    config.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* context) -> ffxReturnCode_t {
        auto* self = static_cast<Impl*>(context);
        return self->dispatch(&self->frameGenContext, &params->header);
    };
    config.frameGenerationCallbackUserContext = &i;
    config.frameGenerationEnabled = enabled;
    config.allowAsyncWorkloads = false;
    config.HUDLessColor = FfxApiResource{};
    config.flags = 0;
    config.onlyPresentGenerated = false;
    config.generationRect = {0, 0, static_cast<int32_t>(in.width), static_cast<int32_t>(in.height)};
    config.frameID = i.frameId;
    ffxReturnCode_t rc = i.configure(&i.frameGenContext, &config.header);
    if (rc != FFX_API_RETURN_OK) {
        error = "AMD FSR configure failed (code " + std::to_string(rc) + ")";
        return false;
    }

    // Prepare: depth + motion vectors + camera for this frame.
    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = i.frameId;
    prepare.flags = 0;
    prepare.commandList = in.commandBuffer;
    prepare.renderSize = {in.width, in.height};
    prepare.jitterOffset = {0.0f, 0.0f};
    prepare.motionVectorScale = {static_cast<float>(in.width), static_cast<float>(in.height)};   // UV units
    prepare.frameTimeDelta = in.frameTimeMs;
    prepare.reset = in.reset;
    prepare.cameraNear = in.cameraNear;
    prepare.cameraFar = in.cameraFar;
    prepare.cameraFovAngleVertical = in.fovY;
    prepare.viewSpaceToMetersFactor = 1.0f;   // one world unit is about a metre
    prepare.depth = image(in.depth, FFX_API_SURFACE_FORMAT_R32_FLOAT, in.width, in.height,
                          FFX_API_RESOURCE_USAGE_DEPTHTARGET, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    prepare.motionVectors = image(in.motion, FFX_API_SURFACE_FORMAT_R16G16_FLOAT, in.width, in.height,
                                  FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    std::memcpy(prepare.cameraPosition, &in.position, sizeof prepare.cameraPosition);
    std::memcpy(prepare.cameraUp, &in.up, sizeof prepare.cameraUp);
    std::memcpy(prepare.cameraRight, &in.right, sizeof prepare.cameraRight);
    std::memcpy(prepare.cameraForward, &in.forward, sizeof prepare.cameraForward);
    rc = i.dispatch(&i.frameGenContext, &prepare.header);
    if (rc != FFX_API_RETURN_OK) {
        error = "AMD FSR prepare failed (code " + std::to_string(rc) + ")";
        return false;
    }

    // The HUD: premultiplied, composed by the swapchain onto every frame.
    ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK ui{};
    ui.header.type = FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;
    ui.uiResource = image(in.ui, FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM, in.width, in.height,
                          FFX_API_RESOURCE_USAGE_RENDERTARGET, FFX_API_RESOURCE_STATE_RENDER_TARGET);
    ui.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA |
               FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
    i.configure(&i.swapchainContext, &ui.header);

    ++i.frameId;
    return true;
}

#else  // no FSR in this build

struct FsrVulkan::Impl {};

std::unique_ptr<FsrVulkan> FsrVulkan::load(std::string& error) {
#if defined(_WIN32)
    error = "AMD FSR not part of this build (run tools/fetch_sdks.py)";
#else
    error = "AMD FSR frame generation is Windows-only";
#endif
    return nullptr;
}
FsrVulkan::~FsrVulkan() = default;
bool FsrVulkan::createSwapchain(const Device&, const VkSwapchainCreateInfoKHR&, VkSwapchainKHR*, std::string& e) {
    e = "AMD FSR unavailable";
    return false;
}
bool FsrVulkan::recreateSwapchain(const VkSwapchainCreateInfoKHR&, VkSwapchainKHR*, std::string& e) {
    e = "AMD FSR unavailable";
    return false;
}
void FsrVulkan::destroySwapchain(VkSwapchainKHR) {}
VkResult FsrVulkan::getSwapchainImages(VkSwapchainKHR, uint32_t*, VkImage*) { return VK_ERROR_UNKNOWN; }
VkResult FsrVulkan::acquireNextImage(VkSwapchainKHR, uint64_t, VkSemaphore, uint32_t*) { return VK_ERROR_UNKNOWN; }
VkResult FsrVulkan::present(VkQueue, const VkPresentInfoKHR*) { return VK_ERROR_UNKNOWN; }
bool FsrVulkan::frame(VkSwapchainKHR, VkFormat, const FrameInputs&, bool, std::string& e) {
    e = "AMD FSR unavailable";
    return false;
}
uint64_t FsrVulkan::presentCount(VkSwapchainKHR) const { return 0; }

#endif
