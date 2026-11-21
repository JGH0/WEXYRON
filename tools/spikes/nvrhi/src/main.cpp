// Phase 0 spike: NVRHI + GLFW (no client API), one instanced triangle on
// Vulkan (Windows/Linux/macOS via MoltenVK/lavapipe) and D3D12 (Windows).
//
//   nvrhi_spike [--api=vulkan|dx12] [--validate] [--frames=N]
//
// --frames=N renders N frames then exits with 0 (for CI / headless runs).

#ifdef _WIN32
#  define NOMINMAX
#endif
#include <vulkan/vulkan.h>
// NVRHI is linked statically and uses vulkan.hpp's dynamic dispatcher: the
// dispatcher's storage must be defined in exactly one translation unit.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#define GLFW_INCLUDE_NONE      // no GL/gl.h (not installed on GL-less Linux builders)
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <nvrhi/validation.h>
#include <nvrhi/utils.h>
#include <algorithm>
#include <memory>

#ifdef _WIN32
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#  include <d3d12.h>
#  include <dxgi1_6.h>
#  include <wrl/client.h>
#  include <nvrhi/d3d12.h>
using Microsoft::WRL::ComPtr;
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Log : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity s, const char* text) override {
        std::fprintf(stderr, "[nvrhi %d] %s\n", int(s), text);
    }
} g_log;

std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "missing %s\n", path.c_str()); return {}; }
    std::vector<char> data(size_t(f.tellg()));
    f.seekg(0);
    f.read(data.data(), std::streamsize(data.size()));
    return data;
}

#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); return false; } } while (0)

// ── Common interface ─────────────────────────────────────────────────────────
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool init(GLFWwindow* window, bool validate) = 0;
    virtual nvrhi::IDevice* device() = 0;
    virtual const char* shaderExt() const = 0;
    virtual std::string adapterName() const = 0;
    /// Acquire the next back buffer; returns its framebuffer.
    virtual nvrhi::IFramebuffer* beginFrame() = 0;
    virtual void present() = 0;
    virtual void shutdown() = 0;
};

// ── Vulkan ───────────────────────────────────────────────────────────────────
class VulkanBackend final : public Backend {
public:
    bool init(GLFWwindow* window, bool validate) override {
        window_ = window;

        // Instance
        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        if (!glfwExts) {
            // Say what the loader actually offers: usually a missing ICD or a
            // missing platform surface extension.
            uint32_t n = 0;
            const VkResult r = vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> exts(n);
            if (n) vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
            std::fprintf(stderr, "glfwVulkanSupported=%d, loader result=%d, %u instance extensions:\n",
                         glfwVulkanSupported(), int(r), n);
            for (auto& e : exts) std::fprintf(stderr, "  %s\n", e.extensionName);
        }
        CHECK(glfwExts, "glfwGetRequiredInstanceExtensions (no Vulkan loader?)");
        std::vector<const char*> instExts(glfwExts, glfwExts + glfwCount);
        VkInstanceCreateFlags instFlags = 0;
#ifdef __APPLE__
        // MoltenVK is a "portability" implementation: the loader hides it
        // unless the app opts in.
        instExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        std::vector<const char*> layers;
        if (validate) layers.push_back("VK_LAYER_KHRONOS_validation");

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "nvrhi_spike";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.flags = instFlags;
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = uint32_t(instExts.size());
        ici.ppEnabledExtensionNames = instExts.data();
        ici.enabledLayerCount = uint32_t(layers.size());
        ici.ppEnabledLayerNames = layers.data();
        CHECK(vkCreateInstance(&ici, nullptr, &instance_) == VK_SUCCESS, "vkCreateInstance");
        instExts_ = instExts;

        CHECK(glfwCreateWindowSurface(instance_, window, nullptr, &surface_) == VK_SUCCESS,
              "glfwCreateWindowSurface");

        // Physical device: discrete first, then anything that can present.
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance_, &n, nullptr);
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(instance_, &n, gpus.data());
        int bestScore = -1;
        for (VkPhysicalDevice gpu : gpus) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(gpu, &p);
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qf(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qf.data());
            for (uint32_t i = 0; i < qn; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &present);
                if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                                : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; gpu_ = gpu; queueFamily_ = int(i); props_ = p; }
                break;
            }
        }
        CHECK(gpu_, "no Vulkan device can present to this window");

        // Device: NVRHI needs timeline semaphores (1.2) and dynamic rendering
        // (1.3 core, or VK_KHR_dynamic_rendering on 1.2-only drivers such as
        // older MoltenVK).
        const bool core13 = VK_API_VERSION_MINOR(props_.apiVersion) >= 3 ||
                            VK_API_VERSION_MAJOR(props_.apiVersion) > 1;
        devExts_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        // NVRHI also records vkCmdPipelineBarrier2 (synchronization2).
        if (!core13) {
            devExts_.push_back("VK_KHR_dynamic_rendering");
            devExts_.push_back("VK_KHR_synchronization2");
        }
        if (hasDeviceExtension("VK_KHR_portability_subset")) devExts_.push_back("VK_KHR_portability_subset");

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
        f12.pNext = core13 ? static_cast<void*>(&f13) : static_cast<void*>(&dyn);

        const float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = uint32_t(queueFamily_);
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pNext = &f12;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = uint32_t(devExts_.size());
        dci.ppEnabledExtensionNames = devExts_.data();
        CHECK(vkCreateDevice(gpu_, &dci, nullptr, &device_) == VK_SUCCESS, "vkCreateDevice");
        vkGetDeviceQueue(device_, uint32_t(queueFamily_), 0, &queue_);
        // Static NVRHI does not initialise vulkan.hpp's dispatcher itself.
        VULKAN_HPP_DEFAULT_DISPATCHER.init(static_cast<PFN_vkGetInstanceProcAddr>(&::vkGetInstanceProcAddr));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance_));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(device_));

        nvrhi::vulkan::DeviceDesc dd;
        dd.errorCB = &g_log;
        dd.instance = instance_;
        dd.physicalDevice = gpu_;
        dd.device = device_;
        dd.graphicsQueue = queue_;
        dd.graphicsQueueIndex = queueFamily_;
        dd.instanceExtensions = instExts_.data();
        dd.numInstanceExtensions = instExts_.size();
        dd.deviceExtensions = devExts_.data();
        dd.numDeviceExtensions = devExts_.size();
        nvrhiVk_ = nvrhi::vulkan::createDevice(dd);
        CHECK(nvrhiVk_, "nvrhi::vulkan::createDevice");
        nvrhi_ = validate ? nvrhi::validation::createValidationLayer(nvrhiVk_) : nvrhi::DeviceHandle(nvrhiVk_);

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& s : acquireSem_) vkCreateSemaphore(device_, &sci, nullptr, &s);
        return createSwapchain();
    }

    nvrhi::IDevice* device() override { return nvrhi_; }
    const char* shaderExt() const override { return ".spv"; }
    std::string adapterName() const override { return std::string("Vulkan: ") + props_.deviceName; }

    nvrhi::IFramebuffer* beginFrame() override {
        VkSemaphore sem = acquireSem_[frame_ % kFrames];
        VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, sem, VK_NULL_HANDLE, &imageIndex_);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return beginFrame(); }
        // The next submission waits until the image is really free.
        nvrhiVk_->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, sem, 0);
        return framebuffers_[imageIndex_];
    }

    void present() override {
        VkSemaphore done = presentSem_[imageIndex_];
        nvrhiVk_->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, done, 0);
        // An empty submit carries the signal (NVRHI attaches pending semaphores
        // to its next submission).
        auto cl = nvrhi_->createCommandList();
        cl->open(); cl->close();
        nvrhi_->executeCommandList(cl);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &imageIndex_;
        VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) recreateSwapchain();
        ++frame_;
        // Spike-level pacing: keep at most one frame in flight.
        nvrhi_->waitForIdle();
        nvrhi_->runGarbageCollection();
    }

    void shutdown() override {
        if (nvrhi_) nvrhi_->waitForIdle();
        destroySwapchain();
        for (auto& s : acquireSem_) if (s) vkDestroySemaphore(device_, s, nullptr);
        nvrhi_ = nullptr;
        nvrhiVk_ = nullptr;
        if (device_) vkDestroyDevice(device_, nullptr);
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

private:
    static constexpr int kFrames = 3;

    bool hasDeviceExtension(const char* name) const {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> e(n);
        vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, e.data());
        for (auto& x : e) if (std::strcmp(x.extensionName, name) == 0) return true;
        return false;
    }

    bool createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        extent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
                                                         : VkExtent2D{uint32_t(w), uint32_t(h)};
        VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sc.surface = surface_;
        sc.minImageCount = std::max(caps.minImageCount, 3u);
        if (caps.maxImageCount) sc.minImageCount = std::min(sc.minImageCount, caps.maxImageCount);
        sc.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        sc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        sc.imageExtent = extent_;
        sc.imageArrayLayers = 1;
        sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sc.preTransform = caps.currentTransform;
        sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // always available (vsync)
        sc.clipped = VK_TRUE;
        CHECK(vkCreateSwapchainKHR(device_, &sc, nullptr, &swapchain_) == VK_SUCCESS, "vkCreateSwapchainKHR");

        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
        std::vector<VkImage> images(n);
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, images.data());
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkImage img : images) {
            nvrhi::TextureDesc td;
            td.width = extent_.width;
            td.height = extent_.height;
            td.format = nvrhi::Format::BGRA8_UNORM;
            td.debugName = "swapchain";
            td.isRenderTarget = true;
            td.initialState = nvrhi::ResourceStates::Present;
            td.keepInitialState = true;
            auto tex = nvrhi_->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(img), td);
            framebuffers_.push_back(nvrhi_->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tex)));
            textures_.push_back(tex);
            VkSemaphore s;
            vkCreateSemaphore(device_, &sci, nullptr, &s);
            presentSem_.push_back(s);
        }
        return true;
    }

    void destroySwapchain() {
        framebuffers_.clear();
        textures_.clear();
        for (VkSemaphore s : presentSem_) vkDestroySemaphore(device_, s, nullptr);
        presentSem_.clear();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    void recreateSwapchain() {
        nvrhi_->waitForIdle();
        destroySwapchain();
        createSwapchain();
    }

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    int queueFamily_ = -1;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    uint32_t imageIndex_ = 0;
    uint64_t frame_ = 0;
    VkSemaphore acquireSem_[kFrames]{};
    std::vector<VkSemaphore> presentSem_;
    std::vector<const char*> instExts_, devExts_;
    nvrhi::vulkan::DeviceHandle nvrhiVk_;
    nvrhi::DeviceHandle nvrhi_;
    std::vector<nvrhi::TextureHandle> textures_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;
};

#ifdef _WIN32
// ── Direct3D 12 ──────────────────────────────────────────────────────────────
class D3D12Backend final : public Backend {
public:
    bool init(GLFWwindow* window, bool validate) override {
        // The D3D12/DXGI debug layers need the optional Windows "Graphics
        // Tools" feature; without it validation is skipped, not fatal.
        bool debugLayer = false;
        if (validate) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) { dbg->EnableDebugLayer(); debugLayer = true; }
            else std::fprintf(stderr, "D3D12 debug layer unavailable (install Graphics Tools)\n");
        }
        if (FAILED(CreateDXGIFactory2(debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory_))))
            CHECK(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_))), "CreateDXGIFactory2");
        CHECK(SUCCEEDED(factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&adapter_))), "EnumAdapter");
        DXGI_ADAPTER_DESC1 ad;
        adapter_->GetDesc1(&ad);
        char name[128];
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name), nullptr, nullptr);
        name_ = std::string("D3D12: ") + name;

        std::fprintf(stderr, "dx12: creating device\n");
        CHECK(SUCCEEDED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_))),
              "D3D12CreateDevice");
        std::fprintf(stderr, "dx12: device created\n");
        if (debugLayer) {
            // Print debug-layer messages instead of relying on a debugger.
            ComPtr<ID3D12InfoQueue1> iq;
            if (SUCCEEDED(device_.As(&iq))) {
                DWORD cookie = 0;
                iq->RegisterMessageCallback(
                    [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY sev, D3D12_MESSAGE_ID id,
                       LPCSTR desc, void*) { std::fprintf(stderr, "[d3d12 %d #%d] %s\n", int(sev), int(id), desc); },
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
            } else {
                std::fprintf(stderr, "dx12: ID3D12InfoQueue1 unavailable\n");
            }
        }
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CHECK(SUCCEEDED(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_))), "CreateCommandQueue");

        nvrhi::d3d12::DeviceDesc dd;
        dd.errorCB = &g_log;
        dd.pDevice = device_.Get();
        dd.pGraphicsCommandQueue = queue_.Get();
        nvrhi_ = nvrhi::d3d12::createDevice(dd);
        CHECK(nvrhi_, "nvrhi::d3d12::createDevice");
        if (validate) nvrhi_ = nvrhi::validation::createValidationLayer(nvrhi_);

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = UINT(w);
        sd.Height = UINT(h);
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kBuffers;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> sc1;
        CHECK(SUCCEEDED(factory_->CreateSwapChainForHwnd(queue_.Get(), glfwGetWin32Window(window), &sd,
                                                         nullptr, nullptr, &sc1)), "CreateSwapChainForHwnd");
        sc1.As(&swapchain_);

        for (UINT i = 0; i < kBuffers; ++i) {
            ComPtr<ID3D12Resource> buf;
            swapchain_->GetBuffer(i, IID_PPV_ARGS(&buf));
            nvrhi::TextureDesc td;
            td.width = sd.Width;
            td.height = sd.Height;
            td.format = nvrhi::Format::BGRA8_UNORM;
            td.debugName = "swapchain";
            td.isRenderTarget = true;
            td.initialState = nvrhi::ResourceStates::Present;
            td.keepInitialState = true;
            auto tex = nvrhi_->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource,
                                                            nvrhi::Object(buf.Get()), td);
            framebuffers_.push_back(nvrhi_->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tex)));
            buffers_.push_back(buf);
        }
        return true;
    }

    nvrhi::IDevice* device() override { return nvrhi_; }
    const char* shaderExt() const override { return ".dxil"; }
    std::string adapterName() const override { return name_; }
    nvrhi::IFramebuffer* beginFrame() override {
        return framebuffers_[swapchain_->GetCurrentBackBufferIndex()];
    }
    void present() override {
        swapchain_->Present(1, 0);
        nvrhi_->waitForIdle();
        nvrhi_->runGarbageCollection();
    }
    void shutdown() override {
        if (nvrhi_) nvrhi_->waitForIdle();
        // NVRHI only tracks its own submissions; DXGI's Present work is on the
        // same queue, so fence the queue before releasing the back buffers.
        waitQueueIdle();
        framebuffers_.clear();
        nvrhi_ = nullptr;
        buffers_.clear();
        swapchain_.Reset();
    }

    void waitQueueIdle() {
        if (!device_ || !queue_) return;
        ComPtr<ID3D12Fence> fence;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue_->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, ev);
        WaitForSingleObject(ev, INFINITE);
        CloseHandle(ev);
    }

private:
    static constexpr UINT kBuffers = 3;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;
    nvrhi::DeviceHandle nvrhi_;
    std::string name_;
};
#endif

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string api = "vulkan";
    bool validate = false;
    int maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--api=", 0) == 0) api = a.substr(6);
        else if (a == "--validate") validate = true;
        else if (a.rfind("--frames=", 0) == 0) maxFrames = std::stoi(a.substr(9));
    }

    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "GLFW error 0x%x: %s\n", code, desc ? desc : "");
    });
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "nvrhi spike", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "window failed\n"); return 1; }

    std::unique_ptr<Backend> backend;
    if (api == "vulkan") backend = std::make_unique<VulkanBackend>();
#ifdef _WIN32
    else if (api == "dx12") backend = std::make_unique<D3D12Backend>();
#endif
    if (!backend) { std::fprintf(stderr, "unsupported api %s\n", api.c_str()); return 1; }
    if (!backend->init(window, validate)) return 1;
    std::printf("%s\n", backend->adapterName().c_str());

    nvrhi::IDevice* dev = backend->device();
    const std::string dir = SPIKE_SHADER_DIR;
    auto vsBin = readFile(dir + "/triangle_vs" + backend->shaderExt());
    auto psBin = readFile(dir + "/triangle_ps" + backend->shaderExt());
    if (vsBin.empty() || psBin.empty()) return 1;
    auto vs = dev->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("main_vs"),
                                vsBin.data(), vsBin.size());
    auto ps = dev->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("main_ps"),
                                psBin.data(), psBin.size());

    nvrhi::GraphicsPipelineHandle pipeline;
    auto cl = dev->createCommandList();
    int frames = 0;
    while (!glfwWindowShouldClose(window) && (maxFrames == 0 || frames < maxFrames)) {
        glfwPollEvents();
        nvrhi::IFramebuffer* fb = backend->beginFrame();
        if (!pipeline) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.VS = vs;
            pd.PS = ps;
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
            pipeline = dev->createGraphicsPipeline(pd, fb->getFramebufferInfo());
            if (!pipeline) { std::fprintf(stderr, "pipeline failed\n"); return 1; }
        }
        cl->open();
        nvrhi::utils::ClearColorAttachment(cl, fb, 0, nvrhi::Color(0.1f, 0.2f, 0.3f, 1.0f));
        nvrhi::GraphicsState gs;
        gs.pipeline = pipeline;
        gs.framebuffer = fb;
        const auto& info = fb->getFramebufferInfo();
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(info.width), float(info.height)));
        cl->setGraphicsState(gs);
        cl->draw(nvrhi::DrawArguments().setVertexCount(3).setInstanceCount(4));
        cl->close();
        dev->executeCommandList(cl);
        backend->present();
        ++frames;
    }
    std::printf("rendered %d frames OK\n", frames);
    pipeline = nullptr;
    cl = nullptr;
    vs = nullptr;
    ps = nullptr;
    backend->shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
