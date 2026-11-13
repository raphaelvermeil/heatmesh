#version 410 core

layout(location = 0) in vec3  aPos;          // from the position VBO
layout(location = 1) in vec3  aNormal;       // from the normal VBO (Step 6)
layout(location = 2) in float aTemperature;  // from the temperature VBO (Step 8)

uniform mat4 uModel;         // object space -> world space
uniform mat4 uView;          // world space  -> camera space
uniform mat4 uProjection;    // camera space -> clip space
uniform mat3 uNormalMatrix;  // object -> world, for normals (see main.cpp)

// Lighting is computed in world space, so we hand the fragment shader
// world-space versions of the position and the normal.
out vec3  vWorldPos;
out vec3  vWorldNormal;
out float vTemperature;  // interpolated across each triangle, like the cube's colors

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos     = worldPos.xyz;
    vWorldNormal  = uNormalMatrix * aNormal;
    vTemperature  = aTemperature;
    gl_Position   = uProjection * uView * worldPos;
}
