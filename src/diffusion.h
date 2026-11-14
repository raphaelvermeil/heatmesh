// diffusion.h: heat spreading over the mesh (Step 11, "diffusion v1").
//
// The rule, for every vertex i at every step:
//     T[i] += a * (average(T[neighbours of i]) - T[i])        with a = dt * k
// "Move a fraction 'a' of the way towards your neighbours' average."
// Hot vertices next to cold ones cool down, cold ones next to hot ones warm up,
// and over time the temperature evens out.

#pragma once

#include "mesh.h"

#include <vector>

// Largest 'a' we allow in a single step. Above 1 the update overshoots the
// average and the simulation blows up (see the explanation in diffusion.cpp).
// 0.5 leaves a comfortable safety margin.
constexpr float kMaxStepAmount = 0.5f;

// One step. Reads only 'in', writes only 'out' (they must be different vectors
// of the same size). Vertices with no neighbours keep their temperature.
void diffuseStep(const Mesh& mesh, const std::vector<float>& in,
                 std::vector<float>& out, float a);

// Advances the simulation by 'amount' = k * dt in total, split into as many
// small steps as needed so each one uses a <= kMaxStepAmount.
// 'scratch' is a work buffer, reused between calls so we don't allocate every frame.
// At most 'maxSteps' steps are taken (if more would be needed, the simulation
// simply runs slower than real time). Returns the number of steps taken.
int diffuse(const Mesh& mesh, std::vector<float>& temperature,
            std::vector<float>& scratch, float amount, int maxSteps);
