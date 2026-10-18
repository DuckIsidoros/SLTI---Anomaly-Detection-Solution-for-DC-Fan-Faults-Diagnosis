#pragma once

#include "dsp_edge.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // To clear the ring buffer, reset write indices and warm-up state.
    void initWindowManager(void);

    // It applies bandpass filtering, push data to ring buffer and mark the timestamp. If the data is sufficient, return true to run an inference.
    bool windowManagerAddSample(float rawX, float rawY, float rawZ);

    // To extract a complete window from ring buffer.
    bool windowManagerSnapshot(float *outX, float *outY, float *outZ);

    uint32_t windowManagerGetLastSampleTimestamp(void);

#ifdef __cplusplus
}
#endif