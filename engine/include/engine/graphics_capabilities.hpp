/**
 * @file graphics_capabilities.hpp
 * @brief What the current OS and graphics hardware can actually run.
 *
 * The settings menu uses this to hide (grey out) options that would only fall
 * back silently on this machine: the Vulkan renderer when this build or the
 * installed driver cannot provide it, and frame-generation technologies that
 * need another operating system, GPU vendor or graphics API.
 *
 * The probe is done once (on first use) and cached. It only reads information;
 * it creates no window and leaves no Vulkan object behind.
 */

#ifndef CUBE_ENGINE_GRAPHICS_CAPABILITIES_HPP
#define CUBE_ENGINE_GRAPHICS_CAPABILITIES_HPP

#include <cstdint>
#include <string>
#include <vector>

/// One graphics adapter found by the platform probe.
struct GraphicsAdapterInfo {
    std::string name;       ///< device name, e.g. "NVIDIA GeForce RTX 4070"
    std::string vendor;     ///< "NVIDIA", "AMD", "Intel", ...
    uint32_t vendorId = 0;  ///< Vulkan/Direct3D vendor id (0 = unknown)
    bool discrete = false;  ///< dedicated GPU (best adapter for gaming)
};

/// Runtime capabilities relevant to VIDEO > RENDERER and FRAME GENERATION.
struct GraphicsCapabilities {
    std::string os;                 ///< "Windows", "Linux" or "macOS"
    bool vulkanBackendBuilt = false;///< this binary contains the Vulkan backend
    bool vulkanAvailable = false;   ///< a suitable Vulkan GPU/driver was found
    std::string vulkanNote;         ///< why Vulkan is unavailable (or "Available")

    std::vector<GraphicsAdapterInfo> adapters;
    std::string primaryGpu;         ///< best adapter name, empty when unknown
    bool hasNvidia = false;
    bool hasAmd = false;
    bool hasIntel = false;
    /// DLSS frame generation needs an RTX 40 series or newer.
    bool hasNvidiaRtx40 = false;
    /// Multi-frame generation (3X/4X) needs an RTX 50 series.
    bool hasNvidiaRtx50 = false;

    // Frame-generation technologies this build + OS + GPU can run.
    bool frameGenFsr = false;
    bool frameGenDlss = false;
    bool frameGenXess = false;
    bool dlssMultiFrame = false;
    std::string fsrNote;   ///< why FSR is unavailable (empty when it works)
    std::string dlssNote;  ///< why DLSS is unavailable (empty when it works)
    std::string xessNote;  ///< why XeSS is unavailable (empty when it works)

    /// At least one technology can generate frames.
    bool frameGenAvailable() const { return frameGenFsr || frameGenDlss || frameGenXess; }
    /// AUTO has something to pick from.
    bool frameGenAuto() const { return frameGenAvailable(); }
};

/// Probe the OS and graphics hardware (cached).
const GraphicsCapabilities& graphicsCapabilities();

#endif // CUBE_ENGINE_GRAPHICS_CAPABILITIES_HPP
