import argparse
from pathlib import Path
import numpy as np
import pandas as pd

from signal_conditioning import (
    apply_bandpass_filter,
    condition_window,
)

from windowing import (
    SAMPLING_RATE_HZ,
    WINDOW_SIZE,
    create_windows,
    validate_dataframe,
)


EXPECTED_FFT_BINS = (WINDOW_SIZE // 2) + 1
NYQUIST_FREQUENCY_HZ = SAMPLING_RATE_HZ / 2
FREQUENCY_RESOLUTION_HZ = SAMPLING_RATE_HZ / WINDOW_SIZE
START_BIN_INDEX = 11
END_BIN_INDEX = 138

AXIS_COLUMN_MAP = {
    "x": "accX_raw",
    "y": "accY_raw",
    "z": "accZ_raw",
}


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="FFT feature engineering for MPU6050 vibration data."
    )

    parser.add_argument(
        "--input",
        required=True,
        help="Path to one validated raw CSV file.",
    )

    parser.add_argument(
        "--axis",
        choices=["z", "xyz"],
        default="xyz",
        help="FFT axis mode. Default: xyz.",
    )

    parser.add_argument(
        "--output",
        default=None,
        help="Optional output CSV path.",
    )

    return parser.parse_args()

# We turn normalize_energy=False since we move per-window L2 Regularization to notebook
def compute_fft(signal, normalize_energy=False):
    signal = np.asarray(signal, dtype=float)
    if len(signal) != WINDOW_SIZE:
        raise ValueError(f"Invalid signal length: {len(signal)}, expected {WINDOW_SIZE}")

    windowed_signal = condition_window(signal) * np.hanning(WINDOW_SIZE)
    fft_values = np.fft.rfft(windowed_signal)
    frequencies = np.fft.rfftfreq(WINDOW_SIZE, d=1.0 / SAMPLING_RATE_HZ)
    magnitudes = np.abs(fft_values) / WINDOW_SIZE

    if normalize_energy:
        norm = np.linalg.norm(magnitudes, ord=2) + 1e-8
        magnitudes = magnitudes / norm

    return frequencies, magnitudes


def validate_fft_output(
    frequencies,
    magnitudes,
):
    if len(frequencies) != EXPECTED_FFT_BINS:
        raise ValueError(
            f"Invalid FFT bin count: {len(frequencies)}, "
            f"expected {EXPECTED_FFT_BINS}"
        )

    if len(magnitudes) != EXPECTED_FFT_BINS:
        raise ValueError(
            f"Invalid magnitude count: {len(magnitudes)}, "
            f"expected {EXPECTED_FFT_BINS}"
        )

    if not np.isclose(
        frequencies[-1],
        NYQUIST_FREQUENCY_HZ,
    ):
        raise ValueError(
            f"Invalid max frequency: {frequencies[-1]} Hz, "
            f"expected {NYQUIST_FREQUENCY_HZ} Hz"
        )

    if np.isnan(magnitudes).any():
        raise ValueError(
            "NaN detected in FFT magnitude output."
        )

    if np.isinf(magnitudes).any():
        raise ValueError(
            "Infinite value detected in FFT magnitude output."
        )


def get_selected_axes(axis_mode):
    if axis_mode == "z":
        return ["z"]

    return ["x", "y", "z"]


def create_fft_feature_row(
    source_file,
    window_id,
    start_index,
    end_index,
    label,
    window,
    selected_axes,
):
    feature_row = {
        "source_file": source_file,
        "window_id": window_id,
        "start_index": start_index,
        "end_index_exclusive": end_index,
        "label": label,
    }

    for axis in selected_axes:
        column_name = AXIS_COLUMN_MAP[axis]

        frequencies, magnitudes = compute_fft(
            window[column_name].values,
            normalize_energy=False
        )

        validate_fft_output(
            frequencies,
            magnitudes,
        )

    for bin_index in range(START_BIN_INDEX, END_BIN_INDEX + 1):
      feature_row[f"fft_{axis}_{bin_index}"] = float(magnitudes[bin_index])

    return feature_row


def process_file(
    input_path,
    axis_mode,
    output_path=None,
):

    input_path = Path(
        input_path
    )

    if not input_path.exists():
        raise FileNotFoundError(
            f"Input file not found: {input_path}"
        )

    df = pd.read_csv(
        input_path
    )

    label = validate_dataframe(
        df,
        input_path,
    )

    for column_name in [
        "accX_raw",
        "accY_raw",
        "accZ_raw",
    ]:
        df[column_name] = apply_bandpass_filter(
            df[column_name].values,
            SAMPLING_RATE_HZ,
        )

    selected_axes = get_selected_axes(
        axis_mode
    )

    feature_rows = []

    for (
        window_id,
        start_index,
        end_index,
        window,
    ) in create_windows(df):

        feature_row = create_fft_feature_row(
            source_file=input_path.name,
            window_id=window_id,
            start_index=start_index,
            end_index=end_index,
            label=label,
            window=window,
            selected_axes=selected_axes,
        )

        feature_rows.append(
            feature_row
        )

    if not feature_rows:
        raise ValueError(
            "No FFT feature rows were created."
        )

    feature_df = pd.DataFrame(
        feature_rows
    )

    if output_path is None:
        output_dir = (
            Path(__file__).resolve().parent
            / "output"
        )

        output_dir.mkdir(
            parents=True,
            exist_ok=True,
        )

        output_path = (
            output_dir
            / f"{input_path.stem}_fft_{axis_mode}.csv"
        )

    else:
        output_path = Path(
            output_path
        )

        output_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

    feature_df.to_csv(
        output_path,
        index=False,
    )

    print("=" * 60)
    print("FFT FEATURE ENGINEERING COMPLETE")
    print("=" * 60)
    print(f"Input file        : {input_path}")
    print(f"Label             : {label}")
    print(f"Axis mode         : {axis_mode}")
    print(f"Selected axes     : {selected_axes}")
    print(f"Sampling rate     : {SAMPLING_RATE_HZ} Hz")
    print(f"Window size       : {WINDOW_SIZE}")
    print(f"FFT bins / axis   : {EXPECTED_FFT_BINS}")
    print(
        f"Frequency range   : "
        f"0 - {NYQUIST_FREQUENCY_HZ:.1f} Hz"
    )
    print(
        f"Frequency res.    : "
        f"{FREQUENCY_RESOLUTION_HZ:.6f} Hz/bin"
    )
    print(f"Feature rows      : {len(feature_df)}")
    print(f"Output columns    : {len(feature_df.columns)}")
    print(f"Output file       : {output_path}")
    print("=" * 60)

    return feature_df


def main():

    args = parse_arguments()

    process_file(
        input_path=args.input,
        axis_mode=args.axis,
        output_path=args.output,
    )


if __name__ == "__main__":
    main()