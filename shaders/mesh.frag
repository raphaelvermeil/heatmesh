#version 410 core

// Lighting model: ambient + diffuse (Lambert).
//   ambient: a constant minimum brightness, so the dark side isn't pure black
//   diffuse: brightness = cos(angle between the surface normal and the light),
//            which is just dot(N, L) when both have length 1
// The light is a "headlight" placed at the camera, so whatever you look at is lit.

in vec3 vWorldPos;
in vec3 vWorldNormal;
out vec4 FragColor;

uniform vec3  uCameraPos;    // world-space camera position (= light position)
uniform vec3  uBaseColor;    // the surface's own color
uniform float uAmbient;      // 0..1
uniform bool  uWireframe;
uniform bool  uShowNormals;  // debug view: color = normal direction

void main() {
    // Interpolation between the 3 vertices shortens the normal slightly,
    // so normalize again per pixel.
    vec3 N = normalize(vWorldNormal);

    // The bunny scan has holes, so we sometimes see the *inside* of the surface.
    // There the normal points away from us; flip it so the inside is lit too.
    // gl_FrontFacing is true when the triangle's corners appear counter-clockwise
    // on screen, which is the same rule the cross product used to build the normal.
    if (!gl_FrontFacing) N = -N;

    if (uShowNormals) {
        FragColor = vec4(N * 0.5 + 0.5, 1.0);  // map -1..1 to 0..1
        return;
    }

    vec3 L = normalize(uCameraPos - vWorldPos);  // direction from surface to light
    float diffuse = max(dot(N, L), 0.0);         // facing away from the light -> 0, not negative

    vec3 color = uBaseColor * (uAmbient + (1.0 - uAmbient) * diffuse);
    if (uWireframe) color = vec3(0.9);
    FragColor = vec4(color, 1.0);
}
