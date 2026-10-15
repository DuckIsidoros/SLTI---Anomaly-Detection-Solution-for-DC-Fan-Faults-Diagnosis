import numpy as np
from scipy.signal import butter, sosfilt, sosfilt_zi
from dataclasses import dataclass

DEFAULT_LOW_CUTOFF_HZ = 8.0
DEFAULT_HIGH_CUTOFF_HZ = 142.0
DEFAULT_FILTER_ORDER = 4


@dataclass(frozen=True)
class BandpassConfig:

  low_cutoff_hz: float = DEFAULT_LOW_CUTOFF_HZ
  high_cutoff_hz: float = DEFAULT_HIGH_CUTOFF_HZ
  order: int = DEFAULT_FILTER_ORDER


def remove_dc_offset(signal: np.ndarray) -> np.ndarray:
  signal = np.asarray(signal, dtype=np.float64)
  return signal - np.mean(signal)


def design_bandpass_sos(
    sampling_rate_hz: float,
    config: BandpassConfig = BandpassConfig(),
) -> np.ndarray:
  nyquist_hz = sampling_rate_hz / 2.0

  if config.low_cutoff_hz <= 0:
    raise ValueError(
        f"Low cutoff ({config.low_cutoff_hz} Hz) must be greater than 0."
    )
  if config.high_cutoff_hz >= nyquist_hz:
    raise ValueError(
        f"High cutoff ({config.high_cutoff_hz} Hz) must be less than Nyquist"
        f" ({nyquist_hz} Hz)."
    )
  if config.low_cutoff_hz >= config.high_cutoff_hz:
    raise ValueError(
        f"Low cutoff ({config.low_cutoff_hz} Hz) must be less than high cutoff"
        f" ({config.high_cutoff_hz} Hz)."
    )

  low_norm = config.low_cutoff_hz / nyquist_hz
  high_norm = config.high_cutoff_hz / nyquist_hz

  return butter(
      config.order,
      [low_norm, high_norm],
      btype="bandpass",
      output="sos",
  )


def apply_bandpass_filter(
    signal: np.ndarray,
    sampling_rate_hz: float,
    config: BandpassConfig = BandpassConfig(),
) -> np.ndarray:
  signal = np.asarray(signal, dtype=np.float64)
  sos = design_bandpass_sos(sampling_rate_hz, config)

  zi = sosfilt_zi(sos) * signal[0]
  filtered_signal, _ = sosfilt(sos, signal, zi=zi)

  return filtered_signal


def condition_window(signal: np.ndarray) -> np.ndarray:
  return remove_dc_offset(signal)