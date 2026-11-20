#version 330 core

// glTF 2.0 mesh shader. Supports unskinned meshes and GPU linear-blend
// skinning with up to 64 bones. Vertex colours/tint let the game recolour a
// shared model per team without extra assets.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat4 uBones[32];
uniform int uHasSkin;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;

void main() {
    mat4 skin = mat4(1.0);
    if (uHasSkin != 0) {
        skin = uBones[aJoints.x] * aWeights.x +
               uBones[aJoints.y] * aWeights.y +
               uBones[aJoints.z] * aWeights.z +
               uBones[aJoints.w] * aWeights.w;
    }

    vec4 world = uModel * skin * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(uModel) * mat3(skin) * aNormal;
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProjection * world;
}
