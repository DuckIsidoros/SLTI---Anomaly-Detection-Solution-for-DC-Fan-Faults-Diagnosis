#pragma once

#include <Arduino.h>

constexpr int SAMPLING_RATE_HZ = 500;
constexpr int WINDOW_SIZE = 512;
constexpr int STEP_SIZE = 256;
constexpr int FFT_BINS = (WINDOW_SIZE/2) + 1;
constexpr int FEATURE_START_BIN = 11;
constexpr int FEATURE_COUNT = 128;

void prepareModelInput(
    const float* magnitudeX,
    const float* magnitudeY,
    const float* magnitudeZ,
    float* modelInput
);
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
void computeFFTMagnitude(
    float* signal,
    float* magnitude,
    size_t length
);