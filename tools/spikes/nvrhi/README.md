# NVRHI spike (Phase 0 of the Vulkan/DX12 port)

A throwaway check that NVRHI + GLFW render on every platform WEXYRON must
support before the engine is ported. It opens a window and draws four
instanced triangles from a single HLSL source compiled to SPIR-V (Vulkan) and
DXIL (D3D12).

```
nvrhi_spike [--api=vulkan|dx12] [--validate] [--frames=N]
```

`--validate` enables the Vulkan validation layer / D3D12 debug layer and the
NVRHI validation layer. `--frames=N` exits after N frames (used by CI, see
`.github/workflows/nvrhi-spike.yml`).

## Build

Requires CMake 3.20+, the Vulkan SDK (`VULKAN_SDK` set) and, on Windows, MSVC
plus the Windows SDK (its `dxc` signs DXIL).

```
cmake -S tools/spikes/nvrhi -B build-spike -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-spike
```

Software Vulkan (lavapipe): point `VK_DRIVER_FILES` at `lvp_icd.x86_64.json`.

## Findings carried into the engine port

- With NVRHI linked statically, the application must define
  `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in one source file and
  initialise `VULKAN_HPP_DEFAULT_DISPATCHER` (loader, instance, device) itself.
- NVRHI's Vulkan backend needs timeline semaphores, dynamic rendering and
  synchronization2 (core in 1.3; KHR extensions on 1.2-only drivers such as
  older MoltenVK). macOS also needs portability enumeration.
- D3D12: `nvrhi->waitForIdle()` does not cover DXGI `Present` work; fence the
  queue before releasing swapchain buffers (the debug layer reports corruption
  otherwise).
- Windows without a GPU driver has no system Vulkan loader: ship `vulkan-1.dll`
  (LunarG VulkanRT components). The loader ignores `VK_DRIVER_FILES` in
  elevated processes, so the lavapipe fallback must not depend on that
  variable alone when the game runs as administrator.
- Static NVRHI on Linux: link `nvrhi_vk` (and `nvrhi_d3d12`) before `nvrhi`.
- DXIL must come from the Windows SDK `dxc` (signed); the Vulkan SDK `dxc` is
  used for SPIR-V only.
