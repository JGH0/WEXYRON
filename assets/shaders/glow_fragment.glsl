#version 330 core

flat in vec2 vAtlas;   // x = texture-array layer, y = textured flag
in vec2 vUv;
in vec4 vColor;
in vec3 vTint;
in float vShade;

out vec4 FragColor;

uniform sampler2DArray uTextures;  // map materials (world + UI colours)
uniform sampler2D uFont;           // 3x5 glyph atlas, native resolution

// vAtlas.y selects the sampler: 0 = flat colour, 1 = map texture, 2 = font.
void main() {
    int mode = int(vAtlas.y + 0.5);
    if (mode == 2) {
        // Text: crisp 1:1 glyph sampling, white pixels tinted by the instance.
        vec4 tex = texture(uFont, vUv);
        FragColor = vec4(tex.rgb * vColor.rgb, tex.a * vColor.a);
    } else if (mode == 1) {
        vec4 tex = texture(uTextures, vec3(vUv, vAtlas.x));
        FragColor = vec4(tex.rgb * vTint * vShade, tex.a * vColor.a);
    } else {
        FragColor = vec4(vColor.rgb * vShade, vColor.a);
    }
    if (FragColor.a <= 0.002) discard;
}
