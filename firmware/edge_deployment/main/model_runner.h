#ifndef MODEL_RUNNER_H_
#define MODEL_RUNNER_H_

#include "dsp_edge.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// To set the percentiles for setting thresholds.
#define CALIBRATION_WINDOW_COUNT ((size_t)100)
#define PERCENTILE_INDEX ((size_t)98)
#define THRESHOLD_BOOST (1.00f)

// To define tensor arena for static allocation for model inference.
#define TENSOR_ARENA_SIZE ((size_t)(50 * 1024))

#define LATENT_DIM ((size_t)16)

// runner state enumuration.
typedef enum
{
    STATE_UNINITIALIZED = -1,
    STATE_CALIBRATING = 0,
    STATE_MONITORING = 1
} RunnerState;

#ifdef __cplusplus
extern "C"
{
#endif

    bool initModelRunner(void);
    bool initModelRunnerExplicit(const uint8_t *modelData, size_t modelSize);
    void resetCalibration(void);
    RunnerState getRunnerState(void);
    float getDynamicThreshold(void);

    bool runInference(
        const float *modelInput,
        const float *conditionVector,
        float *outMse);

    const int8_t *getLatentVector(void);

#ifdef __cplusplus
}
#endif

#endif