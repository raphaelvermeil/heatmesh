#version 410 core

layout(location = 0) in vec3 aPos;     // from the position VBO
layout(location = 1) in vec3 aNormal;  // from the normal VBO (new in Step 6)

uniform mat4 uModel;         // object space -> world space
uniform mat4 uView;          // world space  -> camera space
uniform mat4 uProjection;    // camera space -> clip space
uniform mat3 uNormalMatrix;  // object -> world, for normals (see main.cpp)

// Lighting is computed in world space, so we hand the fragment shader
// world-space versions of the position and the normal.
out vec3 vWorldPos;
out vec3 vWorldNormal;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos    = worldPos.xyz;
    vWorldNormal = uNormalMatrix * aNormal;
    gl_Position  = uProjection * uView * worldPos;
}
