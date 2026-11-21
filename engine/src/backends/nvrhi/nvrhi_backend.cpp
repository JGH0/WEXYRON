/**
 * @file nvrhi_backend.cpp
 * @brief IRenderBackend on NVRHI (Vulkan now; Direct3D 12 uses the same code).
 *
 * Mirrors the OpenGL backend pass for pass so both draw the same image
 * (tests/render_capture_test.cpp compares them):
 *   clear -> map + per-frame boxes -> glTF models -> viewmodel (cleared depth)
 *   -> UI ranges in painter's order.
 * All buffer uploads are recorded first, before any draw, because a copy ends
 * the render pass.
 *
 * The shaders are engine/shaders/*.hlsl, compiled to SPIR-V headers by
 * tools/compile_shaders.py. They take the GL-style matrices the frontend
 * builds and remap depth to [0, 1] themselves.
 */

#include "engine/render_backend.hpp"

#if !defined(CUBE_WITH_VULKAN)

// Built without the NVRHI/Vulkan submodules: OpenGL only.
std::unique_ptr<IRenderBackend> createVulkanRenderBackend(GLFWwindow*) { return nullptr; }
bool vulkanBackendCompiledIn() { return false; }

#else

#include "nvrhi_device.hpp"

#include "engine/font_atlas.hpp"
#include "engine/gltf_model.hpp"

#include <nvrhi/utils.h>

#include "shaders/g_box_ps_spirv.h"
#include "shaders/g_box_vs_spirv.h"
#include "shaders/g_gltf_ps_spirv.h"
#include "shaders/g_gltf_vs_spirv.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

/// Unit quad (two triangles): position + corner, as in the GL backend.
const float kUnitQuad[] = {
    -0.5f, -0.5f,   -0.5f, -0.5f,
     0.5f, -0.5f,    0.5f, -0.5f,
     0.5f,  0.5f,    0.5f,  0.5f,
     0.5f,  0.5f,    0.5f,  0.5f,
    -0.5f,  0.5f,   -0.5f,  0.5f,
    -0.5f, -0.5f,   -0.5f, -0.5f,
};

/// Per-draw constants of gltf.hlsl (DrawBlock); every row is 16-byte aligned.
struct GltfDrawConstants {
    glm::mat4 model;
    glm::mat4 viewProjection;
    glm::mat4 bones[kGltfMaxBones];
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveFactor;
    glm::vec4 tint;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 cameraPos;
    float metallic;
    float roughness;
    float alphaCutoff;
    int32_t alphaMode;
    int32_t hasSkin;
    int32_t hasBaseColorTexture;
    int32_t hasEmissiveTexture;
    int32_t pad;
};
static_assert(sizeof(GltfDrawConstants) == 64 * (2 + kGltfMaxBones) + 16 * 6 + 32,
              "must match DrawBlock in engine/shaders/gltf.hlsl");

/// Box faces are wound counter-clockwise in GL's Y-up clip space. NVRHI sets
/// a D3D-style (Y-up) viewport on Vulkan, so the same convention applies.
constexpr bool kFrontCounterClockwise = true;

/// A GPU buffer that grows (with slack) to fit the instance data written to it.
struct DynamicBuffer {
    nvrhi::BufferHandle buffer;
    size_t capacity = 0;   ///< bytes
};

/// Box-filter mip chain for an RGBA8 image (OpenGL builds the same chain with
/// glGenerateMipmap for glTF textures).
std::vector<std::vector<unsigned char>> buildMipChain(const GltfImage& image) {
    std::vector<std::vector<unsigned char>> levels;
    levels.push_back(image.rgba);
    int w = image.width, h = image.height;
    while (image.mipmaps && (w > 1 || h > 1)) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        const std::vector<unsigned char>& src = levels.back();
        std::vector<unsigned char> dst(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                            sum += src[(static_cast<size_t>(sy) * w + sx) * 4 + c];
                        }
                    }
                    dst[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    return levels;
}

nvrhi::SamplerAddressMode addressMode(GltfWrap wrap) {
    switch (wrap) {
        case GltfWrap::ClampToEdge: return nvrhi::SamplerAddressMode::Clamp;
        case GltfWrap::MirroredRepeat: return nvrhi::SamplerAddressMode::Mirror;
        default: return nvrhi::SamplerAddressMode::Wrap;
    }
}

/// GPU copy of one GltfModel for this backend.
class NvrhiGltfData final : public GltfGpuData {
public:
    struct Primitive {
        nvrhi::BufferHandle vertexBuffer;
        nvrhi::BufferHandle indexBuffer;
        uint32_t indexCount = 0;
    };
    std::vector<std::vector<Primitive>> meshes;          ///< [mesh][primitive]
    std::vector<nvrhi::TextureHandle> textures;          ///< per image
    std::vector<nvrhi::SamplerHandle> samplers;          ///< per image
    std::vector<nvrhi::BindingSetHandle> materialSets;   ///< per material, + fallback last
    std::shared_ptr<bool> backendAlive;

    ~NvrhiGltfData() override {
        // A model that outlives the backend must not touch the destroyed
        // device: give the handles up instead of releasing them.
        if (backendAlive && *backendAlive) return;
        for (auto& mesh : meshes)
            for (auto& p : mesh) { p.vertexBuffer.Detach(); p.indexBuffer.Detach(); }
        for (auto& t : textures) t.Detach();
        for (auto& s : samplers) s.Detach();
        for (auto& b : materialSets) b.Detach();
    }
};

class NvrhiRenderBackend final : public IRenderBackend {
public:
    NvrhiRenderBackend(std::unique_ptr<NvrhiDeviceManager> manager, GLFWwindow* window)
        : manager_(std::move(manager)), window_(window) {}
    ~NvrhiRenderBackend() override { shutdown(); }

    bool init() override;
    void shutdown() override;
    void setTextureArray(const TextureArrayDesc& desc) override;
    void renderFrame(const FramePacket& frame) override;
    void present() override {
        if (frameStarted_) manager_->present();
        frameStarted_ = false;
    }
    void setVsync(bool enabled) override { if (manager_) manager_->setVsync(enabled); }
    void requestCapture() override { captureRequested_ = true; }
    bool takeCapture(CapturedImage& out) override {
        if (!captureReady_) return false;
        out = std::move(capture_);
        captureReady_ = false;
        return true;
    }
    RenderBackendInfo describe() const override {
        return manager_ ? manager_->describe() : RenderBackendInfo{"Vulkan", "?", "?", "?"};
    }

private:
    bool createPipelines(const nvrhi::FramebufferInfoEx& info);
    void ensureTargets();
    void upload(DynamicBuffer& target, const std::vector<BoxInstance>* data, const char* name);
    const NvrhiGltfData& gltfData(const GltfModel& model);
    void rebuildBoxBindingSet();

    void drawBoxes(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IBuffer* instances,
                   uint32_t first, uint32_t count);
    void drawModels(const std::vector<ModelDraw>* draws, const glm::mat4& viewProjection);
    void readBack();

    std::unique_ptr<NvrhiDeviceManager> manager_;
    GLFWwindow* window_ = nullptr;
    nvrhi::IDevice* device_ = nullptr;
    nvrhi::CommandListHandle cl_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);

    nvrhi::ShaderHandle boxVS_, boxPS_, gltfVS_, gltfPS_;
    nvrhi::InputLayoutHandle boxLayout_, gltfLayout_;
    nvrhi::BindingLayoutHandle boxBindings_, gltfBindings_;
    nvrhi::GraphicsPipelineHandle boxWorld_, boxUiOpaque_, boxUiBlend_;
    nvrhi::GraphicsPipelineHandle gltfPipelines_[2][2];   ///< [doubleSided][blend]

    nvrhi::BufferHandle quadBuffer_, frameConstants_, gltfConstants_;
    nvrhi::TextureHandle textureArray_, fontTexture_, whiteTexture_;
    nvrhi::SamplerHandle arraySampler_, fontSampler_, defaultGltfSampler_;
    nvrhi::BindingSetHandle boxSet_;

    DynamicBuffer staticBoxes_, dynamicBoxes_, viewmodelBoxes_, uiBoxes_;
    std::uint64_t uploadedStaticRevision_ = ~std::uint64_t(0);

    nvrhi::TextureHandle depth_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;   ///< per back buffer
    uint64_t targetsGeneration_ = 0;
    nvrhi::IFramebuffer* framebuffer_ = nullptr;           ///< this frame's target
    nvrhi::Viewport viewport_;
    bool frameStarted_ = false;

    bool captureRequested_ = false;
    bool captureReady_ = false;
    CapturedImage capture_;
    nvrhi::StagingTextureHandle captureStaging_;

    std::vector<GltfNodeDraw> nodeDraws_;
};

bool NvrhiRenderBackend::init() {
    if (!manager_ || !manager_->create(window_)) return false;
    device_ = manager_->device();
    *alive_ = true;
    cl_ = device_->createCommandList();

    auto shader = [&](nvrhi::ShaderType type, const char* entry, const unsigned char* code, size_t size) {
        return device_->createShader(nvrhi::ShaderDesc().setShaderType(type).setEntryName(entry), code, size);
    };
    boxVS_ = shader(nvrhi::ShaderType::Vertex, "main_vs", g_box_vs_spirv, g_box_vs_spirv_size);
    boxPS_ = shader(nvrhi::ShaderType::Pixel, "main_ps", g_box_ps_spirv, g_box_ps_spirv_size);
    gltfVS_ = shader(nvrhi::ShaderType::Vertex, "main_vs", g_gltf_vs_spirv, g_gltf_vs_spirv_size);
    gltfPS_ = shader(nvrhi::ShaderType::Pixel, "main_ps", g_gltf_ps_spirv, g_gltf_ps_spirv_size);
    if (!boxVS_ || !boxPS_ || !gltfVS_ || !gltfPS_) return false;

    // Box stream: slot 0 = unit quad, slot 1 = one BoxInstance per face.
    const uint32_t quadStride = 4 * sizeof(float);
    const uint32_t instStride = sizeof(BoxInstance);
    auto attr = [](const char* name, nvrhi::Format format, uint32_t slot, uint32_t offset,
                   uint32_t stride, bool instanced) {
        return nvrhi::VertexAttributeDesc().setName(name).setFormat(format).setBufferIndex(slot)
            .setOffset(offset).setElementStride(stride).setIsInstanced(instanced);
    };
    const nvrhi::VertexAttributeDesc boxAttrs[] = {
        attr("POSITION", nvrhi::Format::RG32_FLOAT, 0, 0, quadStride, false),
        attr("OFFSET", nvrhi::Format::RGB32_FLOAT, 1, offsetof(BoxInstance, offset), instStride, true),
        attr("RIGHT", nvrhi::Format::RGB32_FLOAT, 1, offsetof(BoxInstance, right), instStride, true),
        attr("UP", nvrhi::Format::RGB32_FLOAT, 1, offsetof(BoxInstance, up), instStride, true),
        attr("NORMAL", nvrhi::Format::RGB32_FLOAT, 1, offsetof(BoxInstance, normal), instStride, true),
        attr("COLOR", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(BoxInstance, color), instStride, true),
        attr("UV", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(BoxInstance, uv), instStride, true),
        attr("TINT", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(BoxInstance, tint), instStride, true),
        attr("ATLAS", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(BoxInstance, atlas), instStride, true),
    };
    boxLayout_ = device_->createInputLayout(boxAttrs, static_cast<uint32_t>(std::size(boxAttrs)), boxVS_);

    const uint32_t vtxStride = sizeof(GltfVertex);
    const nvrhi::VertexAttributeDesc gltfAttrs[] = {
        attr("POSITION", nvrhi::Format::RGB32_FLOAT, 0, offsetof(GltfVertex, position), vtxStride, false),
        attr("NORMAL", nvrhi::Format::RGB32_FLOAT, 0, offsetof(GltfVertex, normal), vtxStride, false),
        attr("TEXCOORD", nvrhi::Format::RG32_FLOAT, 0, offsetof(GltfVertex, uv), vtxStride, false),
        attr("COLOR", nvrhi::Format::RGBA32_FLOAT, 0, offsetof(GltfVertex, color), vtxStride, false),
        attr("JOINTS", nvrhi::Format::RGBA32_UINT, 0, offsetof(GltfVertex, joints), vtxStride, false),
        attr("WEIGHTS", nvrhi::Format::RGBA32_FLOAT, 0, offsetof(GltfVertex, weights), vtxStride, false),
    };
    gltfLayout_ = device_->createInputLayout(gltfAttrs, static_cast<uint32_t>(std::size(gltfAttrs)), gltfVS_);

    // Both shaders: b0 constants, t0/t1 textures, s0/s1 samplers.
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
    };
    boxBindings_ = device_->createBindingLayout(layout);
    gltfBindings_ = device_->createBindingLayout(layout);

    quadBuffer_ = device_->createBuffer(nvrhi::BufferDesc().setByteSize(sizeof(kUnitQuad))
        .setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer)
        .setKeepInitialState(true).setDebugName("unit quad"));
    frameConstants_ = device_->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(glm::mat4), "frame constants", 256));
    gltfConstants_ = device_->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(GltfDrawConstants), "glTF draw constants", 4096));

    arraySampler_ = device_->createSampler(nvrhi::SamplerDesc().setAllFilters(false)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
    fontSampler_ = device_->createSampler(nvrhi::SamplerDesc().setAllFilters(false)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    defaultGltfSampler_ = device_->createSampler(nvrhi::SamplerDesc().setAllFilters(true)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

    // Glyph atlas (native resolution, nearest) and a 1x1 white placeholder.
    std::vector<unsigned char> glyphs(static_cast<size_t>(font_atlas::kAtlasW) * font_atlas::kAtlasH * 4);
    font_atlas::buildAtlas(glyphs.data());
    fontTexture_ = device_->createTexture(nvrhi::TextureDesc().setWidth(font_atlas::kAtlasW)
        .setHeight(font_atlas::kAtlasH).setFormat(nvrhi::Format::RGBA8_UNORM)
        .setInitialState(nvrhi::ResourceStates::ShaderResource).setKeepInitialState(true)
        .setDebugName("glyph atlas"));
    whiteTexture_ = device_->createTexture(nvrhi::TextureDesc().setWidth(1).setHeight(1)
        .setFormat(nvrhi::Format::RGBA8_UNORM).setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true).setDebugName("white"));
    const unsigned char white[4] = {255, 255, 255, 255};

    cl_->open();
    cl_->writeBuffer(quadBuffer_, kUnitQuad, sizeof(kUnitQuad));
    cl_->writeTexture(fontTexture_, 0, 0, glyphs.data(), static_cast<size_t>(font_atlas::kAtlasW) * 4);
    cl_->writeTexture(whiteTexture_, 0, 0, white, 4);
    cl_->close();
    device_->executeCommandList(cl_);

    // Until a map provides textures: one checker-less layer.
    TextureArrayDesc empty;
    empty.layers.assign(1, nullptr);
    setTextureArray(empty);
    return true;
}

void NvrhiRenderBackend::setTextureArray(const TextureArrayDesc& desc) {
    if (!device_) return;
    const uint32_t layers = std::max<uint32_t>(1, static_cast<uint32_t>(desc.layers.size()));
    textureArray_ = device_->createTexture(nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2DArray)
        .setWidth(desc.width).setHeight(desc.height).setArraySize(layers)
        .setFormat(nvrhi::Format::RGBA8_UNORM).setInitialState(nvrhi::ResourceStates::ShaderResource)
        .setKeepInitialState(true).setDebugName("material array"));
    cl_->open();
    for (uint32_t layer = 0; layer < desc.layers.size(); ++layer) {
        if (!desc.layers[layer]) continue;
        cl_->writeTexture(textureArray_, layer, 0, desc.layers[layer], static_cast<size_t>(desc.width) * 4);
    }
    cl_->close();
    device_->executeCommandList(cl_);
    rebuildBoxBindingSet();
}

void NvrhiRenderBackend::rebuildBoxBindingSet() {
    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, frameConstants_),
        nvrhi::BindingSetItem::Texture_SRV(0, textureArray_),
        nvrhi::BindingSetItem::Texture_SRV(1, fontTexture_),
        nvrhi::BindingSetItem::Sampler(0, arraySampler_),
        nvrhi::BindingSetItem::Sampler(1, fontSampler_),
    };
    boxSet_ = device_->createBindingSet(set, boxBindings_);
}

bool NvrhiRenderBackend::createPipelines(const nvrhi::FramebufferInfoEx& info) {
    auto pipeline = [&](nvrhi::IShader* vs, nvrhi::IShader* ps, nvrhi::IInputLayout* layout,
                        nvrhi::IBindingLayout* bindings, bool depth, bool depthWrite,
                        nvrhi::RasterCullMode cull, bool blend) {
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vs;
        desc.PS = ps;
        desc.inputLayout = layout;
        desc.bindingLayouts = {bindings};
        desc.renderState.rasterState.setCullMode(cull).setFrontCounterClockwise(kFrontCounterClockwise);
        desc.renderState.depthStencilState.setDepthTestEnable(depth).setDepthWriteEnable(depthWrite)
            .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual).setStencilEnable(false);
        auto& target = desc.renderState.blendState.targets[0];
        target.setBlendEnable(blend)
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::SrcAlpha).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
        return device_->createGraphicsPipeline(desc, info);
    };
    using Cull = nvrhi::RasterCullMode;
    // World: depth-tested, depth-writing, back faces culled, no blending
    // (both box streams are solid geometry, as in the GL backend).
    boxWorld_ = pipeline(boxVS_, boxPS_, boxLayout_, boxBindings_, true, true, Cull::Back, false);
    // UI: painter's order, no depth, single-sided planes are never culled.
    boxUiOpaque_ = pipeline(boxVS_, boxPS_, boxLayout_, boxBindings_, false, false, Cull::None, false);
    boxUiBlend_ = pipeline(boxVS_, boxPS_, boxLayout_, boxBindings_, false, false, Cull::None, true);
    for (int doubleSided = 0; doubleSided < 2; ++doubleSided) {
        for (int blend = 0; blend < 2; ++blend) {
            // BLEND materials are drawn without depth writes (as in GL).
            gltfPipelines_[doubleSided][blend] =
                pipeline(gltfVS_, gltfPS_, gltfLayout_, gltfBindings_, true, blend == 0,
                         doubleSided ? Cull::None : Cull::Back, blend == 1);
        }
    }
    return boxWorld_ && boxUiOpaque_ && boxUiBlend_ && gltfPipelines_[0][0] && gltfPipelines_[1][1];
}

void NvrhiRenderBackend::ensureTargets() {
    if (targetsGeneration_ != manager_->swapchainGeneration() || !depth_) {
        targetsGeneration_ = manager_->swapchainGeneration();
        framebuffers_.assign(manager_->backBufferCount(), nullptr);
        depth_ = device_->createTexture(nvrhi::TextureDesc().setWidth(manager_->width())
            .setHeight(manager_->height()).setFormat(nvrhi::Format::D32).setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::DepthWrite).setKeepInitialState(true)
            .setClearValue(nvrhi::Color(1.0f)).setUseClearValue(true).setDebugName("depth"));
        captureStaging_ = nullptr;
    }
    nvrhi::FramebufferHandle& fb = framebuffers_[manager_->backBufferIndex()];
    if (!fb) {
        fb = device_->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(manager_->backBuffer()).setDepthAttachment(depth_));
    }
    framebuffer_ = fb;
    if (!boxWorld_) createPipelines(framebuffer_->getFramebufferInfo());
}

void NvrhiRenderBackend::upload(DynamicBuffer& target, const std::vector<BoxInstance>* data,
                                const char* name) {
    if (!data || data->empty()) return;
    const size_t bytes = data->size() * sizeof(BoxInstance);
    if (bytes > target.capacity) {
        // Grow with slack so occasional spikes don't reallocate every frame.
        target.capacity = bytes * 2;
        target.buffer = device_->createBuffer(nvrhi::BufferDesc().setByteSize(target.capacity)
            .setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer)
            .setKeepInitialState(true).setDebugName(name));
        if (&target == &staticBoxes_) uploadedStaticRevision_ = ~std::uint64_t(0);
    }
    cl_->writeBuffer(target.buffer, data->data(), bytes);
}

const NvrhiGltfData& NvrhiRenderBackend::gltfData(const GltfModel& model) {
    if (auto* existing = dynamic_cast<NvrhiGltfData*>(model.gpuData())) return *existing;

    auto data = std::make_unique<NvrhiGltfData>();
    data->backendAlive = alive_;
    for (const GltfImage& image : model.images()) {
        const auto levels = buildMipChain(image);
        nvrhi::TextureHandle texture;
        if (image.width > 0 && image.height > 0 && !image.rgba.empty()) {
            texture = device_->createTexture(nvrhi::TextureDesc().setWidth(image.width).setHeight(image.height)
                .setMipLevels(static_cast<uint32_t>(levels.size())).setFormat(nvrhi::Format::RGBA8_UNORM)
                .setInitialState(nvrhi::ResourceStates::ShaderResource).setKeepInitialState(true)
                .setDebugName("glTF texture"));
            int w = image.width;
            for (uint32_t mip = 0; mip < levels.size(); ++mip) {
                cl_->writeTexture(texture, 0, mip, levels[mip].data(), static_cast<size_t>(w) * 4);
                w = std::max(1, w / 2);
            }
        }
        data->textures.push_back(texture);
        data->samplers.push_back(device_->createSampler(nvrhi::SamplerDesc()
            .setMinFilter(image.minFilter == GltfFilter::Linear)
            .setMagFilter(image.magFilter == GltfFilter::Linear)
            .setMipFilter(image.mipFilter == GltfFilter::Linear)
            .setAddressU(addressMode(image.wrapS)).setAddressV(addressMode(image.wrapT))
            .setAddressW(nvrhi::SamplerAddressMode::Wrap)));
    }

    data->meshes.resize(model.meshes().size());
    for (size_t m = 0; m < model.meshes().size(); ++m) {
        for (const GltfPrimitive& source : model.meshes()[m].primitives) {
            NvrhiGltfData::Primitive p;
            p.indexCount = static_cast<uint32_t>(source.indices.size());
            const size_t vbytes = source.vertices.size() * sizeof(GltfVertex);
            const size_t ibytes = source.indices.size() * sizeof(uint32_t);
            if (vbytes && ibytes) {
                p.vertexBuffer = device_->createBuffer(nvrhi::BufferDesc().setByteSize(vbytes)
                    .setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer)
                    .setKeepInitialState(true).setDebugName("glTF vertices"));
                p.indexBuffer = device_->createBuffer(nvrhi::BufferDesc().setByteSize(ibytes)
                    .setIsIndexBuffer(true).setInitialState(nvrhi::ResourceStates::IndexBuffer)
                    .setKeepInitialState(true).setDebugName("glTF indices"));
                cl_->writeBuffer(p.vertexBuffer, source.vertices.data(), vbytes);
                cl_->writeBuffer(p.indexBuffer, source.indices.data(), ibytes);
            }
            data->meshes[m].push_back(p);
        }
    }

    // One binding set per material (+ one for the default material).
    auto texture = [&](int image) -> nvrhi::ITexture* {
        return (image >= 0 && static_cast<size_t>(image) < data->textures.size() && data->textures[image])
                   ? data->textures[image].Get() : whiteTexture_.Get();
    };
    auto sampler = [&](int image) -> nvrhi::ISampler* {
        return (image >= 0 && static_cast<size_t>(image) < data->samplers.size())
                   ? data->samplers[image].Get() : defaultGltfSampler_.Get();
    };
    std::vector<GltfMaterial> materials = model.materials();
    materials.push_back(GltfModel::defaultMaterial());
    for (const GltfMaterial& material : materials) {
        const int base = material.hasBaseColorTexture ? material.baseColorTexture : -1;
        const int emissive = material.hasEmissiveTexture ? material.emissiveTexture : -1;
        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, gltfConstants_),
            nvrhi::BindingSetItem::Texture_SRV(0, texture(base)),
            nvrhi::BindingSetItem::Texture_SRV(1, texture(emissive)),
            nvrhi::BindingSetItem::Sampler(0, sampler(base)),
            nvrhi::BindingSetItem::Sampler(1, sampler(emissive)),
        };
        data->materialSets.push_back(device_->createBindingSet(set, gltfBindings_));
    }

    model.setGpuData(std::move(data));
    return *static_cast<NvrhiGltfData*>(model.gpuData());
}

void NvrhiRenderBackend::drawBoxes(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IBuffer* instances,
                                   uint32_t first, uint32_t count) {
    if (!pipeline || !instances || count == 0) return;
    nvrhi::GraphicsState state;
    state.pipeline = pipeline;
    state.framebuffer = framebuffer_;
    state.viewport.addViewportAndScissorRect(viewport_);
    state.bindings = {boxSet_};
    state.vertexBuffers = {
        nvrhi::VertexBufferBinding().setBuffer(quadBuffer_).setSlot(0).setOffset(0),
        nvrhi::VertexBufferBinding().setBuffer(instances).setSlot(1).setOffset(0),
    };
    cl_->setGraphicsState(state);
    cl_->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(count).setStartInstanceLocation(first));
}

void NvrhiRenderBackend::drawModels(const std::vector<ModelDraw>* draws, const glm::mat4& viewProjection) {
    if (!draws) return;
    GltfDrawConstants c{};
    c.viewProjection = viewProjection;
    c.lightDir = glm::vec4(glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f)), 0.0f);
    c.lightColor = glm::vec4(glm::vec3(0.95f), 0.0f);
    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
    const glm::vec4 camera = inverseViewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    c.cameraPos = glm::vec4(camera.w != 0.0f ? glm::vec3(camera) / camera.w : glm::vec3(0.0f), 1.0f);
    for (glm::mat4& bone : c.bones) bone = glm::mat4(1.0f);

    for (const ModelDraw& d : *draws) {
        if (!d.model || !d.instance || !d.model->isLoaded()) continue;
        const GltfModel& model = *d.model;
        const NvrhiGltfData& gpu = *static_cast<const NvrhiGltfData*>(model.gpuData());
        model.collectNodeDraws(*d.instance, d.transform, nodeDraws_);
        c.tint = glm::vec4(d.tint, 0.0f);

        const std::vector<GltfMaterial>& materials = model.materials();
        for (const GltfNodeDraw& node : nodeDraws_) {
            c.model = node.model;
            c.hasSkin = node.skinned ? 1 : 0;
            if (node.skinned) std::copy(node.bones.begin(), node.bones.end(), c.bones);

            const auto& primitives = model.meshes()[static_cast<size_t>(node.mesh)].primitives;
            const auto& gpuPrimitives = gpu.meshes[static_cast<size_t>(node.mesh)];
            for (size_t p = 0; p < primitives.size(); ++p) {
                const NvrhiGltfData::Primitive& prim = gpuPrimitives[p];
                if (!prim.vertexBuffer || prim.indexCount == 0) continue;
                const int index = primitives[p].material;
                const bool hasMaterial = index >= 0 && static_cast<size_t>(index) < materials.size();
                const GltfMaterial material = hasMaterial ? materials[static_cast<size_t>(index)]
                                                          : GltfModel::defaultMaterial();
                const size_t setIndex = hasMaterial ? static_cast<size_t>(index) : materials.size();

                c.baseColorFactor = material.baseColorFactor;
                c.emissiveFactor = glm::vec4(material.emissiveFactor, 0.0f);
                c.metallic = material.metallicFactor;
                c.roughness = material.roughnessFactor;
                c.alphaMode = material.alphaMode;
                c.alphaCutoff = material.alphaCutoff;
                c.hasBaseColorTexture = material.hasBaseColorTexture ? 1 : 0;
                c.hasEmissiveTexture = material.hasEmissiveTexture ? 1 : 0;
                cl_->writeBuffer(gltfConstants_, &c, sizeof(c));

                nvrhi::GraphicsState state;
                state.pipeline = gltfPipelines_[material.doubleSided ? 1 : 0][material.alphaMode == 2 ? 1 : 0];
                state.framebuffer = framebuffer_;
                state.viewport.addViewportAndScissorRect(viewport_);
                state.bindings = {gpu.materialSets[setIndex]};
                state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(prim.vertexBuffer).setSlot(0)};
                state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(prim.indexBuffer)
                                        .setFormat(nvrhi::Format::R32_UINT);
                cl_->setGraphicsState(state);
                cl_->drawIndexed(nvrhi::DrawArguments().setVertexCount(prim.indexCount));
            }
        }
    }
}

void NvrhiRenderBackend::renderFrame(const FramePacket& frame) {
    frameStarted_ = false;
    if (!device_ || !manager_->beginFrame()) return;   // minimised / swapchain lost
    frameStarted_ = true;
    ensureTargets();

    // The packet's viewport is in GL window coordinates (origin bottom-left).
    const float fbH = static_cast<float>(manager_->height());
    const float top = fbH - static_cast<float>(frame.viewportY + frame.viewportH);
    viewport_ = nvrhi::Viewport(static_cast<float>(frame.viewportX),
                                static_cast<float>(frame.viewportX + frame.viewportW),
                                top, top + static_cast<float>(frame.viewportH), 0.0f, 1.0f);

    cl_->open();

    // ── Uploads first (a copy ends the render pass) ──
    const size_t staticCount = frame.worldStatic ? frame.worldStatic->size() : 0;
    if (staticCount > 0 && (frame.staticRevision != uploadedStaticRevision_ ||
                            staticCount * sizeof(BoxInstance) > staticBoxes_.capacity)) {
        upload(staticBoxes_, frame.worldStatic, "map boxes");
        uploadedStaticRevision_ = frame.staticRevision;
    }
    upload(dynamicBoxes_, frame.worldDynamic, "frame boxes");
    upload(viewmodelBoxes_, frame.viewmodelBoxes, "viewmodel boxes");
    upload(uiBoxes_, frame.uiInstances, "ui boxes");
    for (const auto* list : {frame.worldModels, frame.viewmodelModels}) {
        if (!list) continue;
        for (const ModelDraw& d : *list) if (d.model && d.model->isLoaded()) gltfData(*d.model);
    }

    // ── Clear ──
    nvrhi::utils::ClearColorAttachment(cl_, framebuffer_, 0,
        nvrhi::Color(frame.clearColor.r, frame.clearColor.g, frame.clearColor.b, frame.clearColor.a));
    cl_->clearDepthStencilTexture(depth_, nvrhi::AllSubresources, true, 1.0f, false, 0);

    // ── World ──
    cl_->writeBuffer(frameConstants_, glm::value_ptr(frame.worldViewProjection), sizeof(glm::mat4));
    drawBoxes(boxWorld_, staticBoxes_.buffer, 0, static_cast<uint32_t>(staticCount));
    drawBoxes(boxWorld_, dynamicBoxes_.buffer, 0,
              static_cast<uint32_t>(frame.worldDynamic ? frame.worldDynamic->size() : 0));
    drawModels(frame.worldModels, frame.worldViewProjection);

    // ── First-person weapon: own camera, cleared depth ──
    const size_t viewmodelCount = frame.viewmodelBoxes ? frame.viewmodelBoxes->size() : 0;
    const bool hasViewmodelModels = frame.viewmodelModels && !frame.viewmodelModels->empty();
    if (viewmodelCount > 0 || hasViewmodelModels) {
        cl_->clearDepthStencilTexture(depth_, nvrhi::AllSubresources, true, 1.0f, false, 0);
        cl_->writeBuffer(frameConstants_, glm::value_ptr(frame.viewmodelViewProjection), sizeof(glm::mat4));
        drawBoxes(boxWorld_, viewmodelBoxes_.buffer, 0, static_cast<uint32_t>(viewmodelCount));
        drawModels(frame.viewmodelModels, frame.viewmodelViewProjection);
    }

    // ── UI in submission order ──
    if (frame.uiRanges && !frame.uiRanges->empty()) {
        cl_->writeBuffer(frameConstants_, glm::value_ptr(frame.uiViewProjection), sizeof(glm::mat4));
        for (const UiRange& range : *frame.uiRanges) {
            drawBoxes(range.blended ? boxUiBlend_ : boxUiOpaque_, uiBoxes_.buffer,
                      static_cast<uint32_t>(range.first), static_cast<uint32_t>(range.count));
        }
    }

    if (captureRequested_) {
        if (!captureStaging_) {
            nvrhi::TextureDesc desc = manager_->backBuffer()->getDesc();
            desc.isRenderTarget = false;
            desc.initialState = nvrhi::ResourceStates::CopyDest;
            desc.keepInitialState = true;
            desc.debugName = "capture";
            captureStaging_ = device_->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        }
        cl_->copyTexture(captureStaging_, nvrhi::TextureSlice(), manager_->backBuffer(), nvrhi::TextureSlice());
    }

    cl_->close();
    device_->executeCommandList(cl_);

    if (captureRequested_) readBack();
}

void NvrhiRenderBackend::readBack() {
    captureRequested_ = false;
    device_->waitForIdle();
    const nvrhi::TextureDesc& desc = captureStaging_->getDesc();
    size_t pitch = 0;
    const auto* src = static_cast<const unsigned char*>(
        device_->mapStagingTexture(captureStaging_, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
    if (!src) return;
    const bool bgra = desc.format == nvrhi::Format::BGRA8_UNORM;
    capture_.width = static_cast<int>(desc.width);
    capture_.height = static_cast<int>(desc.height);
    capture_.rgb.resize(static_cast<size_t>(desc.width) * desc.height * 3);
    for (uint32_t y = 0; y < desc.height; ++y) {
        const unsigned char* row = src + y * pitch;
        unsigned char* out = capture_.rgb.data() + static_cast<size_t>(y) * desc.width * 3;
        for (uint32_t x = 0; x < desc.width; ++x) {
            out[x * 3 + 0] = row[x * 4 + (bgra ? 2 : 0)];
            out[x * 3 + 1] = row[x * 4 + 1];
            out[x * 3 + 2] = row[x * 4 + (bgra ? 0 : 2)];
        }
    }
    device_->unmapStagingTexture(captureStaging_);
    captureReady_ = true;
}

void NvrhiRenderBackend::shutdown() {
    if (!device_) {
        if (manager_) manager_->destroy();
        return;
    }
    device_->waitForIdle();
    *alive_ = false;   // models still holding GPU copies give them up
    cl_ = nullptr;
    boxSet_ = nullptr;
    framebuffers_.clear();
    depth_ = nullptr;
    captureStaging_ = nullptr;
    boxWorld_ = boxUiOpaque_ = boxUiBlend_ = nullptr;
    for (auto& row : gltfPipelines_) for (auto& p : row) p = nullptr;
    staticBoxes_ = dynamicBoxes_ = viewmodelBoxes_ = uiBoxes_ = DynamicBuffer{};
    quadBuffer_ = frameConstants_ = gltfConstants_ = nullptr;
    textureArray_ = fontTexture_ = whiteTexture_ = nullptr;
    arraySampler_ = fontSampler_ = defaultGltfSampler_ = nullptr;
    boxBindings_ = gltfBindings_ = nullptr;
    boxLayout_ = gltfLayout_ = nullptr;
    boxVS_ = boxPS_ = gltfVS_ = gltfPS_ = nullptr;
    device_ = nullptr;
    manager_->destroy();
}

} // namespace

std::unique_ptr<IRenderBackend> createNvrhiRenderBackend(std::unique_ptr<NvrhiDeviceManager> manager,
                                                         GLFWwindow* window) {
    return std::make_unique<NvrhiRenderBackend>(std::move(manager), window);
}

std::unique_ptr<IRenderBackend> createVulkanRenderBackend(GLFWwindow* window) {
    return createNvrhiRenderBackend(createVulkanDeviceManager(), window);
}

bool vulkanBackendCompiledIn() { return true; }

#endif // CUBE_WITH_VULKAN
