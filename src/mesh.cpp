// mesh.cpp: .obj loading via tinyobjloader, and mesh normalization.

#include "mesh.h"

#include <tiny_obj_loader.h>

#include <algorithm>  // std::max
#include <cstdio>

bool loadObj(const std::string& path, Mesh& mesh) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;  // split quads/polygons into triangles

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        std::fprintf(stderr, "Failed to load %s: %s\n", path.c_str(), reader.Error().c_str());
        return false;
    }
    if (!reader.Warning().empty())
        std::fprintf(stderr, "OBJ warning (%s): %s\n", path.c_str(), reader.Warning().c_str());

    // 'auto' lets the compiler figure out the type (here: const tinyobj::attrib_t&).
    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    // attrib.vertices is a flat list of floats: x0 y0 z0 x1 y1 z1 ...
    mesh.positions.clear();
    mesh.positions.reserve(attrib.vertices.size() / 3);  // allocate once, up front
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
        mesh.positions.emplace_back(attrib.vertices[i],
                                    attrib.vertices[i + 1],
                                    attrib.vertices[i + 2]);
    }

    // An .obj can contain several shapes (objects); we merge them into one mesh.
    // We use only vertex_index (the position), so triangles that share a corner
    // share the same vertex number.
    mesh.indices.clear();
    for (const auto& shape : shapes) {               // range-based for: "for each shape in shapes"
        for (const auto& idx : shape.mesh.indices) {
            mesh.indices.push_back(static_cast<unsigned int>(idx.vertex_index));
        }
    }

    std::printf("Loaded %s: %zu vertices, %zu triangles\n",
                path.c_str(), mesh.vertexCount(), mesh.triangleCount());
    return !mesh.positions.empty() && !mesh.indices.empty();
}

void normalizeToUnitBox(Mesh& mesh) {
    if (mesh.positions.empty()) return;

    // Bounding box: smallest and largest x, y, z over all vertices.
    glm::vec3 lo = mesh.positions[0];
    glm::vec3 hi = mesh.positions[0];
    for (const glm::vec3& p : mesh.positions) {
        lo = glm::min(lo, p);  // component-wise min: (min(x), min(y), min(z))
        hi = glm::max(hi, p);
    }

    glm::vec3 center = 0.5f * (lo + hi);
    glm::vec3 size   = hi - lo;
    float longest    = std::max({size.x, size.y, size.z});
    float scale      = (longest > 0.0f) ? 1.0f / longest : 1.0f;

    // 'glm::vec3& p' (with &) is a reference into the vector, so changing p
    // changes the stored position. Without &, we'd be modifying a copy.
    for (glm::vec3& p : mesh.positions) {
        p = (p - center) * scale;
    }
}

void computeVertexNormals(Mesh& mesh) {
    // Start every vertex normal at (0,0,0). assign(n, value) resizes the vector
    // to n elements, all equal to value.
    mesh.normals.assign(mesh.positions.size(), glm::vec3(0.0f));

    // Pass 1: for each triangle, compute its normal and add it to its 3 corners.
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        unsigned int ia = mesh.indices[3 * t + 0];
        unsigned int ib = mesh.indices[3 * t + 1];
        unsigned int ic = mesh.indices[3 * t + 2];
        const glm::vec3& a = mesh.positions[ia];
        const glm::vec3& b = mesh.positions[ib];
        const glm::vec3& c = mesh.positions[ic];

        // The cross product of two edges is perpendicular to the triangle.
        // Its direction follows the right-hand rule on the corner order a -> b -> c,
        // and its length is twice the triangle's area. We deliberately do NOT
        // normalize it here: big triangles then count more than tiny slivers.
        glm::vec3 faceNormal = glm::cross(b - a, c - a);

        mesh.normals[ia] += faceNormal;
        mesh.normals[ib] += faceNormal;
        mesh.normals[ic] += faceNormal;
    }

    // Pass 2: each sum now points the average way; scale it to length 1.
    for (glm::vec3& n : mesh.normals) {
        float len = glm::length(n);
        // A vertex used by no triangle (or only by degenerate, zero-area ones)
        // has a zero sum; normalizing it would divide by zero, so pick any direction.
        n = (len > 0.0f) ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}
