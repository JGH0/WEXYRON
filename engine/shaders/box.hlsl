// Instanced box/quad shader for the NVRHI backends (Vulkan now, D3D12 later).
// HLSL port of assets/shaders/glow_vertex.glsl + glow_fragment.glsl: keep the
// colour maths in sync, the OpenGL backend still uses the GLSL version.
//
// Two pixel shaders:
//   main_ps     UI layer: colour only (premultiplied-alpha blending is set up
//               by the pipeline, see nvrhi_backend.cpp).
//   main_ps_mv  scene: colour + motion vector (frame generation input).
//
// Compiled offline by tools/compile_shaders.py (dxc) into
// engine/src/backends/nvrhi/shaders/*.h.

cbuffer FrameBlock : register(b0)
{
    float4x4 uViewProjection;       // GL-style clip space (z in [-w, w])
    float4x4 uPrevViewProjection;   // same camera, previous frame (motion vectors)
};

Texture2DArray uTextures : register(t0);  // map materials (world + UI colours)
Texture2D      uFont     : register(t1);  // 3x5 glyph atlas, native resolution
SamplerState   sTextures : register(s0);  // nearest, repeat
SamplerState   sFont     : register(s1);  // nearest, clamp

struct VSInput
{
    // Unit quad in the XY plane, centred (the GL shader also reads a corner
    // attribute with the same values; it is unused, so it is not bound here).
    [[vk::location(0)]] float2 aPos    : POSITION0;   // (-0.5..0.5)
    // Instance data
    [[vk::location(1)]] float3 iOffset : TEXCOORD1;   // face centre
    [[vk::location(2)]] float3 iRight  : TEXCOORD2;   // half-extent along x
    [[vk::location(3)]] float3 iUp     : TEXCOORD3;   // half-extent along y
    [[vk::location(4)]] float3 iNormal : TEXCOORD4;   // outward face normal
    [[vk::location(5)]] float4 iColor  : TEXCOORD5;   // rgb + alpha
    [[vk::location(6)]] float4 iUv     : TEXCOORD6;   // tile u0, v0, u1, v1
    [[vk::location(7)]] float4 iTint   : TEXCOORD7;   // rgb tint for textured instances
    [[vk::location(8)]] float4 iAtlas  : TEXCOORD8;   // x = array layer, y = mode
};

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation float2 atlas : TEXCOORD0;   // x = array layer, y = mode
    float2 uv    : TEXCOORD1;
    float4 color : TEXCOORD2;
    float3 tint  : TEXCOORD3;
    float  shade : TEXCOORD4;
    float4 curClip  : TEXCOORD5;   // unmodified clip positions for motion vectors
    float4 prevClip : TEXCOORD6;
};

VSOutput main_vs(VSInput i)
{
    VSOutput o;
    // aPos is the [-0.5, 0.5] unit quad; iRight/iUp are half-extents.
    float3 world = i.iOffset + i.iRight * (i.aPos.x * 2.0) + i.iUp * (i.aPos.y * 2.0);
    float4 clip = mul(uViewProjection, float4(world, 1.0));
    o.curClip = clip;
    o.prevClip = mul(uPrevViewProjection, float4(world, 1.0));   // boxes do not move
    clip.z = (clip.z + clip.w) * 0.5;   // GL depth range [-w, w] -> Vulkan/D3D [0, w]
    o.position = clip;

    o.uv = lerp(i.iUv.xy, i.iUv.zw, i.aPos + 0.5);
    o.atlas = i.iAtlas.xy;

    // Lit 3D faces get directional shading; flat UI and text stay unlit
    // (iTint.a == 0), otherwise white text renders grey.
    float up = saturate(abs(i.iNormal.y));
    float facing = saturate(dot(normalize(i.iNormal), normalize(float3(0.4, 0.8, 0.5))));
    o.shade = (i.iTint.a > 0.5) ? (0.66 + 0.20 * up + 0.14 * facing) : 1.0;

    o.color = i.iColor;
    o.tint = i.iTint.rgb;
    return o;
}

// atlas.y selects the sampler: 0 = flat colour, 1 = map texture, 2 = font.
float4 shade(VSOutput i)
{
    int mode = int(i.atlas.y + 0.5);
    float4 result;
    if (mode == 2) {
        // Text: crisp 1:1 glyph sampling, white pixels tinted by the instance.
        float4 tex = uFont.Sample(sFont, i.uv);
        result = float4(tex.rgb * i.color.rgb, tex.a * i.color.a);
    } else if (mode == 1) {
        float4 tex = uTextures.Sample(sTextures, float3(i.uv, i.atlas.x));
        result = float4(tex.rgb * i.tint * i.shade, tex.a * i.color.a);
    } else {
        result = float4(i.color.rgb * i.shade, i.color.a);
    }
    if (result.a <= 0.002) discard;
    return result;
}

/// Screen-space motion from this pixel to where it was last frame, in UV units
/// (x right, y down): previous minus current position.
float2 motionVector(float4 curClip, float4 prevClip)
{
    float2 cur = curClip.xy / curClip.w;
    float2 prev = prevClip.xy / prevClip.w;
    return (prev - cur) * float2(0.5, -0.5);
}

float4 main_ps(VSOutput i) : SV_Target0
{
    return shade(i);
}

struct SceneOutput
{
    float4 color  : SV_Target0;
    float2 motion : SV_Target1;
};

SceneOutput main_ps_mv(VSOutput i)
{
    SceneOutput o;
    o.color = shade(i);
    o.motion = motionVector(i.curClip, i.prevClip);
    return o;
}
