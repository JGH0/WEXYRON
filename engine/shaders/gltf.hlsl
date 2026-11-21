// glTF 2.0 mesh shader for the NVRHI backends (Vulkan now, D3D12 later).
// HLSL port of assets/shaders/gltf_vertex.glsl + gltf_fragment.glsl: keep the
// colour maths in sync, the OpenGL backend still uses the GLSL version.
//
// Unskinned meshes and GPU linear-blend skinning with up to 32 bones; cheap
// metallic/roughness shading with one directional light plus ambient. Writes
// colour + motion vector (the previous model matrix, camera and bone palette
// give where the vertex was last frame).
//
// Compiled offline by tools/compile_shaders.py (dxc) into
// engine/src/backends/nvrhi/shaders/*.h.

// Must match GltfDrawConstants in nvrhi_backend.cpp (16-byte aligned rows).
cbuffer DrawBlock : register(b0)
{
    float4x4 uModel;
    float4x4 uViewProjection;    // GL-style clip space (z in [-w, w])
    float4x4 uPrevModel;
    float4x4 uPrevViewProjection;
    float4x4 uBones[32];
    float4x4 uPrevBones[32];
    float4   uBaseColorFactor;
    float4   uEmissiveFactor;    // xyz
    float4   uTint;              // xyz
    float4   uLightDir;          // xyz
    float4   uLightColor;        // xyz
    float4   uCameraPos;         // xyz
    float    uMetallic;
    float    uRoughness;
    float    uAlphaCutoff;
    int      uAlphaMode;         // 0 opaque, 1 mask, 2 blend
    int      uHasSkin;
    int      uHasBaseColorTexture;
    int      uHasEmissiveTexture;
    int      uPad;
};

Texture2D    uBaseColorTexture : register(t0);
Texture2D    uEmissiveTexture  : register(t1);
SamplerState sBaseColor        : register(s0);
SamplerState sEmissive         : register(s1);

struct VSInput
{
    [[vk::location(0)]] float3 aPos     : POSITION0;
    [[vk::location(1)]] float3 aNormal  : NORMAL0;
    [[vk::location(2)]] float2 aUV      : TEXCOORD0;
    [[vk::location(3)]] float4 aColor   : COLOR0;
    [[vk::location(4)]] uint4  aJoints  : BLENDINDICES0;
    [[vk::location(5)]] float4 aWeights : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : TEXCOORD3;
    float4 curClip  : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
};

float4x4 skinMatrix(float4x4 bones[32], uint4 joints, float4 weights)
{
    return bones[joints.x] * weights.x + bones[joints.y] * weights.y +
           bones[joints.z] * weights.z + bones[joints.w] * weights.w;
}

VSOutput main_vs(VSInput i)
{
    float4x4 identity = float4x4(1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1);
    float4x4 skin = identity;
    float4x4 prevSkin = identity;
    if (uHasSkin != 0) {
        skin = skinMatrix(uBones, i.aJoints, i.aWeights);
        prevSkin = skinMatrix(uPrevBones, i.aJoints, i.aWeights);
    }

    VSOutput o;
    float4 world = mul(uModel, mul(skin, float4(i.aPos, 1.0)));
    float4 prevWorld = mul(uPrevModel, mul(prevSkin, float4(i.aPos, 1.0)));
    o.worldPos = world.xyz;
    o.normal = mul((float3x3)uModel, mul((float3x3)skin, i.aNormal));
    o.uv = i.aUV;
    o.color = i.aColor;
    float4 clip = mul(uViewProjection, world);
    o.curClip = clip;
    o.prevClip = mul(uPrevViewProjection, prevWorld);
    clip.z = (clip.z + clip.w) * 0.5;   // GL depth range [-w, w] -> Vulkan/D3D [0, w]
    o.position = clip;
    return o;
}

struct SceneOutput
{
    float4 color  : SV_Target0;
    float2 motion : SV_Target1;
};

SceneOutput main_ps(VSOutput i)
{
    float4 base = uBaseColorFactor * i.color;
    if (uHasBaseColorTexture != 0) {
        base *= uBaseColorTexture.Sample(sBaseColor, i.uv);
    }
    base.rgb *= uTint.xyz;

    if (uAlphaMode == 1 && base.a < uAlphaCutoff) discard;

    float3 N = normalize(i.normal);
    float3 L = normalize(uLightDir.xyz);
    float3 V = normalize(uCameraPos.xyz - i.worldPos);
    float3 H = normalize(L + V);

    float ndl = max(dot(N, L), 0.0);
    float ndh = max(dot(N, H), 0.0);

    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.04, 1.0);

    float3 diffuseColor = base.rgb * (1.0 - metallic);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic);

    float specPower = lerp(128.0, 8.0, roughness);
    float spec = pow(ndh, specPower) * lerp(0.35, 0.08, roughness);
    float3 specular = F0 * spec;

    float3 ambient = diffuseColor * 0.55;
    float3 direct = (diffuseColor * ndl + specular * ndl) * uLightColor.xyz * 0.65;
    float3 color = ambient + direct;

    if (uHasEmissiveTexture != 0) {
        color += uEmissiveTexture.Sample(sEmissive, i.uv).rgb * uEmissiveFactor.xyz;
    } else {
        color += uEmissiveFactor.xyz;
    }

    SceneOutput o;
    o.color = float4(color, base.a);
    float2 cur = i.curClip.xy / i.curClip.w;
    float2 prev = i.prevClip.xy / i.prevClip.w;
    o.motion = (prev - cur) * float2(0.5, -0.5);   // UV units, previous - current
    return o;
}
