// diffusion.cpp: explicit (forward Euler) diffusion on the vertex graph.

#include "diffusion.h"

#include <algorithm>  // std::min
#include <cmath>      // std::ceil

void diffuseStep(const Mesh& mesh, const std::vector<float>& in,
                 std::vector<float>& out, float a) {
    // Raw pointers to the vectors' storage. In a Debug build (no optimizations),
    // every in[j] on a std::vector is a real function call; going through a plain
    // pointer is a direct memory read. This makes the loop several times faster
    // in Debug, and changes nothing in Release (the compiler does it for us there).
    const float* src = in.data();
    float*       dst = out.data();
    const size_t n   = mesh.vertexCount();

    for (size_t i = 0; i < n; ++i) {
        const std::vector<unsigned int>& nbList = mesh.neighbors[i];
        const size_t        count = nbList.size();
        const unsigned int* nb    = nbList.data();

        // The 1,113 unused vertices have no neighbours: averaging zero values
        // would divide by zero, so they just keep their value.
        if (count == 0) {
            dst[i] = src[i];
            continue;  // skip the rest of the loop body, go to the next i
        }

        float sum = 0.0f;
        for (size_t k = 0; k < count; ++k) sum += src[nb[k]];
        const float average = sum / static_cast<float>(count);

        // The README formula. Rewritten, it's also  (1 - a) * T[i] + a * average:
        // a weighted blend of "my value" and "my neighbours' value".
        dst[i] = src[i] + a * (average - src[i]);
    }
}

int diffuse(const Mesh& mesh, std::vector<float>& temperature,
            std::vector<float>& scratch, float amount, int maxSteps) {
    if (amount <= 0.0f || maxSteps <= 0) return 0;

    // How many steps so that each one is at most kMaxStepAmount?
    //   amount = 3.2  ->  ceil(3.2 / 0.5) = ceil(6.4) = 7 steps of 3.2 / 7 = 0.457
    int steps = static_cast<int>(std::ceil(amount / kMaxStepAmount));
    float a   = amount / static_cast<float>(steps);
    if (steps > maxSteps) {
        // Too much work for one frame: do maxSteps safe steps and accept that the
        // simulation falls behind real time, rather than using an unsafe 'a'.
        steps = maxSteps;
        a     = kMaxStepAmount;
    }

    scratch.resize(temperature.size());  // only allocates the first time
    for (int s = 0; s < steps; ++s) {
        diffuseStep(mesh, temperature, scratch, a);
        // scratch now holds the new values. swap() exchanges the two vectors'
        // contents in O(1): it swaps their internal pointers, no floats are copied.
        // Afterwards 'temperature' is the new state and 'scratch' the old one,
        // ready to be overwritten by the next step.
        temperature.swap(scratch);
    }
    return steps;
}
