#include "dsp_edge.h"
#include <math.h>
#include <arduinoFFT.h>

namespace
{
    // We use an epsilon constant to prevent division by zero in L2 normalization.
    constexpr float EPSILON = 1e-8f;

    // Single biquad second-order section state and coefficients.
    struct SosSection
    {
        float b0, b1, b2;
        float a1, a2;
        float z1, z2;
    };

    // Scipy butter(4, [8.0/250.0, 142.0/250.0], btype='bandpass', output='sos') coefficients.
    const float SOS_COEFFICIENTS[SOS_SECTIONS][5] = {
        {0.116955502223359f, 0.233911004446717f, 0.116955502223359f, 0.149367269803975f, 0.052075387490515f},
        {1.0f, 2.0f, 1.0f, 0.299550026103775f, 0.482684337652456f},
        {1.0f, -2.0f, 1.0f, -1.810596432474866f, 0.821218831019077f},
        {1.0f, -2.0f, 1.0f, -1.920315409929951f, 0.930266028775419f}};

    SosSection sosFilter[AXIS_COUNT][SOS_SECTIONS];

    // symmetric hannwindow lookup table.
    float hannTable[WINDOW_SIZE];

    // single-precision buffers for arduinoFFT.
    float fftRealBuffer[WINDOW_SIZE];
    float fftImagBuffer[WINDOW_SIZE];

    // float template enables sqrtf/cosf paths.
    ArduinoFFT<float> FFT(fftRealBuffer, fftImagBuffer, WINDOW_SIZE, SAMPLING_RATE_HZ);

    // To normalize an extracted axis slice using root compression and per-window L2 norm.
    void normalizeAxisSlice(const float *magnitude, float *outputSlice)
    {
        float sumSquares = 0.0f;
        for (int i = 0; i < FEATURE_COUNT; i++)
        {
            const float mag = fmaxf(magnitude[FEATURE_START_BIN + i], 0.0f);
            const float rootVal = sqrtf(mag);
            outputSlice[i] = rootVal;
            sumSquares += rootVal * rootVal;
        }

        const float invNorm = 1.0f / (sqrtf(sumSquares) + EPSILON);
        for (int i = 0; i < FEATURE_COUNT; i++)
        {
            outputSlice[i] *= invNorm;
        }
    }
}

#ifdef __cplusplus
extern "C"
{
#endif

    // To precompute the symmetric Hann window table and initialize filter states.
    void initDsp(void)
    {
        resetBandpassState();
        const float factor = 2.0f * static_cast<float>(PI) / static_cast<float>(WINDOW_SIZE - 1);
        for (size_t i = 0; i < WINDOW_SIZE; i++)
        {
            hannTable[i] = 0.5f - 0.5f * cosf(static_cast<float>(i) * factor);
        }
    }

    // To clear the delay lines (z1, z2) for all filter sections.
    void resetBandpassState(void)
    {
        for (int axis = 0; axis < AXIS_COUNT; axis++)
        {
            for (int sec = 0; sec < SOS_SECTIONS; sec++)
            {
                sosFilter[axis][sec].b0 = SOS_COEFFICIENTS[sec][0];
                sosFilter[axis][sec].b1 = SOS_COEFFICIENTS[sec][1];
                sosFilter[axis][sec].b2 = SOS_COEFFICIENTS[sec][2];
                sosFilter[axis][sec].a1 = SOS_COEFFICIENTS[sec][3];
                sosFilter[axis][sec].a2 = SOS_COEFFICIENTS[sec][4];
                sosFilter[axis][sec].z1 = 0.0f;
                sosFilter[axis][sec].z2 = 0.0f;
            }
        }
    }

    // To appliy transposed direct form II biquad filtering.
    float applyBandpassSample(float input, Axis axis)
    {
        if (axis < 0 || axis >= AXIS_COUNT)
        {
            return input;
        }

        float value = input;
        for (int i = 0; i < SOS_SECTIONS; i++)
        {
            SosSection &sec = sosFilter[axis][i];
            const float output = sec.b0 * value + sec.z1;
            sec.z1 = sec.b1 * value - sec.a1 * output + sec.z2;
            sec.z2 = sec.b2 * value - sec.a2 * output;
            value = output;
        }

        return value;
    }

    // To compute and subtract the mean of the input signal array in place.
    void removeDcOffset(float *signal, size_t length)
    {
        if (signal == nullptr || length == 0)
        {
            return;
        }

        float sum = 0.0f;
        for (size_t i = 0; i < length; i++)
        {
            sum += signal[i];
        }

        const float mean = sum / static_cast<float>(length);
        for (size_t i = 0; i < length; i++)
        {
            signal[i] -= mean;
        }
    }

    // To multiply the input signal array by the precomputed Hann table.
    void applyHannWindow(float *signal, size_t length)
    {
        if (signal == nullptr || length != WINDOW_SIZE)
        {
            return;
        }

        for (size_t i = 0; i < WINDOW_SIZE; i++)
        {
            signal[i] *= hannTable[i];
        }
    }

    // To encapsulate DC removal, Hann windowing, forward FFT, and 1/N scaling.
    void computeFFTMagnitude(const float *signal, float *magnitude, size_t length)
    {
        if (signal == nullptr || magnitude == nullptr || length != WINDOW_SIZE)
        {
            return;
        }

        float sum = 0.0f;
        for (size_t i = 0; i < WINDOW_SIZE; i++)
        {
            sum += signal[i];
        }
        const float mean = sum / static_cast<float>(WINDOW_SIZE);

        for (size_t i = 0; i < WINDOW_SIZE; i++)
        {
            // float multiply — no double cast, no double propagation
            fftRealBuffer[i] = (signal[i] - mean) * hannTable[i];
            fftImagBuffer[i] = 0.0f;
        }

#if defined(ARDUINOFFT_LIB_VERSION) || defined(FFTDirection)
        FFT.compute(FFTDirection::Forward);
        FFT.complexToMagnitude();
#else
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();
#endif

        const float invN = 1.0f / static_cast<float>(WINDOW_SIZE);
        // Only bins up to FEATURE_END_BIN are consumed (model input uses bins 11-138).
        for (int i = 0; i <= FEATURE_END_BIN; i++)
        {
            magnitude[i] = fftRealBuffer[i] * invN;
        }
    }

    // To prepare data for model input.
    void prepareModelInput(
        const float *magnitudeX,
        const float *magnitudeY,
        const float *magnitudeZ,
        float *modelInput)
    {
        if (magnitudeX == nullptr || magnitudeY == nullptr || magnitudeZ == nullptr || modelInput == nullptr)
        {
            return;
        }

        normalizeAxisSlice(magnitudeX, &modelInput[0 * FEATURE_COUNT]);
        normalizeAxisSlice(magnitudeY, &modelInput[1 * FEATURE_COUNT]);
        normalizeAxisSlice(magnitudeZ, &modelInput[2 * FEATURE_COUNT]);
    }

    // encode one-hot for the environment.
    void prepareConditionVector(Environment env, float *conditionOutput)
    {
        if (conditionOutput == nullptr)
        {
            return;
        }

        conditionOutput[0] = (env == ENV_FLOOR) ? 1.0f : 0.0f;
        conditionOutput[1] = (env == ENV_TABLE) ? 1.0f : 0.0f;
    }

#ifdef __cplusplus
}
#endif