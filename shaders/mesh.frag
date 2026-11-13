#version 410 core

// Lighting model: ambient + diffuse (Lambert).
//   ambient: a constant minimum brightness, so the dark side isn't pure black
//   diffuse: brightness = cos(angle between the surface normal and the light),
//            which is just dot(N, L) when both have length 1
// The light is a "headlight" placed at the camera, so whatever you look at is lit.

in vec3 vWorldPos;
in vec3 vWorldNormal;
in float vTemperature;  // 0 = cold, 1 = hot
out vec4 FragColor;

uniform vec3  uCameraPos;    // world-space camera position (= light position)
uniform vec3  uBaseColor;    // the surface's own color
uniform float uAmbient;      // 0..1
uniform bool  uShowNormals;  // debug view: color = normal direction
uniform bool  uShowTemperature;

// Flat color: skip lighting and paint everything in one color. Used for the
// wireframe lines and for the neighbour-highlight points (Step 7).
uniform bool  uUseFlatColor;
uniform vec3  uFlatColor;

void main() {
    // Checked first: the highlight points have no normal attribute, so the
    // lighting code below would produce garbage for them.
    if (uUseFlatColor) {
        FragColor = vec4(uFlatColor, 1.0);
        return;
    }

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

    // Step 8, temporary: blend the surface color towards red as it heats up.
    // mix(a, b, t) = a*(1-t) + b*t, so t=0 gives a, t=1 gives b.
    // Step 9 replaces this with a proper blue -> red colormap.
    vec3 surface = uBaseColor;
    if (uShowTemperature) {
        float t = clamp(vTemperature, 0.0, 1.0);  // keep it inside 0..1 just in case
        surface = mix(uBaseColor, vec3(1.0, 0.1, 0.05), t);
    }

    vec3 color = surface * (uAmbient + (1.0 - uAmbient) * diffuse);
    FragColor = vec4(color, 1.0);
}
