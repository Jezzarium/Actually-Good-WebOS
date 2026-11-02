#pragma once

#include <algorithm>

// A damped spring toward target, for every animated value.
struct Spring {
    float x = 0, v = 0, target = 0;

    void step(float dt, float stiffness, float damping) {
        // Small fixed substeps keep the integration stable at any frame time.
        for (float left = dt; left > 0; left -= 1.f / 240) {
            float h = std::min(left, 1.f / 240);
            v += ((target - x) * stiffness - v * damping) * h;
            x += v * h;
        }
    }
    void snap(float value) { x = target = value, v = 0; }
};
