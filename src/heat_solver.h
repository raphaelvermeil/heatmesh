// heat_solver.h: Step 13, the "proper" heat equation on a triangle mesh.
//
// Two upgrades over Step 11's graph average:
//
//  1. Cotangent Laplacian: each neighbour is weighted by the *geometry* of the
//     two triangles on either side of the edge (via the cotangents of the angles
//     facing it), and each vertex owns a share of the surface area (its "mass").
//     Result: diffusion that depends on the real shape, not on how the mesh
//     happens to be triangulated.
//
//  2. Implicit Euler: instead of computing the new temperatures *from* the old
//     ones (explicit), we solve for the new temperatures that are consistent
//     with themselves:   (M + tau * L) * T_new = M * T_old
//     That's a big linear system (35,947 unknowns), solved with Eigen. In
//     exchange, it's stable for ANY step size: no more 'a <= 0.5' limit.
//
// The Eigen types are hidden in heat_solver.cpp (the "pImpl" pattern, explained
// there), so this header and main.cpp never include Eigen.

#pragma once

#include "mesh.h"

#include <memory>
#include <vector>

class ImplicitHeatSolver {
public:
    ImplicitHeatSolver();
    ~ImplicitHeatSolver();  // defined in the .cpp, where Impl is a complete type

    // Builds the cotangent Laplacian L and the mass (area) diagonal M. Done once.
    void build(const Mesh& mesh);

    // One implicit step: T <- solution of (M + tau * L) T_new = M T.
    // tau = diffusivity * step duration. The matrix is factorized again only
    // when tau changes (that's the expensive part); otherwise the previous
    // factorization is reused and a step is just a fast "solve".
    // Returns false if the solver failed (T is then left unchanged).
    bool step(std::vector<float>& temperature, double tau);

    // Total heat = integral of T over the surface = sum of area_i * T_i.
    // The implicit cotangent scheme conserves this exactly.
    double totalHeat(const std::vector<float>& temperature) const;

    double surfaceArea() const;      // sum of all vertex areas (for the panel)
    double lastFactorMs() const;     // how long the last factorization took
    double lastSolveMs()  const;     // how long the last solve took

private:
    struct Impl;                  // declared here, defined in heat_solver.cpp
    std::unique_ptr<Impl> impl_;  // owns it; deleted automatically with the solver
};
