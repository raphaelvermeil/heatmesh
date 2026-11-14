// mesh.cpp: .obj loading via tinyobjloader, and mesh normalization.

#include "mesh.h"

#include <tiny_obj_loader.h>

#include <algorithm>   // std::max, std::sort, std::unique
#include <cstdio>
#include <filesystem>  // std::filesystem::u8path
#include <fstream>
#include <sstream>

bool loadObj(const std::string& path, Mesh& mesh, std::string& error) {
    // Read the file ourselves instead of tinyobj's ParseFromFile(path): paths from
    // the file dialog are UTF-8, and on Windows a plain std::string path breaks on
    // accents ("C:/Users/Hélène/..."). u8path says "this text is UTF-8", and the
    // filesystem library converts it to the form Windows expects.
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) {
        error = "Can't open the file.";
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;  // split quads/polygons into triangles

    tinyobj::ObjReader reader;
    // 2nd argument = contents of the .mtl material file; we don't use materials.
    if (!reader.ParseFromString(text.str(), "", config)) {
        error = "Not a valid .obj: " + reader.Error();
        return false;
    }
    // Warnings (e.g. "material not found") are harmless for us: print, don't fail.
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
            // A broken file can reference a vertex that doesn't exist (or none: -1).
            // Using it later would read outside 'positions', so reject the file now.
            if (idx.vertex_index < 0 ||
                static_cast<size_t>(idx.vertex_index) >= mesh.positions.size()) {
                error = "The file references a vertex that doesn't exist.";
                return false;
            }
            mesh.indices.push_back(static_cast<unsigned int>(idx.vertex_index));
        }
    }

    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        error = "The file contains no triangles (only points or lines?).";
        return false;
    }

    std::printf("Loaded %s: %zu vertices, %zu triangles\n",
                path.c_str(), mesh.vertexCount(), mesh.triangleCount());
    return true;
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

void buildAdjacency(Mesh& mesh) {
    // One empty list per vertex. '{}' here means "an empty std::vector<unsigned int>".
    mesh.neighbors.assign(mesh.positions.size(), {});

    // A lambda is a small function written inline. [&] means it can use the
    // variables around it (here: mesh) by reference. Calling link(a, b) records
    // the edge in *both* directions, since "a is next to b" means "b is next to a".
    auto link = [&](unsigned int a, unsigned int b) {
        mesh.neighbors[a].push_back(b);
        mesh.neighbors[b].push_back(a);
    };

    // Pass 1: every triangle a-b-c has 3 edges: a-b, b-c, c-a.
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        unsigned int a = mesh.indices[3 * t + 0];
        unsigned int b = mesh.indices[3 * t + 1];
        unsigned int c = mesh.indices[3 * t + 2];
        link(a, b);
        link(b, c);
        link(c, a);
    }

    // Pass 2: an edge inside the surface belongs to 2 triangles, so it was added
    // twice. Remove the duplicates with the classic "sort, then unique + erase":
    //   sort:   [3, 1, 3, 2] -> [1, 2, 3, 3]   (duplicates are now side by side)
    //   unique: moves the distinct values to the front -> [1, 2, 3, ?] and returns
    //           an iterator to the '?' (the start of the leftover junk)
    //   erase:  cuts the vector from there to the end  -> [1, 2, 3]
    for (auto& list : mesh.neighbors) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
}
