#include "window_manager.h"
#include <Arduino.h>
#include <string.h>

namespace
{
    // ring buffer capacity declaration.
    constexpr size_t RING_CAPACITY = 1024;
    constexpr size_t RING_MASK = RING_CAPACITY - 1;

    struct TriAxisSample
    {
        float x;
        float y;
        float z;
    };

    TriAxisSample sampleRing[RING_CAPACITY];
    size_t ringWriteIndex = 0;
    size_t ringTotalWritten = 0;
    size_t lastSnapshotTotal = 0;

    // To save the timestamp of the latest data sample.
    uint32_t lastSampleTimestampUs = 0;

    void ringPush(float x, float y, float z)
    {
        const size_t idx = ringWriteIndex & RING_MASK;
        sampleRing[idx].x = x;
        sampleRing[idx].y = y;
        sampleRing[idx].z = z;

        ringWriteIndex++;
        ringTotalWritten++;
    }
}

#ifdef __cplusplus
extern "C"
{
#endif

    void initWindowManager(void)
    {
        memset(sampleRing, 0, sizeof(sampleRing));
        ringWriteIndex = 0;
        ringTotalWritten = 0;
        lastSnapshotTotal = 0;
        lastSampleTimestampUs = 0;
    }

    bool windowManagerAddSample(float rawX, float rawY, float rawZ)
    {
        const float filteredX = applyBandpassSample(rawX, AXIS_X);
        const float filteredY = applyBandpassSample(rawY, AXIS_Y);
        const float filteredZ = applyBandpassSample(rawZ, AXIS_Z);

        ringPush(filteredX, filteredY, filteredZ);

        lastSampleTimestampUs = micros();

        return (ringTotalWritten - lastSnapshotTotal) >= static_cast<size_t>(STEP_SIZE);
    }

    bool windowManagerSnapshot(float *outX, float *outY, float *outZ)
    {
        if (outX == nullptr || outY == nullptr || outZ == nullptr)
        {
            return false;
        }

        if (ringTotalWritten < static_cast<size_t>(WINDOW_SIZE))
        {
            return false;
        }

        const size_t startTotal = ringTotalWritten - static_cast<size_t>(WINDOW_SIZE);
        for (size_t i = 0; i < static_cast<size_t>(WINDOW_SIZE); i++)
        {
            const size_t idx = (startTotal + i) & RING_MASK;
            outX[i] = sampleRing[idx].x;
            outY[i] = sampleRing[idx].y;
            outZ[i] = sampleRing[idx].z;
        }

        lastSnapshotTotal = ringTotalWritten;
        return true;
    }

    uint32_t windowManagerGetLastSampleTimestamp(void)
    {
        return lastSampleTimestampUs;
    }

#ifdef __cplusplus
}
#endif