// colormap.h: colour scales that turn a temperature (0..1) into a colour.
//
// A colormap here is 5 colours ("stops") placed at t = 0, 0.25, 0.5, 0.75, 1.
// Any t in between is a linear blend of the two nearest stops.
//
// The stop table lives here, in C++, and is sent to the fragment shader as a
// uniform array. That way the shader (which colours the bunny) and the ImGui
// legend (drawn by C++) always use exactly the same colours.
//
// This file has no .cpp: everything is marked 'inline', which allows a
// definition to sit in a header that several .cpp files may #include.

#pragma once

#include <glm/glm.hpp>

#include <algorithm>  // std::clamp, std::min
#include <array>

constexpr int kColormapStops = 5;  // must match the array size in shaders/mesh.frag

struct Colormap {
    const char* name;
    // std::array<T, N>: a fixed-size array (like float x[5]) that also knows its
    // size and can be copied/compared like a normal value.
    std::array<glm::vec3, kColormapStops> stops;
};

// 'inline' on a variable (C++17): one shared definition even if this header is
// included in several .cpp files. Without it, each .cpp would get its own copy
// and the linker would complain about duplicates.
inline const std::array<Colormap, 2> kColormaps = {{
    // Classic rainbow: easy to read for "how hot", but not perceptually even.
    {"Rainbow", {{
        {0.00f, 0.00f, 1.00f},   // t = 0.00  blue
        {0.00f, 1.00f, 1.00f},   // t = 0.25  cyan
        {0.00f, 1.00f, 0.00f},   // t = 0.50  green
        {1.00f, 1.00f, 0.00f},   // t = 0.75  yellow
        {1.00f, 0.00f, 0.00f},   // t = 1.00  red
    }}},
    // Cool-warm (diverging): blue -> light grey -> red. Smoother, easier on the eye.
    {"Cool-warm", {{
        {0.23f, 0.30f, 0.75f},
        {0.55f, 0.69f, 0.99f},
        {0.87f, 0.87f, 0.87f},
        {0.96f, 0.60f, 0.48f},
        {0.71f, 0.02f, 0.15f},
    }}},
}};

// C++ twin of colormap() in shaders/mesh.frag: same maths, used for the legend.
//   t = 0.6  ->  x = 0.6 * 4 = 2.4  ->  segment i = 2 (between stops 2 and 3),
//   f = 0.4  ->  40% of the way from stop 2 to stop 3.
inline glm::vec3 sampleColormap(const Colormap& cm, float t) {
    float x = std::clamp(t, 0.0f, 1.0f) * (kColormapStops - 1);
    int   i = std::min(static_cast<int>(x), kColormapStops - 2);  // t = 1 stays in the last segment
    float f = x - static_cast<float>(i);
    return glm::mix(cm.stops[i], cm.stops[i + 1], f);
}
