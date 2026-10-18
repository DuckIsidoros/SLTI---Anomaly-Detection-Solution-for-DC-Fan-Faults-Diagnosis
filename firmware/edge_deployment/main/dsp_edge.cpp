#include "dsp_edge.h"
#include <math.h>
#include <arduinoFFT.h>

constexpr int SOS_SECTIONS = 4;
constexpr int AXIS_COUNT = 3;
constexpr float EPSILON = 1e-8f;

constexpr float THRESHOLD_ENV_FLOOR = 0.000691f;
constexpr float THRESHOLD_ENV_TABLE = 0.000582f;

struct SosSection
{
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;

    float z1;
    float z2;
};

const float SOS_COEFFICIENTS[SOS_SECTIONS][5] = {
    {0.60894463271567f,
     -1.21788926543134f,
     0.60894463271567f,
     -1.570650306303734f,
     0.623033341607531f},
    {1.0f,
     2.0f,
     1.0f,
     1.775855808209344f,
     0.790363620107327f},
    {1.0f,
     -2.0f,
     1.0f,
     -1.7703242211039167f,
     0.828011828639938f},
    {1.0f,
     2.0f,
     1.0f,
     1.894330930650549f,
     0.909455814866532f}};

SosSection sosFilter[AXIS_COUNT][SOS_SECTIONS];

void resetBandpassState()
{
    /*
    Input:
    - None.

    Output:
    - Reset filter state for X, Y, and Z axes.

    Why:
    - Each acceleration axis must maintain
      independent continuous filter state.
    */

    for (int axis = 0; axis < AXIS_COUNT; axis++)
    {
        for (int section = 0; section < SOS_SECTIONS; section++)
        {

            sosFilter[axis][section].b0 =
                SOS_COEFFICIENTS[section][0];

            sosFilter[axis][section].b1 =
                SOS_COEFFICIENTS[section][1];

            sosFilter[axis][section].b2 =
                SOS_COEFFICIENTS[section][2];

            sosFilter[axis][section].a1 =
                SOS_COEFFICIENTS[section][3];

            sosFilter[axis][section].a2 =
                SOS_COEFFICIENTS[section][4];

            sosFilter[axis][section].z1 = 0.0f;
            sosFilter[axis][section].z2 = 0.0f;
        }
    }
}

float applyBandpassSample(
    float input,
    Axis axis)
{
    /*
    Input:
    - One acceleration sample.
    - Axis identifier.

    Output:
    - Bandpass-filtered sample.

    Why:
    - Keep independent continuous filter states
      for X, Y, and Z acceleration channels.
    */

    float value = input;

    for (int i = 0; i < SOS_SECTIONS; i++)
    {
        SosSection &section =
            sosFilter[axis][i];

        float output =
            section.b0 * value + section.z1;

        section.z1 =
            section.b1 * value - section.a1 * output + section.z2;

        section.z2 =
            section.b2 * value - section.a2 * output;

        value = output;
    }

    return value;
}
void removeDcOffset(
    float *signal,
    size_t length)
{
    /*
    Input:
    - Signal window.
    - Number of samples.

    Output:
    - Signal with its mean value removed.

    Why:
    - Remove the DC component before applying
      the Hann window and FFT.
    */

    if (signal == nullptr || length == 0)
    {
        return;
    }

    float mean = 0.0f;

    for (size_t i = 0; i < length; i++)
    {
        mean += signal[i];
    }

    mean /= static_cast<float>(length);

    for (size_t i = 0; i < length; i++)
    {
        signal[i] -= mean;
    }
}

void applyHannWindow(
    float *signal,
    size_t length)
{
    /*
    Input:
    - Signal window.
    - Number of samples.

    Output:
    - Signal multiplied by a Hann window.

    Why:
    - Reduce spectral leakage before FFT.
    */

    if (signal == nullptr || length <= 1)
    {
        return;
    }

    for (size_t i = 0; i < length; i++)
    {

        float hann =
            0.5f - 0.5f * cosf(
                              2.0f * PI * i / static_cast<float>(length - 1));

        signal[i] *= hann;
    }
}
void computeFFTMagnitude(
    float *signal,
    float *magnitude,
    size_t length)
{
    /*
    Input:
    - Time-domain signal window.
    - Output magnitude buffer.
    - Number of samples.

    Output:
    - One-sided FFT magnitude spectrum.

    Why:
    - Convert the time-domain vibration signal
      into frequency-domain features for the model.
    */

    if (
        signal == nullptr ||
        magnitude == nullptr ||
        length != WINDOW_SIZE)
    {
        return;
    }

    static double realBuffer[WINDOW_SIZE];
    static double imagBuffer[WINDOW_SIZE];

    for (size_t i = 0; i < length; i++)
    {
        realBuffer[i] = static_cast<double>(signal[i]);
        imagBuffer[i] = 0.0;
    }

    ArduinoFFT<double> FFT(
        realBuffer,
        imagBuffer,
        WINDOW_SIZE,
        SAMPLING_RATE_HZ);

    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    for (int i = 0; i < FFT_BINS; i++)
    {
        magnitude[i] =
            static_cast<float>(realBuffer[i] / WINDOW_SIZE);
    }
}

void prepareConditionVector(Environment env, float *conditionOutput)
{
    if (!conditionOutput)
        return;

    conditionOutput[0] = (env == ENV_FLOOR) ? 1.0f : 0.0f;
    conditionOutput[1] = (env == ENV_TABLE) ? 1.0f : 0.0f;
}

void prepareModelInput(
    const float *magnitudeX,
    const float *magnitudeY,
    const float *magnitudeZ,
    float *modelInput)
{
    /*
    Input:
    - FFT magnitude of X, Y, and Z axes.
    - Output model input buffer.

    Output:
    - 128 x 3 normalized model features.

    Why:
    - Match the preprocessing used during model training:
      FFT bins 11..138 -> sqrt -> per-axis L2 normalization.
    */

    if (
        magnitudeX == nullptr ||
        magnitudeY == nullptr ||
        magnitudeZ == nullptr ||
        modelInput == nullptr)
    {
        return;
    }

    float featureX[FEATURE_COUNT];
    float featureY[FEATURE_COUNT];
    float featureZ[FEATURE_COUNT];

    float normX = 0.0f;
    float normY = 0.0f;
    float normZ = 0.0f;

    // Extract bins 11..138 and apply root compression
    for (int i = 0; i < FEATURE_COUNT; i++)
    {
        int bin = FEATURE_START_BIN + i;

        featureX[i] =
            sqrtf(fmaxf(magnitudeX[bin], 0.0f));

        featureY[i] =
            sqrtf(fmaxf(magnitudeY[bin], 0.0f));

        featureZ[i] =
            sqrtf(fmaxf(magnitudeZ[bin], 0.0f));

        normX += featureX[i] * featureX[i];
        normY += featureY[i] * featureY[i];
        normZ += featureZ[i] * featureZ[i];
    }

    // Calculate per-axis L2 norm for optimization later
    const float invNormX = 1.0f / (sqrtf(normX) + EPSILON);
    const float invNormY = 1.0f / (sqrtf(normY) + EPSILON);
    const float invNormZ = 1.0f / (sqrtf(normZ) + EPSILON);

    // I fix this to match the architecture of our model:
    for (int i = 0; i < FEATURE_COUNT; i++)
    {
        modelInput[0 * FEATURE_COUNT + i] = featureX[i] * invNormX;
        modelInput[1 * FEATURE_COUNT + i] = featureY[i] * invNormY;
        modelInput[2 * FEATURE_COUNT + i] = featureZ[i] * invNormZ;
    }
}