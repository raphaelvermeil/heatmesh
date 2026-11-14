// heat_solver.cpp: cotangent Laplacian + implicit Euler, solved with Eigen.
//
// This is the only file that includes Eigen, and CMakeLists.txt compiles it
// with -O2 even in Debug builds (unoptimized Eigen is painfully slow).

#include "heat_solver.h"

#include <Eigen/Sparse>                 // SparseMatrix, Triplet
#include <Eigen/SparseCholesky>         // SimplicialLDLT

#include <chrono>
#include <cmath>
#include <cstdio>

// --- pImpl ("pointer to implementation") ---
// The header only says "there is a struct Impl" and holds a pointer to it.
// Its contents, which need Eigen, are only spelled out here. So main.cpp doesn't
// have to compile Eigen's (huge) headers, and doesn't care what's inside.
struct ImplicitHeatSolver::Impl {
    using SpMat = Eigen::SparseMatrix<double>;  // 'using' = a shorter name for a type

    SpMat           L;        // cotangent Laplacian, n x n, symmetric, mostly zeros
    Eigen::VectorXd area;     // area_i: the share of surface owned by vertex i
    Eigen::VectorXd mass;     // = area, except 1 for vertices with no triangles (see build)

    // Cholesky-type factorization of A = M + tau*L. "Simplicial LDL^T": it writes
    // A = P^T L D L^T P (P reorders vertices to keep the factors sparse), after
    // which solving A x = b is just two cheap triangular substitutions.
    Eigen::SimplicialLDLT<SpMat> solver;
    double factoredTau = -1.0;  // the tau 'solver' was built for; -1 = none yet

    double factorMs = 0.0, solveMs = 0.0;
};

ImplicitHeatSolver::ImplicitHeatSolver() : impl_(std::make_unique<Impl>()) {}
ImplicitHeatSolver::~ImplicitHeatSolver() = default;  // here, where Impl is complete

// cot(angle at corner o) in the triangle o, p, q:
//   cos = dot(u, v) / (|u||v|),  sin = |cross(u, v)| / (|u||v|),  cot = cos / sin
// so the lengths cancel out: cot = dot(u, v) / |cross(u, v)|.
static double cotangentAt(const glm::dvec3& o, const glm::dvec3& p, const glm::dvec3& q) {
    const glm::dvec3 u = p - o, v = q - o;
    const double crossLen = glm::length(glm::cross(u, v));
    return glm::dot(u, v) / crossLen;  // caller makes sure crossLen > 0
}

void ImplicitHeatSolver::build(const Mesh& mesh) {
    const int n = static_cast<int>(mesh.vertexCount());
    Impl& m = *impl_;  // a short alias for the implementation

    // A triplet is (row, column, value). We list every contribution, and
    // setFromTriplets() adds up all the ones that land on the same (row, column).
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(mesh.triangleCount() * 12);  // each triangle adds 12 entries
    m.area = Eigen::VectorXd::Zero(n);

    int skipped = 0;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        const int ids[3] = {static_cast<int>(mesh.indices[3 * t + 0]),
                            static_cast<int>(mesh.indices[3 * t + 1]),
                            static_cast<int>(mesh.indices[3 * t + 2])};
        // dvec3 = vec3 of doubles: the cotangent of a very thin triangle loses a
        // lot of precision in float.
        const glm::dvec3 p[3] = {glm::dvec3(mesh.positions[ids[0]]),
                                 glm::dvec3(mesh.positions[ids[1]]),
                                 glm::dvec3(mesh.positions[ids[2]])};

        const double area = 0.5 * glm::length(glm::cross(p[1] - p[0], p[2] - p[0]));
        if (area < 1e-14) { ++skipped; continue; }  // degenerate: no shape, no angles

        // Each vertex gets a third of the triangle's area ("barycentric" area).
        for (int k = 0; k < 3; ++k) m.area[ids[k]] += area / 3.0;

        // For corner k, the opposite edge joins the other two corners i and j.
        // That edge gets weight 0.5 * cot(angle at k) from this triangle; the
        // triangle on the other side of the edge adds its own half.
        for (int k = 0; k < 3; ++k) {
            const int i = (k + 1) % 3, j = (k + 2) % 3;  // the two other corners
            const double w = 0.5 * cotangentAt(p[k], p[i], p[j]);
            const int vi = ids[i], vj = ids[j];
            // Row i of L*T is  sum_j w_ij * (T_i - T_j), written as 4 entries:
            triplets.emplace_back(vi, vi,  w);
            triplets.emplace_back(vj, vj,  w);
            triplets.emplace_back(vi, vj, -w);
            triplets.emplace_back(vj, vi, -w);
        }
    }

    m.L.resize(n, n);
    m.L.setFromTriplets(triplets.begin(), triplets.end());

    // Vertices used by no triangle (the 1,113 leftovers) have area 0, so their
    // row of M + tau*L would be all zeros, and the system would have no unique
    // solution. Giving them mass 1 turns their equation into T_new = T_old.
    m.mass = m.area;
    int isolated = 0;
    for (int i = 0; i < n; ++i)
        if (m.mass[i] <= 0.0) { m.mass[i] = 1.0; ++isolated; }

    m.factoredTau = -1.0;  // any old factorization is now invalid
    std::printf("Cotangent Laplacian: %d x %d, %ld non-zeros, %d degenerate triangles, "
                "%d isolated vertices, area %.4f\n",
                n, n, static_cast<long>(m.L.nonZeros()), skipped, isolated, m.area.sum());
}

bool ImplicitHeatSolver::step(std::vector<float>& temperature, double tau) {
    Impl& m = *impl_;
    using clock = std::chrono::steady_clock;

    // Factorize A = M + tau * L, but only if tau changed since last time.
    if (tau != m.factoredTau) {
        const auto t0 = clock::now();
        Impl::SpMat A = m.L * tau;
        // Add the mass on the diagonal: mass.asDiagonal() is the diagonal matrix
        // M, turned into a sparse matrix so it can be added to the sparse A.
        A += Impl::SpMat(m.mass.asDiagonal());
        m.solver.compute(A);
        m.factorMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        if (m.solver.info() != Eigen::Success) {
            std::fprintf(stderr, "Factorization failed\n");
            m.factoredTau = -1.0;
            return false;
        }
        m.factoredTau = tau;
    }

    const auto t0 = clock::now();
    const int n = static_cast<int>(temperature.size());

    // Right-hand side b = M * T_old. Eigen::Map views our float array as an Eigen
    // vector without copying; .cast<double>() converts to double.
    const Eigen::Map<const Eigen::VectorXf> Told(temperature.data(), n);
    const Eigen::VectorXd b = m.mass.cwiseProduct(Told.cast<double>());  // element-wise

    const Eigen::VectorXd Tnew = m.solver.solve(b);
    if (m.solver.info() != Eigen::Success) {
        std::fprintf(stderr, "Solve failed\n");
        return false;
    }

    // Write back into the std::vector, again through a Map (no extra copy).
    Eigen::Map<Eigen::VectorXf>(temperature.data(), n) = Tnew.cast<float>();
    m.solveMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    return true;
}

double ImplicitHeatSolver::totalHeat(const std::vector<float>& temperature) const {
    const Eigen::Map<const Eigen::VectorXf> T(temperature.data(),
                                              static_cast<int>(temperature.size()));
    return impl_->area.dot(T.cast<double>());  // sum of area_i * T_i
}

double ImplicitHeatSolver::surfaceArea() const { return impl_->area.sum(); }
double ImplicitHeatSolver::lastFactorMs() const { return impl_->factorMs; }
double ImplicitHeatSolver::lastSolveMs()  const { return impl_->solveMs; }
