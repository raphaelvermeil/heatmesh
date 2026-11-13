// camera.cpp: definitions for OrbitCamera's member functions.

#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>  // lookAt, perspective

#include <algorithm>  // std::clamp
#include <cmath>      // std::sin, std::cos, std::pow

// 'constexpr' = a constant known at compile time. These live in an unnamed
// namespace, which (like 'static') keeps them private to this .cpp file.
namespace {
constexpr float kDegreesPerPixel = 0.3f;   // drag sensitivity
constexpr float kZoomPerStep     = 0.9f;   // each wheel notch: distance *= 0.9
constexpr float kMaxPitch        = 89.0f;  // at exactly 90, lookAt's "up" vector breaks
constexpr float kMinDistance     = 0.5f;
constexpr float kMaxDistance     = 50.0f;
}  // namespace

// 'OrbitCamera::' says "this is the rotate() that belongs to OrbitCamera".
// Inside a member function, yawDeg_ etc. refer to *this* camera's members.
void OrbitCamera::rotate(float dxPixels, float dyPixels) {
    // Drag right -> camera moves left around the object, so the object appears
    // to turn right with your hand. Drag down -> camera rises, looks from above.
    yawDeg_   -= dxPixels * kDegreesPerPixel;
    pitchDeg_ += dyPixels * kDegreesPerPixel;
    pitchDeg_  = std::clamp(pitchDeg_, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::zoom(float scrollSteps) {
    // Multiplying (instead of adding) makes zoom feel the same at any distance.
    // Scroll up (positive) -> 0.9^steps < 1 -> closer.
    distance_ *= std::pow(kZoomPerStep, scrollSteps);
    distance_  = std::clamp(distance_, kMinDistance, kMaxDistance);
}

void OrbitCamera::reset() {
    // OrbitCamera{} creates a fresh camera with all the default values from the
    // header; copying it into *this resets every member at once.
    *this = OrbitCamera{};
}

glm::vec3 OrbitCamera::position() const {
    float yaw   = glm::radians(yawDeg_);
    float pitch = glm::radians(pitchDeg_);
    // Spherical -> Cartesian. With yaw = pitch = 0 this gives (0, 0, distance):
    // straight in front of the target, like the fixed camera in Step 3.
    glm::vec3 offset(std::cos(pitch) * std::sin(yaw),
                     std::sin(pitch),
                     std::cos(pitch) * std::cos(yaw));
    return target + distance_ * offset;
}

glm::mat4 OrbitCamera::viewMatrix() const {
    return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
}
