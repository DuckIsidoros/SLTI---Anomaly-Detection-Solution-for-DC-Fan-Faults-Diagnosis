#pragma once

#include <Arduino.h>

constexpr int SAMPLING_RATE_HZ = 500;
constexpr int WINDOW_SIZE = 512;
constexpr int STEP_SIZE = 256;

enum Axis {
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2
};

void resetBandpassState();

float applyBandpassSample(
    float input,
    Axis axis
);

void removeDcOffset(
    float* signal,
    size_t length
);

void applyHannWindow(
    float* signal,
    size_t length
);