/**
 * @file streamline_vulkan.cpp
 * @brief NVIDIA DLSS frame generation + Reflex through Streamline on Vulkan
 *        (see streamline_vulkan.hpp).
 *
 * Follows the Streamline 2.14 programming guides (ProgrammingGuide.md,
 * ProgrammingGuideDLSS_G.md, ProgrammingGuideReflex.md):
 *   - slInit before any Vulkan call; Vulkan through sl.interposer.dll
 *   - every frame: new frame token, slReflexSleep, PCL markers around
 *     simulation / render submit / present (same token as the constants)
 *   - slSetConstants once per frame, inputs tagged with slSetTagForFrame
 *     (eValidUntilPresent: the images are not reused before the present)
 *   - DLSS-G off in menus; the swapchain is recreated when DLSS-G is loaded or
 *     unloaded (done by the device manager)
 *
 * The interposer is loaded at runtime (no link-time dependency) after its
 * NVIDIA signature was verified, as the guide asks for production builds.
 *
 * Built only with the SDK files from tools/fetch_sdks.py (CUBE_WITH_STREAMLINE)
 * on Windows; everywhere else load() reports why DLSS is unavailable.
 */

#include "streamline_vulkan.hpp"

#if defined(CUBE_WITH_VULKAN) && defined(CUBE_WITH_STREAMLINE) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion-null"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_security.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

/// The folder of the running executable (Streamline is shipped there).
std::wstring exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, length);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
}

std::string resultText(sl::Result r) {
    switch (r) {
        case sl::Result::eOk: return "ok";
        case sl::Result::eErrorDriverOutOfDate: return "the NVIDIA driver is too old";
        case sl::Result::eErrorOSOutOfDate: return "Windows is too old";
        case sl::Result::eErrorOSDisabledHWS: return "hardware-accelerated GPU scheduling is off (Windows graphics settings)";
        case sl::Result::eErrorNoSupportedAdapterFound:
        case sl::Result::eErrorAdapterNotSupported: return "this GPU is not supported";
        case sl::Result::eErrorNoPlugins:
        case sl::Result::eErrorFeatureMissing: return "the Streamline plugins are missing";
        case sl::Result::eErrorFeatureFailedToLoad: return "the plugin failed to load";
        case sl::Result::eErrorFeatureNotSupported: return "not supported on this system";
        default: return "Streamline error " + std::to_string(static_cast<int>(r));
    }
}

/// CUBE_STREAMLINE_LOG=1: Streamline's verbose log (info lines included).
bool verboseLog() {
    const char* v = std::getenv("CUBE_STREAMLINE_LOG");
    return v && *v && std::strcmp(v, "0") != 0;
}

void logMessage(sl::LogType type, const char* message) {
    if (!message || (type == sl::LogType::eInfo && !verboseLog())) return;
    std::cerr << "[streamline] " << message;
    const size_t n = std::strlen(message);
    if (n == 0 || message[n - 1] != '\n') std::cerr << '\n';
    std::cerr.flush();
}

/// glm (column-major, column vectors) to Streamline (row-major, row vectors):
/// the same 16 floats.
sl::float4x4 toSl(const glm::mat4& m) {
    sl::float4x4 out;
    static_assert(sizeof out == 16 * sizeof(float), "sl::float4x4 is 16 floats");
    std::memcpy(static_cast<void*>(&out), glm::value_ptr(m), sizeof out);
    return out;
}

/// OpenGL clip z [-w, w] to Vulkan [0, w] (what the shaders write).
glm::mat4 vulkanClip(const glm::mat4& projection) {
    glm::mat4 remap(1.0f);
    remap[2][2] = 0.5f;
    remap[3][2] = 0.5f;
    return remap * projection;
}

template <typename F>
F* procAddress(HMODULE module, const char* name) {
    return reinterpret_cast<F*>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

} // namespace

struct StreamlineVulkan::Impl {
    HMODULE module = nullptr;
    bool initialized = false;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;

    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slIsFeatureSupported* isFeatureSupported = nullptr;
    PFun_slSetFeatureLoaded* setFeatureLoaded = nullptr;
    PFun_slGetFeatureFunction* getFeatureFunction = nullptr;
    PFun_slGetNewFrameToken* getNewFrameToken = nullptr;
    PFun_slSetConstants* setConstants = nullptr;
    PFun_slSetTagForFrame* setTagForFrame = nullptr;

    PFun_slDLSSGGetState* dlssgGetState = nullptr;
    PFun_slDLSSGSetOptions* dlssgSetOptions = nullptr;
    PFun_slReflexSleep* reflexSleep = nullptr;
    PFun_slReflexSetOptions* reflexSetOptions = nullptr;
    PFun_slPCLSetMarker* pclSetMarker = nullptr;

    sl::FrameToken* token = nullptr;
    uint32_t frameIndex = 0;
    enum class Phase { Idle, Simulating, Submitting, Presenting } phase = Phase::Idle;

    sl::DLSSGMode mode = sl::DLSSGMode::eOff;
    uint32_t minSize = 0;

    template <typename F>
    void feature(sl::Feature f, const char* name, F*& out) {
        void* fn = nullptr;
        out = (getFeatureFunction && getFeatureFunction(f, name, fn) == sl::Result::eOk) ? reinterpret_cast<F*>(fn)
                                                                                        : nullptr;
    }
    void fetchDlssg() {
        feature(sl::kFeatureDLSS_G, "slDLSSGGetState", dlssgGetState);
        feature(sl::kFeatureDLSS_G, "slDLSSGSetOptions", dlssgSetOptions);
    }
    void marker(sl::PCLMarker m) {
        if (pclSetMarker && token) pclSetMarker(m, *token);
    }
};

std::unique_ptr<StreamlineVulkan> StreamlineVulkan::load(std::string& error) {
    if (const char* v = std::getenv("CUBE_STREAMLINE"); v && std::strcmp(v, "0") == 0) {
        error = "NVIDIA Streamline disabled (CUBE_STREAMLINE=0)";
        return nullptr;
    }
    const std::wstring path = exeDirectory() + L"sl.interposer.dll";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "NVIDIA DLSS files are not installed (tools/fetch_sdks.py)";
        return nullptr;
    }
    if (!sl::security::verifyEmbeddedSignature(path.c_str())) {
        error = "sl.interposer.dll is not signed by NVIDIA - DLSS disabled";
        return nullptr;
    }
    std::unique_ptr<StreamlineVulkan> sl(new StreamlineVulkan());
    sl->impl_ = std::make_unique<Impl>();
    Impl& i = *sl->impl_;
    i.module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!i.module) {
        error = "cannot load sl.interposer.dll";
        return nullptr;
    }
    i.init = procAddress<PFun_slInit>(i.module, "slInit");
    i.shutdown = procAddress<PFun_slShutdown>(i.module, "slShutdown");
    i.isFeatureSupported = procAddress<PFun_slIsFeatureSupported>(i.module, "slIsFeatureSupported");
    i.setFeatureLoaded = procAddress<PFun_slSetFeatureLoaded>(i.module, "slSetFeatureLoaded");
    i.getFeatureFunction = procAddress<PFun_slGetFeatureFunction>(i.module, "slGetFeatureFunction");
    i.getNewFrameToken = procAddress<PFun_slGetNewFrameToken>(i.module, "slGetNewFrameToken");
    i.setConstants = procAddress<PFun_slSetConstants>(i.module, "slSetConstants");
    i.setTagForFrame = procAddress<PFun_slSetTagForFrame>(i.module, "slSetTagForFrame");
    i.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(i.module, "vkGetInstanceProcAddr")));
    if (!i.init || !i.shutdown || !i.isFeatureSupported || !i.setFeatureLoaded || !i.getFeatureFunction ||
        !i.getNewFrameToken || !i.setConstants || !i.setTagForFrame || !i.getInstanceProcAddr) {
        error = "sl.interposer.dll is incomplete";
        return nullptr;
    }

    const std::wstring dir = exeDirectory();
    const wchar_t* pluginPaths[] = {dir.c_str()};
    const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    sl::Preferences pref{};
    pref.showConsole = false;
    pref.logLevel = verboseLog() ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    pref.pathsToPlugins = pluginPaths;
    pref.numPathsToPlugins = 1;
    pref.logMessageCallback = logMessage;
    // No over-the-air plugin updates: the game runs the DLLs it ships.
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = static_cast<uint32_t>(sizeof features / sizeof features[0]);
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "1.0";
    pref.projectId = "5c1b2a4e-7d3f-4e8a-9b6c-2f0e1d3a4b5c";
    pref.renderAPI = sl::RenderAPI::eVulkan;
    const sl::Result result = i.init(pref, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        error = "NVIDIA Streamline could not start: " + resultText(result);
        return nullptr;
    }
    i.initialized = true;   // feature functions exist once the device does (checkDlssg)
    return sl;
}

StreamlineVulkan::~StreamlineVulkan() {
    if (!impl_) return;
    shutdown();
    if (impl_->module) FreeLibrary(impl_->module);
}

PFN_vkGetInstanceProcAddr StreamlineVulkan::getInstanceProcAddr() const { return impl_->getInstanceProcAddr; }

bool StreamlineVulkan::checkDlssg(VkPhysicalDevice gpu, std::string& why) {
    Impl& i = *impl_;
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = gpu;
    i.feature(sl::kFeaturePCL, "slPCLSetMarker", i.pclSetMarker);   // every vendor
    // Reflex (low latency) is optional for the game, but DLSS-G needs it.
    if (i.isFeatureSupported(sl::kFeatureReflex, adapter) == sl::Result::eOk) {
        i.feature(sl::kFeatureReflex, "slReflexSleep", i.reflexSleep);
        i.feature(sl::kFeatureReflex, "slReflexSetOptions", i.reflexSetOptions);
    }
    setLowLatency(false);   // must be set once even when off
    const sl::Result result = i.isFeatureSupported(sl::kFeatureDLSS_G, adapter);
    if (result != sl::Result::eOk) {
        why = result == sl::Result::eErrorNoSupportedAdapterFound || result == sl::Result::eErrorAdapterNotSupported
                  ? std::string("NVIDIA DLSS frame generation needs a GeForce RTX 40 series or newer GPU")
                  : "NVIDIA DLSS frame generation unavailable: " + resultText(result);
        return false;
    }
    if (!i.reflexSleep) {
        why = "NVIDIA DLSS frame generation needs NVIDIA Reflex, which is unavailable";
        return false;
    }
    i.fetchDlssg();
    if (!i.dlssgGetState || !i.dlssgSetOptions) {
        why = "NVIDIA DLSS frame generation plugin is incomplete";
        return false;
    }
    return true;
}

void StreamlineVulkan::setDlssgLoaded(bool loaded) {
    Impl& i = *impl_;
    if (loaded == dlssgLoaded_ || !i.initialized) return;
    i.setFeatureLoaded(sl::kFeatureDLSS_G, loaded);
    dlssgLoaded_ = loaded;
    i.mode = sl::DLSSGMode::eOff;
    generating_ = 0;
    presented_ = 0;
    runtimeError_.clear();
    if (loaded) i.fetchDlssg();
}

void StreamlineVulkan::setLowLatency(bool enabled) {
    Impl& i = *impl_;
    if (!i.reflexSetOptions) return;
    sl::ReflexOptions options{};
    options.mode = enabled ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
    i.reflexSetOptions(options);
}

void StreamlineVulkan::beginSimulation() {
    Impl& i = *impl_;
    if (!i.initialized) return;
    uint32_t index = ++i.frameIndex;
    if (i.getNewFrameToken(i.token, &index) != sl::Result::eOk) {
        i.token = nullptr;
        return;
    }
    if (i.reflexSleep) i.reflexSleep(*i.token);
    i.marker(sl::PCLMarker::eSimulationStart);
    i.phase = Impl::Phase::Simulating;
}

void StreamlineVulkan::beginRenderSubmit() {
    Impl& i = *impl_;
    if (!i.initialized) return;
    if (i.phase != Impl::Phase::Simulating) beginSimulation();
    i.marker(sl::PCLMarker::eSimulationEnd);
    i.marker(sl::PCLMarker::eRenderSubmitStart);
    i.phase = Impl::Phase::Submitting;
}

void StreamlineVulkan::endRenderSubmit() {
    Impl& i = *impl_;
    if (i.phase == Impl::Phase::Submitting) i.marker(sl::PCLMarker::eRenderSubmitEnd);
}

void StreamlineVulkan::beginPresent() {
    Impl& i = *impl_;
    if (i.phase != Impl::Phase::Submitting) return;
    i.marker(sl::PCLMarker::ePresentStart);
    i.phase = Impl::Phase::Presenting;
}

void StreamlineVulkan::endPresent() {
    Impl& i = *impl_;
    if (i.phase != Impl::Phase::Presenting) return;
    i.marker(sl::PCLMarker::ePresentEnd);
    i.phase = Impl::Phase::Idle;
    if (!dlssgLoaded_ || !i.dlssgGetState) return;
    sl::DLSSGState state{};
    if (i.dlssgGetState(sl::ViewportHandle(0), state, nullptr) != sl::Result::eOk) return;
    presented_ += std::max<uint32_t>(1, state.numFramesActuallyPresented);
    if (state.numFramesToGenerateMax > 0) maxFrames_ = state.numFramesToGenerateMax;
    i.minSize = state.minWidthOrHeight;
    if (state.status != sl::DLSSGStatus::eOk && i.mode != sl::DLSSGMode::eOff) {
        const auto status = static_cast<uint32_t>(state.status);
        std::string what;
        if (state.status & sl::DLSSGStatus::eFailResolutionTooLow) what = "the window is too small";
        else if (state.status & sl::DLSSGStatus::eFailReflexNotDetectedAtRuntime) what = "Reflex was not detected";
        else if (state.status & sl::DLSSGStatus::eFailCommonConstantsInvalid) what = "invalid camera constants";
        else what = "status " + std::to_string(status);
        const std::string error = "NVIDIA DLSS frame generation stopped: " + what;
        if (error != runtimeError_) std::cerr << "Vulkan: " << error << std::endl;
        runtimeError_ = error;
    }
}

bool StreamlineVulkan::frame(const FrameInputs& in, std::string& error) {
    Impl& i = *impl_;
    if (!i.initialized || !dlssgLoaded_ || !i.dlssgSetOptions) {
        error = "NVIDIA DLSS frame generation is not loaded";
        return false;
    }
    if (i.phase != Impl::Phase::Submitting) beginRenderSubmit();
    if (!i.token) {
        error = "Streamline gave no frame token";
        return false;
    }
    const sl::ViewportHandle viewport(0);

    // ── Common constants (once per frame) ──
    const glm::mat4 projection = vulkanClip(in.projection);
    const glm::mat4 prevProjection = vulkanClip(in.prevProjection);
    const glm::mat4 clipToPrevClip = (prevProjection * in.prevView) * glm::inverse(projection * in.view);
    const glm::mat4 cameraToWorld = glm::inverse(in.view);
    const glm::mat4& P = in.projection;   // OpenGL perspective
    sl::Constants c{};
    c.cameraViewToClip = toSl(projection);
    c.clipToCameraView = toSl(glm::inverse(projection));
    c.clipToLensClip = toSl(glm::mat4(1.0f));
    c.clipToPrevClip = toSl(clipToPrevClip);
    c.prevClipToClip = toSl(glm::inverse(clipToPrevClip));
    c.jitterOffset = sl::float2(0.0f, 0.0f);
    c.mvecScale = sl::float2(1.0f, 1.0f);   // motion vectors are in UV units already
    c.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    const glm::vec3 pos(cameraToWorld[3]), right(glm::normalize(glm::vec3(cameraToWorld[0]))),
        up(glm::normalize(glm::vec3(cameraToWorld[1]))), fwd(-glm::normalize(glm::vec3(cameraToWorld[2])));
    c.cameraPos = sl::float3(pos.x, pos.y, pos.z);
    c.cameraUp = sl::float3(up.x, up.y, up.z);
    c.cameraRight = sl::float3(right.x, right.y, right.z);
    c.cameraFwd = sl::float3(fwd.x, fwd.y, fwd.z);
    if (P[1][1] != 0.0f && P[0][0] != 0.0f) {
        c.cameraNear = P[3][2] / (P[2][2] - 1.0f);
        c.cameraFar = P[3][2] / (P[2][2] + 1.0f);
        c.cameraFOV = 2.0f * std::atan(1.0f / P[1][1]);
        c.cameraAspectRatio = P[1][1] / P[0][0];
    } else {
        c.cameraNear = 0.1f;
        c.cameraFar = 1000.0f;
        c.cameraFOV = 1.0f;
        c.cameraAspectRatio = in.height ? static_cast<float>(in.width) / static_cast<float>(in.height) : 1.0f;
    }
    c.motionVectorsInvalidValue = 0.0f;
    c.depthInverted = sl::Boolean::eFalse;
    c.cameraMotionIncluded = sl::Boolean::eTrue;
    c.motionVectors3D = sl::Boolean::eFalse;
    c.reset = in.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    c.orthographicProjection = sl::Boolean::eFalse;
    c.motionVectorsDilated = sl::Boolean::eFalse;
    c.motionVectorsJittered = sl::Boolean::eFalse;
    sl::Result r = i.setConstants(c, *i.token, viewport);
    if (r != sl::Result::eOk) {
        error = "slSetConstants: " + resultText(r);
        return false;
    }

    // ── Inputs (null tags in menus, as the guide asks) ──
    const bool generate = in.generate && runtimeError_.empty() && (i.minSize == 0 ||
                          std::min(in.width, in.height) >= i.minSize);
    auto describe = [&](const Image& im) {
        sl::Resource res(sl::ResourceType::eTex2d, reinterpret_cast<void*>(im.image),
                         reinterpret_cast<void*>(im.memory), reinterpret_cast<void*>(im.view),
                         static_cast<uint32_t>(im.layout));
        if (im.info) {
            res.width = im.info->extent.width;
            res.height = im.info->extent.height;
            res.nativeFormat = static_cast<uint32_t>(im.info->format);
            res.mipLevels = im.info->mipLevels;
            res.arrayLayers = im.info->arrayLayers;
            res.flags = static_cast<uint32_t>(im.info->flags);
            res.usage = static_cast<uint32_t>(im.info->usage);
        }
        return res;
    };
    sl::Resource depth = describe(in.depth), motion = describe(in.motion), hudless = describe(in.hudless),
                 ui = describe(in.ui);
    const sl::Extent extent{0, 0, in.width, in.height};
    const auto lifecycle = sl::ResourceLifecycle::eValidUntilPresent;
    sl::ResourceTag tags[] = {
        sl::ResourceTag(generate ? &depth : nullptr, sl::kBufferTypeDepth, lifecycle, &extent),
        sl::ResourceTag(generate ? &motion : nullptr, sl::kBufferTypeMotionVectors, lifecycle, &extent),
        sl::ResourceTag(generate ? &hudless : nullptr, sl::kBufferTypeHUDLessColor, lifecycle, &extent),
        sl::ResourceTag(generate ? &ui : nullptr, sl::kBufferTypeUIColorAndAlpha, lifecycle, &extent),
    };
    r = i.setTagForFrame(*i.token, viewport, tags, static_cast<uint32_t>(sizeof tags / sizeof tags[0]),
                         reinterpret_cast<sl::CommandBuffer*>(in.commandBuffer));
    if (r != sl::Result::eOk) {
        error = "slSetTagForFrame: " + resultText(r);
        return false;
    }

    // ── Options: every frame, as the Streamline sample does (applied at the
    // next present). Setting them only on change lost the first "on" now and
    // then while DLSS-G was still starting, leaving it off for good.
    const sl::DLSSGMode mode = generate ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    const uint32_t frames = std::clamp<uint32_t>(in.framesToGenerate, 1, std::max<uint32_t>(1, maxFrames_));
    sl::DLSSGOptions options{};
    options.mode = mode;
    options.numFramesToGenerate = frames;
    // Menus switch it off briefly: keep the resources (no stutter).
    options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    r = i.dlssgSetOptions(viewport, options);
    if (r != sl::Result::eOk) {
        error = "slDLSSGSetOptions: " + resultText(r);
        return false;
    }
    i.mode = mode;
    generating_ = mode == sl::DLSSGMode::eOn ? frames : 0;
    return true;
}

void StreamlineVulkan::shutdown() {
    Impl& i = *impl_;
    if (!i.initialized) return;
    i.shutdown();
    i.initialized = false;
    i.token = nullptr;
}

#else // no Streamline in this build

struct StreamlineVulkan::Impl {};

std::unique_ptr<StreamlineVulkan> StreamlineVulkan::load(std::string& error) {
#if defined(_WIN32)
    error = "NVIDIA DLSS is not part of this build (tools/fetch_sdks.py, then rebuild)";
#else
    error = "NVIDIA DLSS frame generation needs Windows";
#endif
    return nullptr;
}
StreamlineVulkan::~StreamlineVulkan() = default;
PFN_vkGetInstanceProcAddr StreamlineVulkan::getInstanceProcAddr() const { return nullptr; }
bool StreamlineVulkan::checkDlssg(VkPhysicalDevice, std::string& why) {
    why = "NVIDIA DLSS unavailable";
    return false;
}
void StreamlineVulkan::setDlssgLoaded(bool) {}
void StreamlineVulkan::setLowLatency(bool) {}
void StreamlineVulkan::beginSimulation() {}
void StreamlineVulkan::beginRenderSubmit() {}
bool StreamlineVulkan::frame(const FrameInputs&, std::string& error) {
    error = "NVIDIA DLSS unavailable";
    return false;
}
void StreamlineVulkan::endRenderSubmit() {}
void StreamlineVulkan::beginPresent() {}
void StreamlineVulkan::endPresent() {}
void StreamlineVulkan::shutdown() {}

#endif
