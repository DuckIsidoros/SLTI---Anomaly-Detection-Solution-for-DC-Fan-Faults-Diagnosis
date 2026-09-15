import numpy as np
from scipy.signal import butter, sosfilt


LOW_CUTOFF_HZ = 20.0
HIGH_CUTOFF_HZ = 240.0
FILTER_ORDER = 4


def remove_dc_offset(signal):
    """
    Input:
    - One acceleration signal window.

    Output:
    - Signal with its mean value removed.

    Why:
    - Remove the DC component caused by gravity,
      sensor bias, or static orientation.
    - Reduce the 0 Hz component before FFT.
    """

    signal = np.asarray(
        signal,
        dtype=float,
    )

    return signal - np.mean(signal)


def apply_bandpass_filter(
    signal,
    sampling_rate_hz,
):
    """
    Input:
    - One continuous acceleration signal.
    - Sampling rate in Hz.

    Output:
    - Bandpass-filtered acceleration signal.

    Why:
    - Suppress low-frequency motion and high-frequency
      noise outside the useful vibration range.
    - Preserve the 20-240 Hz band selected from
      exploratory spectrum analysis.
    """

    signal = np.asarray(
        signal,
        dtype=float,
    )

    nyquist_hz = sampling_rate_hz / 2.0

    low_normalized = (
        LOW_CUTOFF_HZ / nyquist_hz
    )

    high_normalized = (
        HIGH_CUTOFF_HZ / nyquist_hz
    )

    sos = butter(
        FILTER_ORDER,
        [low_normalized, high_normalized],
        btype="bandpass",
        output="sos",
    )

    return sosfilt(
        sos,
        signal,
    )


def condition_window(signal):
    """
    Input:
    - One acceleration signal window.

    Output:
    - Conditioned window signal.

    Why:
    - Apply window-local preprocessing before FFT.
    """

    signal = remove_dc_offset(signal)

    return signal