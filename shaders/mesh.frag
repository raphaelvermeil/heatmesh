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

// Step 9: the colormap's 5 stops, sent from C++ (see src/colormap.h).
// A uniform *array*: uColormap[0] .. uColormap[4].
uniform vec3  uColormap[5];
uniform bool  uIsolines;      // draw thin dark lines at T = 0.1, 0.2, ...
uniform float uIsolineCount;  // how many bands between 0 and 1 (10 -> a line every 0.1)

// Flat color: skip lighting and paint everything in one color. Used for the
// wireframe lines and for the neighbour-highlight points (Step 7).
uniform bool  uUseFlatColor;
uniform vec3  uFlatColor;

// Temperature -> colour. Same maths as sampleColormap() in src/colormap.h.
vec3 colormap(float t) {
    float x = clamp(t, 0.0, 1.0) * 4.0;    // 0..4: which segment, and how far into it
    int   i = min(int(floor(x)), 3);       // segment 0..3 (t = 1 stays in segment 3)
    float f = x - float(i);                // 0..1 inside the segment
    return mix(uColormap[i], uColormap[i + 1], f);
}

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

    vec3 surface = uBaseColor;
    if (uShowTemperature) {
        surface = colormap(vTemperature);

        if (uIsolines) {
            // Isolines = lines of equal temperature, like contour lines on a map.
            // v counts bands: T = 0.23 with 10 bands -> v = 2.3.
            float v = vTemperature * uIsolineCount;
            // Distance to the nearest whole number (0 at 2.0 or 3.0, 0.5 at 2.5).
            float dist = abs(fract(v + 0.5) - 0.5);
            // fwidth(v) = how much v changes from this pixel to the next. Dividing
            // by it measures the distance in *pixels*, so lines stay ~1 pixel wide
            // whether you zoom in or out.
            float line = 1.0 - clamp(dist / fwidth(v), 0.0, 1.0);
            surface = mix(surface, vec3(0.05), 0.7 * line);
        }
    }

    vec3 color = surface * (uAmbient + (1.0 - uAmbient) * diffuse);
    FragColor = vec4(color, 1.0);
}
