from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from signal_conditioning import remove_dc_offset
from windowing import (
    SAMPLING_RATE_HZ,
    WINDOW_SIZE,
    create_windows,
    validate_dataframe,
)


DATASET_FOLDER = Path(__file__).resolve().parent.parent / "dataset"

INPUT_FILES = [
    "raw_healthy_floor_training.csv",
    "raw_healthy_floor_validation.csv",
    "raw_healthy_table_training.csv",
    "raw_healthy_table_validation.csv",
    "raw_imbalanced_table.csv",
    "raw_obstruction_table.csv",
]

AXIS_COLUMN_MAP = {
    "x": "accX_raw",
    "y": "accY_raw",
    "z": "accZ_raw",
}


def compute_window_spectrum(signal):
    """
    Input:
    - One 512-sample acceleration signal.

    Output:
    - Frequency bins.
    - One-sided FFT magnitude spectrum.

    Why:
    - Apply the same DC removal and Hann preprocessing
      used by the feature engineering pipeline.
    - Keep bandpass disabled so the raw spectrum can
      be inspected before choosing cutoff frequencies.
    """

    signal = np.asarray(
        signal,
        dtype=float,
    )

    if len(signal) != WINDOW_SIZE:
        raise ValueError(
            f"Invalid signal length: {len(signal)}, "
            f"expected {WINDOW_SIZE}"
        )

    signal = remove_dc_offset(signal)

    hann_window = np.hanning(WINDOW_SIZE)
    windowed_signal = signal * hann_window

    fft_values = np.fft.rfft(windowed_signal)

    frequencies = np.fft.rfftfreq(
        WINDOW_SIZE,
        d=1.0 / SAMPLING_RATE_HZ,
    )

    magnitudes = np.abs(fft_values) / WINDOW_SIZE

    return frequencies, magnitudes


def compute_average_spectrum(file_path, axis):
    """
    Input:
    - One raw CSV recording.
    - Axis name: x, y, or z.

    Output:
    - Frequency bins.
    - Average FFT magnitude across all windows.

    Why:
    - Reduce window-to-window variation and show the
      overall spectral behavior of one recording.
    """

    df = pd.read_csv(file_path)

    validate_dataframe(
        df,
        file_path,
    )

    column_name = AXIS_COLUMN_MAP[axis]

    spectra = []
    frequencies = None

    for (
        _window_id,
        _start_index,
        _end_index,
        window,
    ) in create_windows(df):

        frequencies, magnitudes = compute_window_spectrum(
            window[column_name].values
        )

        spectra.append(
            magnitudes
        )

    if not spectra:
        raise ValueError(
            f"No windows created from: {file_path.name}"
        )

    spectra = np.asarray(
        spectra
    )

    average_spectrum = np.mean(
        spectra,
        axis=0,
    )

    return frequencies, average_spectrum


def plot_axis_spectra(axis):
    """
    Input:
    - Axis name: x, y, or z.

    Output:
    - Spectrum comparison PNG.

    Why:
    - Compare healthy and fault recordings across the
      full 0-250 Hz range before selecting a bandpass.
    """

    plt.figure(
        figsize=(12, 7)
    )

    for file_name in INPUT_FILES:
        file_path = DATASET_FOLDER / file_name

        if not file_path.exists():
            raise FileNotFoundError(
                f"Dataset file not found: {file_path}"
            )

        frequencies, average_spectrum = compute_average_spectrum(
            file_path,
            axis,
        )

        label = file_path.stem.replace(
            "raw_",
            "",
        )

        plt.plot(
            frequencies,
            average_spectrum,
            label=label,
        )

        print(
            f"[PASS] {file_name} | axis={axis}"
        )

    plt.xlabel(
        "Frequency (Hz)"
    )
    plt.ylabel(
        "Average FFT Magnitude"
    )
    plt.title(
        f"Average Spectrum Comparison - Axis {axis.upper()}"
    )
    plt.xlim(
        0,
        SAMPLING_RATE_HZ / 2,
    )
    plt.grid(
        True,
        alpha=0.3,
    )
    plt.legend()
    plt.tight_layout()

    output_folder = (
        Path(__file__).resolve().parent
        / "output"
        / "spectrum_comparison"
    )

    output_folder.mkdir(
        parents=True,
        exist_ok=True,
    )

    output_path = (
        output_folder
        / f"spectrum_{axis}.png"
    )

    plt.savefig(
        output_path,
        dpi=150,
    )

    plt.close()

    print(
        f"[SAVED] {output_path}"
    )


def main():
    """
    Input:
    - Six predefined raw vibration recordings.

    Output:
    - X, Y, and Z spectrum comparison plots.

    Why:
    - Provide evidence for selecting bandpass cutoff
      frequencies from real project data.
    """

    print("=" * 70)
    print("SPECTRUM COMPARISON STARTED")
    print("=" * 70)

    for axis in ["x", "y", "z"]:
        plot_axis_spectra(
            axis
        )

    print("=" * 70)
    print("SPECTRUM COMPARISON COMPLETE")
    print("=" * 70)


if __name__ == "__main__":
    main()