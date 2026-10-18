#include "model_runner.h"
#include "model_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace
{
    // TFLM core instances and statis buffer.
    const tflite::Model *tflmModel = nullptr;

    // static buffers for MicroInterpreter (to avoid heap)
    alignas(alignof(tflite::MicroInterpreter)) static uint8_t interpreterBuffer[sizeof(tflite::MicroInterpreter)];
    tflite::MicroInterpreter *interpreter = nullptr;

    // Pointers to 2 input tensors and 2 output tensors.
    TfLiteTensor *inputXTensor = nullptr;       // Shape: [1, 1, 128, 3] -> 384 bytes
    TfLiteTensor *inputCTensor = nullptr;       // Shape: [1, 2]          -> 2 bytes
    TfLiteTensor *outputRecTensor = nullptr;    // Shape: [1, 1, 128, 3] -> 384 bytes
    TfLiteTensor *outputLatentTensor = nullptr; // Shape: [1, 16]         -> 16 bytes

    // alignas(16) to avoid LoadStoreAlignmentCause on ESP32
    alignas(16) uint8_t tensorArena[TENSOR_ARENA_SIZE];

    // static operators according to the machine learning pipeline.
    static tflite::MicroMutableOpResolver<6> opResolver;

    // cached quantization parameters.
    float scaleX = 1.0f;
    int32_t zpX = 0;

    float scaleC = 1.0f;
    int32_t zpC = 0;

    float scaleRec = 1.0f;
    int32_t zpRec = 0;

    // pre-quantized condition cache.
    int8_t cachedCondFloor[2];
    int8_t cachedCondTable[2];

    // calibration and state tracker.
    struct EnvCalibrationTracker
    {
        RunnerState state;
        size_t count;
        float threshold;
        float samples[CALIBRATION_WINDOW_COUNT];
    };

    EnvCalibrationTracker envTrackers[2];
    int activeEnvIndex = 0; // 0: ENV_FLOOR, 1: ENV_TABLE
    bool isRunnerInitialized = false;

    // convert floating-point numbers to int8
    inline int8_t quantizeFloatToInt8(float value, float scale, int32_t zeroPoint)
    {
        const int32_t q = static_cast<int32_t>(roundf(value / scale)) + zeroPoint;
        if (q < -128)
            return -128;
        if (q > 127)
            return 127;
        return static_cast<int8_t>(q);
    }

    // casting int32 to avoid underflow/overflow
    inline float dequantizeInt8ToFloat(int8_t value, float scale, int32_t zeroPoint)
    {
        return (static_cast<int32_t>(value) - zeroPoint) * scale;
    }

    inline int getEnvironmentIndex(const float *conditionVector)
    {
        return (conditionVector[1] > conditionVector[0]) ? 1 : 0;
    }

    // register 6 ops
    bool registerRequiredOps()
    {
        if (opResolver.AddConv2D() != kTfLiteOk)
            return false;
        if (opResolver.AddMaxPool2D() != kTfLiteOk)
            return false;
        if (opResolver.AddFullyConnected() != kTfLiteOk)
            return false;
        if (opResolver.AddReshape() != kTfLiteOk)
            return false;
        if (opResolver.AddConcatenation() != kTfLiteOk)
            return false;
        if (opResolver.AddQuantize() != kTfLiteOk)
            return false;
        return true;
    }

    // set thresholds algorithm.
    void updateEmpiricalCalibration(EnvCalibrationTracker &tracker, float sampleMse)
    {
        if (tracker.count < CALIBRATION_WINDOW_COUNT)
        {
            tracker.samples[tracker.count] = sampleMse;
            tracker.count++;
        }

        if (tracker.count >= CALIBRATION_WINDOW_COUNT)
        {
            float sortedBuf[CALIBRATION_WINDOW_COUNT];
            memcpy(sortedBuf, tracker.samples, sizeof(sortedBuf));
            std::sort(sortedBuf, sortedBuf + CALIBRATION_WINDOW_COUNT);

            const float empiricalPercentile = sortedBuf[PERCENTILE_INDEX];
            tracker.threshold = empiricalPercentile * THRESHOLD_BOOST;
            tracker.state = STATE_MONITORING;
        }
    }
}

#ifdef __cplusplus
extern "C"
{
#endif

    bool initModelRunnerExplicit(const uint8_t *modelData, size_t modelSize)
    {
        isRunnerInitialized = false;
        resetCalibration();

        if (modelData == nullptr || modelSize == 0)
        {
            return false;
        }

        // check FlatBuffer
        tflmModel = tflite::GetModel(modelData);
        if (tflmModel->version() != TFLITE_SCHEMA_VERSION)
        {
            return false;
        }

        // To initialize Resolver with 6 ops
        static bool opsRegistered = false;
        if (!opsRegistered)
        {
            if (!registerRequiredOps())
            {
                return false;
            }
            opsRegistered = true;
        }

        // MicroInterpret
        if (interpreter != nullptr)
        {
            interpreter->~MicroInterpreter();
            interpreter = nullptr;
        }

        interpreter = new (interpreterBuffer) tflite::MicroInterpreter(
            tflmModel, opResolver, tensorArena, TENSOR_ARENA_SIZE);

        // Allocate memory for static tensors
        if (interpreter->AllocateTensors() != kTfLiteOk)
        {
            return false;
        }

        // To check tensors
        if (interpreter->inputs_size() != 2 || interpreter->outputs_size() != 2)
        {
            return false;
        }

        // 6. Decompose the input tensor based on byte size (to prevent errors in FlatBuffer)
        TfLiteTensor *in0 = interpreter->input(0);
        TfLiteTensor *in1 = interpreter->input(1);

        if (in0->bytes == (128 * 3) && in1->bytes == 2)
        {
            inputXTensor = in0;
            inputCTensor = in1;
        }
        else if (in0->bytes == 2 && in1->bytes == (128 * 3))
        {
            inputCTensor = in0;
            inputXTensor = in1;
        }
        else
        {
            return false;
        }

        // Decompose tensor outputs based on byte size
        TfLiteTensor *out0 = interpreter->output(0);
        TfLiteTensor *out1 = interpreter->output(1);

        if (out0->bytes == (128 * 3) && out1->bytes == LATENT_DIM)
        {
            outputRecTensor = out0;
            outputLatentTensor = out1;
        }
        else if (out0->bytes == LATENT_DIM && out1->bytes == (128 * 3))
        {
            outputLatentTensor = out0;
            outputRecTensor = out1;
        }
        else
        {
            return false;
        }

        // check the datatype whether it is int8
        if (inputXTensor->type != kTfLiteInt8 || inputCTensor->type != kTfLiteInt8 ||
            outputRecTensor->type != kTfLiteInt8 || outputLatentTensor->type != kTfLiteInt8)
        {
            return false;
        }

        // extract dynamically quantized parameters
        scaleX = inputXTensor->params.scale;
        zpX = inputXTensor->params.zero_point;

        scaleC = inputCTensor->params.scale;
        zpC = inputCTensor->params.zero_point;

        scaleRec = outputRecTensor->params.scale;
        zpRec = outputRecTensor->params.zero_point;

        if (scaleX <= 0.0f || scaleC <= 0.0f || scaleRec <= 0.0f)
        {
            return false;
        }

        // pre-quantized environmental vectors.
        cachedCondFloor[0] = quantizeFloatToInt8(1.0f, scaleC, zpC);
        cachedCondFloor[1] = quantizeFloatToInt8(0.0f, scaleC, zpC);

        cachedCondTable[0] = quantizeFloatToInt8(0.0f, scaleC, zpC);
        cachedCondTable[1] = quantizeFloatToInt8(1.0f, scaleC, zpC);

        isRunnerInitialized = true;
        return true;
    }

    bool initModelRunner(void)
    {
        return initModelRunnerExplicit(g_model, g_model_len);
    }

    void resetCalibration(void)
    {
        envTrackers[0].state = STATE_CALIBRATING;
        envTrackers[0].count = 0;
        envTrackers[0].threshold = 0.0f;
        memset(envTrackers[0].samples, 0, sizeof(envTrackers[0].samples));

        envTrackers[1].state = STATE_CALIBRATING;
        envTrackers[1].count = 0;
        envTrackers[1].threshold = 0.0f;
        memset(envTrackers[1].samples, 0, sizeof(envTrackers[1].samples));

        activeEnvIndex = 0;
    }

    RunnerState getRunnerState(void)
    {
        if (!isRunnerInitialized)
        {
            return STATE_UNINITIALIZED;
        }
        return envTrackers[activeEnvIndex].state;
    }

    float getDynamicThreshold(void)
    {
        return envTrackers[activeEnvIndex].threshold;
    }

    const int8_t *getLatentVector(void)
    {
        if (!isRunnerInitialized || outputLatentTensor == nullptr)
        {
            return nullptr;
        }
        return outputLatentTensor->data.int8;
    }

    bool runInference(
        const float *modelInput,
        const float *conditionVector,
        float *outMse)
    {
        if (!isRunnerInitialized || modelInput == nullptr || conditionVector == nullptr)
        {
            return false;
        }

        activeEnvIndex = getEnvironmentIndex(conditionVector);
        EnvCalibrationTracker &tracker = envTrackers[activeEnvIndex];

        // quantized and import data
        int8_t *inXData = inputXTensor->data.int8;
        for (int l = 0; l < 128; l++)
        {
            for (int c = 0; c < 3; c++)
            {
                const float val = modelInput[c * 128 + l];
                inXData[l * 3 + c] = quantizeFloatToInt8(val, scaleX, zpX);
            }
        }

        // import environmental vector from cache.
        int8_t *inCData = inputCTensor->data.int8;
        const int8_t *srcCond = (activeEnvIndex == 1) ? cachedCondTable : cachedCondFloor;
        inCData[0] = srcCond[0];
        inCData[1] = srcCond[1];

        // edge inference of TFLM
        if (interpreter->Invoke() != kTfLiteOk)
        {
            return false;
        }

        // To compute MSE
        const int8_t *outRecData = outputRecTensor->data.int8;
        float totalSqErr = 0.0f;
        constexpr int totalElements = 128 * 3;

        for (int i = 0; i < totalElements; i++)
        {
            const float deqX = dequantizeInt8ToFloat(inXData[i], scaleX, zpX);
            const float deqRec = dequantizeInt8ToFloat(outRecData[i], scaleRec, zpRec);
            const float diff = deqX - deqRec;
            totalSqErr += diff * diff;
        }

        const float mse = totalSqErr / static_cast<float>(totalElements);
        if (outMse != nullptr)
        {
            *outMse = mse;
        }

        // To update the machine's states
        if (tracker.state == STATE_CALIBRATING)
        {
            updateEmpiricalCalibration(tracker, mse);
            return false;
        }

        return (mse > tracker.threshold);
    }

#ifdef __cplusplus
}
#endif