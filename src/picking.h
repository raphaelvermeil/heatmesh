// picking.h: turning a mouse position into "which triangle / vertex is under it".
//
// The idea (ray picking):
//   1. A pixel on screen corresponds to a *line* in 3D: every point along it
//      projects onto that same pixel. That line, starting at the camera, is a ray.
//   2. We build that ray by running the camera transforms *backwards*.
//   3. We test the ray against every triangle and keep the closest hit.

#pragma once

#include "mesh.h"

#include <glm/glm.hpp>

#include <optional>

// A ray: all points origin + t * direction, for t >= 0.
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;  // length 1
};

// Everything we learn about a hit.
struct Hit {
    float        t;              // distance along the ray to the hit point
    size_t       triangle;       // which triangle (index into mesh.indices / 3)
    glm::vec3    point;          // the hit position
    unsigned int nearestVertex;  // the triangle corner closest to the hit point
};

// Builds the ray going through a mouse position.
//   mouse:      pixel coordinates, (0,0) = top-left of the window
//   windowSize: window size in the same units as 'mouse'
//   invMvp:     inverse(projection * view * model)  ->  the ray comes out in
//               the mesh's own (object) space, where mesh.positions live
Ray rayFromMouse(glm::vec2 mouse, glm::vec2 windowSize, const glm::mat4& invMvp);

// Ray vs. one triangle (Moller-Trumbore algorithm). If the ray hits it, returns
// t and writes the barycentric coordinates u, v of the hit point
// (hit = (1-u-v)*a + u*b + v*c). Returns std::nullopt if it misses.
std::optional<float> intersectTriangle(const Ray& ray,
                                       const glm::vec3& a, const glm::vec3& b,
                                       const glm::vec3& c, float& u, float& v);

// Ray vs. the whole mesh: the closest hit, or std::nullopt if nothing is hit.
std::optional<Hit> raycastMesh(const Mesh& mesh, const Ray& ray);
