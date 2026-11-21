// Phase 0 spike: one instanced triangle, no vertex buffers.
// Compiled offline to SPIR-V (dxc -spirv -fvk-invert-y) and DXIL (dxc).

struct VSOut
{
    float4 pos : SV_Position;
    float3 col : COLOR0;
};

VSOut main_vs(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const float2 p[3] = { float2(0.0, 0.25), float2(0.22, -0.2), float2(-0.22, -0.2) };
    const float3 c[3] = { float3(1, 0.2, 0.2), float3(0.2, 1, 0.2), float3(0.2, 0.4, 1) };
    VSOut o;
    o.pos = float4(p[vid] + float2(-0.6 + 0.4 * iid, 0.0), 0.0, 1.0);
    o.col = c[vid];
    return o;
}

float4 main_ps(VSOut i) : SV_Target0
{
    return float4(i.col, 1.0);
}
