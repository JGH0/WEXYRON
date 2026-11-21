// Final composite for the NVRHI backends: the HUD layer over the HUD-less
// scene. The UI layer holds premultiplied colour (see the UI pipelines in
// nvrhi_backend.cpp), so "over" is ui.rgb + scene.rgb * (1 - ui.a), which is
// exactly what drawing the UI straight onto the scene gives.
//
// The scene and UI stay separate because frame generation needs the scene
// without the HUD; the SDKs composite the HUD onto generated frames.
//
// Compiled offline by tools/compile_shaders.py (dxc).

Texture2D<float4> uScene : register(t0);
Texture2D<float4> uUi    : register(t1);

struct VSOutput
{
    float4 position : SV_Position;
};

// One triangle covering the screen (no vertex buffer).
VSOutput main_vs(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float4 main_ps(VSOutput i) : SV_Target0
{
    int2 pixel = int2(i.position.xy);
    float4 scene = uScene.Load(int3(pixel, 0));
    float4 ui = uUi.Load(int3(pixel, 0));
    return float4(ui.rgb + scene.rgb * (1.0 - ui.a), 1.0);
}
