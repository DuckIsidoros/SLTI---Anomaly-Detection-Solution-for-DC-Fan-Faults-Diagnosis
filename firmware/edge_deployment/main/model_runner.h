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
#define MIN_THRESHOLD_FLOOR (1e-6f)

// To define tensor arena for static allocation for model inference.
// shortcut: 32KB static arena fits dram0_0_seg with WiFi/MQTT; increase or move to heap if model grows
#define TENSOR_ARENA_SIZE ((size_t)(32 * 1024))

#define LATENT_DIM ((size_t)16)

// runner state enumuration.
typedef enum
{
    STATE_UNINITIALIZED = -1,
    STATE_CALIBRATING = 0,
    STATE_MONITORING = 1
} RunnerState;

typedef enum
{
    INFERENCE_OK = 0,
    INFERENCE_ERR_NOT_INITIALIZED = -1,
    INFERENCE_ERR_INVALID_INPUT = -2,
    INFERENCE_ERR_INVOKE_FAILED = -3
} InferenceStatus;

#ifdef __cplusplus
extern "C"
{
#endif

    bool initModelRunner(void);
    bool initModelRunnerExplicit(const uint8_t *modelData, size_t modelSize);
    void setActiveEnvironment(Environment env);
    void resetCalibration(void);
    RunnerState getRunnerState(void);
    size_t getCalibrationStep(void);
    float getDynamicThreshold(void);

    InferenceStatus runInference(
        const float *modelInput,
        const float *conditionVector,
        float *outMse,
        bool *outIsAnomaly);

    const int8_t *getLatentVector(void);

#ifdef __cplusplus
}
#endif

#endif
