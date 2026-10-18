#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr int SAMPLING_RATE_HZ = 500;
constexpr int WINDOW_SIZE = 512;
constexpr int STEP_SIZE = 256;
constexpr int FFT_BINS = (WINDOW_SIZE / 2) + 1;
constexpr int FEATURE_START_BIN = 11;
constexpr int FEATURE_END_BIN = 138;
constexpr int FEATURE_COUNT = (FEATURE_END_BIN - FEATURE_START_BIN + 1);
constexpr int AXIS_COUNT = 3;
constexpr int SOS_SECTIONS = 4;
constexpr int MODEL_INPUT_SIZE = AXIS_COUNT * FEATURE_COUNT;

enum Axis
{
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2
};

// Environment for deploying the system encoding
#ifndef ENVIRONMENT_ENUM_DEFINED
#define ENVIRONMENT_ENUM_DEFINED
typedef enum
{
    ENV_FLOOR = 0,
    ENV_TABLE = 1
} Environment;

#endif
#ifdef __cplusplus
extern "C"
{
#endif

    // To initialize precomputed lookup tables and clear filter states.
    void initDsp(void);

    // To reset filter delay line states (z1, z2) for all axes.
    void resetBandpassState(void);

    // Direct from II SOS.
    float applyBandpassSample(float input, Axis axis);

    // DC offset removal algorithm.
    void removeDcOffset(float *signal, size_t length);

    // Hann window algorithm.
    void applyHannWindow(float *signal, size_t length);

    // To perform dsp processing including: removes DC, apply Hann window, and compute single-sided FFT magnitude divided by N.
    void computeFFTMagnitude(const float *signal, float *magnitude, size_t length);

    // To apply square-root compression and per-axis L2 normalization.
    void prepareModelInput(
        const float *magnitudeX,
        const float *magnitudeY,
        const float *magnitudeZ,
        float *modelInput);

    // To encode one-hot environment vector: [floor, table].
    void prepareConditionVector(Environment env, float *conditionOutput);

#ifdef __cplusplus
}
#endif