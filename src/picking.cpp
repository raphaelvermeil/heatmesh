// picking.cpp: mouse ray construction and ray/triangle intersection.

#include "picking.h"

#include <cmath>   // std::abs
#include <limits>  // std::numeric_limits

Ray rayFromMouse(glm::vec2 mouse, glm::vec2 windowSize, const glm::mat4& invMvp) {
    // 1. Pixels -> NDC ("normalized device coordinates"), the -1..1 square that
    //    gl_Position ends up in. Screen y goes *down*, NDC y goes *up*: hence 1 - ...
    float ndcX = 2.0f * mouse.x / windowSize.x - 1.0f;
    float ndcY = 1.0f - 2.0f * mouse.y / windowSize.y;

    // 2. In NDC, z = -1 is the near plane and z = +1 the far plane. Take the point
    //    under the mouse on each plane and send it back through the inverse matrix.
    glm::vec4 nearH = invMvp * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = invMvp * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);

    // 3. The projection matrix produces "homogeneous" coordinates: the real point
    //    is (x/w, y/w, z/w). The GPU does this division for us when drawing;
    //    going backwards we have to do it ourselves.
    glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
    glm::vec3 farP  = glm::vec3(farH) / farH.w;

    return Ray{nearP, glm::normalize(farP - nearP)};
}

std::optional<float> intersectTriangle(const Ray& ray,
                                       const glm::vec3& a, const glm::vec3& b,
                                       const glm::vec3& c, float& u, float& v) {
    // Moller-Trumbore. We look for t, u, v such that
    //     origin + t * dir  =  a + u * (b - a) + v * (c - a)
    // i.e. "a point on the ray" equals "a point in the triangle's plane".
    // That's 3 equations (x, y, z) with 3 unknowns, solved with cross/dot products.
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p  = glm::cross(ray.direction, e2);
    const float det    = glm::dot(e1, p);

    // det ~ 0: the ray is parallel to the triangle's plane, so there's no single hit.
    // (The bunny's triangles are tiny, so det is tiny too; hence a very small epsilon.)
    if (std::abs(det) < 1e-12f) return std::nullopt;
    const float invDet = 1.0f / det;

    const glm::vec3 s = ray.origin - a;
    u = glm::dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) return std::nullopt;       // outside, beyond edge a-c or b-c

    const glm::vec3 q = glm::cross(s, e1);
    v = glm::dot(ray.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;   // outside, beyond edge a-b or b-c

    const float t = glm::dot(e2, q) * invDet;
    if (t <= 0.0f) return std::nullopt;                  // the triangle is behind the camera

    return t;  // an 'optional<float>' that holds a value
}

std::optional<Hit> raycastMesh(const Mesh& mesh, const Ray& ray) {
    std::optional<Hit> best;  // starts empty ("no hit yet")
    float bestT = std::numeric_limits<float>::max();

    // Brute force: test all 69,451 triangles. Only done while the mouse button is
    // held, and it takes ~1-2 ms, so we keep it simple.
    for (size_t tri = 0; tri < mesh.triangleCount(); ++tri) {
        const unsigned int ia = mesh.indices[3 * tri + 0];
        const unsigned int ib = mesh.indices[3 * tri + 1];
        const unsigned int ic = mesh.indices[3 * tri + 2];

        float u = 0.0f, v = 0.0f;
        std::optional<float> t = intersectTriangle(ray, mesh.positions[ia], mesh.positions[ib],
                                                   mesh.positions[ic], u, v);
        // 'if (t && ...)': an optional converts to true when it holds a value.
        // '*t' reads that value.
        if (t && *t < bestT) {
            bestT = *t;

            // Barycentric weights of the 3 corners; they add up to 1.
            // The biggest weight belongs to the closest corner.
            const float wa = 1.0f - u - v, wb = u, wc = v;
            unsigned int nearest = ia;
            float biggest = wa;
            if (wb > biggest) { nearest = ib; biggest = wb; }
            if (wc > biggest) { nearest = ic; }

            best = Hit{*t, tri, ray.origin + *t * ray.direction, nearest};
        }
    }
    return best;
}
