#include "dsp_edge.h"
#include <math.h>


constexpr int SOS_SECTIONS = 4;
constexpr int AXIS_COUNT = 3;


struct SosSection {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;

    float z1;
    float z2;
};


const float SOS_COEFFICIENTS[SOS_SECTIONS][5] = {
    {
        0.60894463271567f,
        -1.21788926543134f,
        0.60894463271567f,
        -1.570650306303734f,
        0.623033341607531f
    },
    {
        1.0f,
        2.0f,
        1.0f,
        1.775855808209344f,
        0.790363620107327f
    },
    {
        1.0f,
        -2.0f,
        1.0f,
        -1.7703242211039167f,
        0.828011828639938f
    },
    {
        1.0f,
        2.0f,
        1.0f,
        1.894330930650549f,
        0.909455814866532f
    }
};


SosSection sosFilter[AXIS_COUNT][SOS_SECTIONS];


void resetBandpassState() {
    /*
    Input:
    - None.

    Output:
    - Reset filter state for X, Y, and Z axes.

    Why:
    - Each acceleration axis must maintain
      independent continuous filter state.
    */

    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        for (int section = 0; section < SOS_SECTIONS; section++) {

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
    Axis axis
) {
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

    for (int i = 0; i < SOS_SECTIONS; i++) {
        SosSection& section =
            sosFilter[axis][i];

        float output =
            section.b0 * value
            + section.z1;

        section.z1 =
            section.b1 * value
            - section.a1 * output
            + section.z2;

        section.z2 =
            section.b2 * value
            - section.a2 * output;

        value = output;
    }

    return value;
}