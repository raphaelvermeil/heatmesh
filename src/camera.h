// camera.h: an orbit camera that circles around a target point.
//
// Instead of storing the camera's position directly, we store it in
// *spherical coordinates* around the target:
//   - yaw:      angle around the vertical (Y) axis   -> drag left/right
//   - pitch:    angle above/below the horizontal      -> drag up/down
//   - distance: how far from the target               -> scroll wheel
// position() converts these back to an (x, y, z) point.

#pragma once

#include <glm/glm.hpp>

// A 'class' bundles data (member variables) with the functions that act on it
// (member functions, also called methods).
class OrbitCamera {
public:
    // --- Settings: public, so main.cpp (and ImGui sliders) can tweak them ---
    glm::vec3 target{0.0f, 0.0f, 0.0f};  // the point we orbit around
    float fovDegrees = 45.0f;
    float nearPlane  = 0.1f;
    float farPlane   = 100.0f;

    // Member functions. 'const' at the end means "this function doesn't modify
    // the camera", so it can be called on a const OrbitCamera.
    void rotate(float dxPixels, float dyPixels);  // called while dragging
    void zoom(float scrollSteps);                 // called on mouse wheel
    void reset();                                 // back to the starting view

    glm::vec3 position() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;

    float yaw()      const { return yawDeg_; }    // small getters, defined right here
    float pitch()    const { return pitchDeg_; }
    float distance() const { return distance_; }

private:
    // --- State: private, so it can only change through rotate()/zoom(), which
    // keep it valid (e.g. pitch never reaches exactly +/-90 degrees).
    // The trailing underscore is just a naming convention for private members.
    float yawDeg_   = 30.0f;
    float pitchDeg_ = 20.0f;
    float distance_ = 3.0f;
};
