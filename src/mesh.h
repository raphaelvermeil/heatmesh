// mesh.h: a triangle mesh stored on the CPU, plus loading it from an .obj file.

#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

// A 'struct' is the same as a class, except its members are public by default.
// We use struct for plain "bag of data" types like this one.
struct Mesh {
    // std::vector<T> is a resizable array that manages its own memory.
    // positions[i] is the 3D position of vertex i.
    std::vector<glm::vec3> positions;
    // Every 3 consecutive entries form one triangle: indices[3*t], [3*t+1], [3*t+2]
    // are the vertex numbers of triangle t. Vertices are *shared* between
    // triangles, which is what lets us find a vertex's neighbours in Step 7.
    std::vector<unsigned int> indices;
    // normals[i] is the direction vertex i's surface faces (length 1).
    // Filled by computeVertexNormals(); same size as positions.
    std::vector<glm::vec3> normals;
    // neighbors[i] is the list of vertices connected to vertex i by a triangle
    // edge, sorted, without duplicates. Filled by buildAdjacency().
    // A vector *of vectors*: each vertex has its own list, of its own length.
    std::vector<std::vector<unsigned int>> neighbors;

    size_t vertexCount()   const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }
};

// Loads the triangles of an .obj file (positions only; normals/UVs are ignored).
// Returns false and prints an error if the file can't be read.
bool loadObj(const std::string& path, Mesh& mesh);

// Moves and scales the mesh so it's centered on the origin and fits in a
// box of size 1 on its longest side. Different .obj files use wildly different
// units; this way the camera works for all of them.
void normalizeToUnitBox(Mesh& mesh);

// Computes one normal per vertex: the average of the normals of the triangles
// around it, weighted by triangle area. Needed for lighting.
void computeVertexNormals(Mesh& mesh);

// Builds mesh.neighbors from the triangles: two vertices are neighbours when
// they share an edge. This is the graph that heat will flow along (Step 11).
void buildAdjacency(Mesh& mesh);
