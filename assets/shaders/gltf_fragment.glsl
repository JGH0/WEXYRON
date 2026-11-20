#version 330 core

// Cheap metallic/roughness shading for glTF materials. The game's art is
// flat-shaded boxes, so a single directional light plus ambient keeps the
// imported models consistent with the procedural world.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform vec4 uBaseColorFactor;
uniform vec3 uEmissiveFactor;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uEmissiveTexture;
uniform int uHasBaseColorTexture;
uniform int uHasEmissiveTexture;
uniform vec3 uTint;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uCameraPos;
uniform int uAlphaMode;
uniform float uAlphaCutoff;

void main() {
    vec4 base = uBaseColorFactor * vColor;
    if (uHasBaseColorTexture != 0) {
        base *= texture(uBaseColorTexture, vUV);
    }
    base.rgb *= uTint;

    if (uAlphaMode == 1 && base.a < uAlphaCutoff) discard;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);

    float ndl = max(dot(N, L), 0.0);
    float ndh = max(dot(N, H), 0.0);

    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.04, 1.0);

    vec3 diffuseColor = base.rgb * (1.0 - metallic);
    vec3 F0 = mix(vec3(0.04), base.rgb, metallic);

    float specPower = mix(128.0, 8.0, roughness);
    float spec = pow(ndh, specPower) * mix(0.35, 0.08, roughness);
    vec3 specular = F0 * spec;

    vec3 ambient = diffuseColor * 0.55;
    vec3 direct = (diffuseColor * ndl + specular * ndl) * uLightColor * 0.65;
    vec3 color = ambient + direct;

    if (uHasEmissiveTexture != 0) {
        color += texture(uEmissiveTexture, vUV).rgb * uEmissiveFactor;
    } else {
        color += uEmissiveFactor;
    }

    FragColor = vec4(color, base.a);
}
