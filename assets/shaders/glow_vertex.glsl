#version 330 core

// One quad, many instances. Everything the renderer used to push as solid
// per-face quads (map blocks, props, HUD bars, glyphs) is now an instance, so a
// whole frame of geometry costs a handful of draw calls.

// Unit quad in the XY plane, centred, with the corner used for UV mapping.
layout(location = 0) in vec2 aPos;      // (-0.5..0.5)
layout(location = 1) in vec2 aCorner;   // (-0.5..0.5), same values

// ── Instance data ──
layout(location = 2) in vec3 iOffset;   // face centre
layout(location = 3) in vec3 iRight;    // half-extent along screen x
layout(location = 4) in vec3 iUp;       // half-extent along screen y
layout(location = 5) in vec3 iNormal;   // outward face normal
layout(location = 6) in vec4 iColor;    // rgb + alpha
layout(location = 7) in vec4 iUv;       // tile u0, v0, u1, v1
layout(location = 8) in vec4 iTint;     // rgb tint for textured instances
layout(location = 9) in vec4 iAtlas;    // x = array layer, y = textured flag (z,w unused)

layout(std140) uniform FrameBlock {     // binding = 0
    mat4 uViewProjection;
};

flat out vec2 vAtlas;   // x = array layer, y = textured flag
out vec2 vUv;
out vec4 vColor;
out vec3 vTint;
out float vShade;

void main() {
    // aPos is the [-0.5, 0.5] unit quad; iRight/iUp are half-extents.
    vec3 world = iOffset + iRight * (aPos.x * 2.0) + iUp * (aPos.y * 2.0);
    gl_Position = uViewProjection * vec4(world, 1.0);

    // Flat, per-vertex UV: gl_Position needs no interpolation, and the earlier
    // interpolated variates duplicated glyph columns on some drivers.
    vUv    = mix(iUv.xy, iUv.zw, aPos + 0.5);
    vAtlas = iAtlas.xy;

    // Lit 3D faces get directional shading; flat UI and text stay unlit
    // (iTint.a == 0), otherwise white text renders grey.
    float up = clamp(abs(iNormal.y), 0.0, 1.0);
    float facing = clamp(dot(normalize(iNormal), normalize(vec3(0.4, 0.8, 0.5))), 0.0, 1.0);
    vShade = (iTint.a > 0.5) ? (0.66 + 0.20 * up + 0.14 * facing) : 1.0;

    vColor = iColor;
    vTint  = iTint.rgb;
}
