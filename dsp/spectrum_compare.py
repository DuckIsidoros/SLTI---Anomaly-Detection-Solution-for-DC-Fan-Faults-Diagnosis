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

INPUT_FILES = [
    "floor_healthy.csv",
    "floor_healthy_2.csv",
    "table_healthy.csv",
    "table_healthy_2.csv",
    "table_imbalance.csv",
    "table_obstruction.csv",
]

AXIS_COLUMN_MAP = {
    "x": "accX_raw",
    "y": "accY_raw",
    "z": "accZ_raw",
}

FEATURE_FREQ_MIN = 11 * (SAMPLING_RATE_HZ / WINDOW_SIZE)   
FEATURE_FREQ_MAX = 138 * (SAMPLING_RATE_HZ / WINDOW_SIZE) 

def compute_window_spectrum(signal):
    signal = np.asarray(signal, dtype=float)
    if len(signal) != WINDOW_SIZE:
        raise ValueError(
            f"Invalid signal length: {len(signal)}, expected {WINDOW_SIZE}"
        )

    windowed_signal = condition_window(signal) * np.hanning(WINDOW_SIZE)
    fft_values = np.fft.rfft(windowed_signal)
    frequencies = np.fft.rfftfreq(WINDOW_SIZE, d=1.0 / SAMPLING_RATE_HZ)
    magnitudes = np.abs(fft_values) / WINDOW_SIZE

    return frequencies, magnitudes


def compute_average_spectrum(file_path, axis):
    df = pd.read_csv(file_path)
    validate_dataframe(df, file_path)

    column_name = AXIS_COLUMN_MAP[axis]

    df[column_name] = apply_bandpass_filter(
        df[column_name].values,
        SAMPLING_RATE_HZ,
    )

    spectra = []
    frequencies = None

    for _window_id, _start_index, _end_index, window in create_windows(df):
        frequencies, magnitudes = compute_window_spectrum(
            window[column_name].values
        )
        spectra.append(magnitudes)

    if not spectra:
        raise ValueError(f"No windows created from: {file_path.name}")

    average_spectrum = np.mean(np.asarray(spectra), axis=0)
    return frequencies, average_spectrum


def plot_axis_spectra(axis, input_dir: Path, output_dir: Path):
    plt.figure(figsize=(13, 7))

    for file_name in INPUT_FILES:
        file_path = input_dir / file_name
        if not file_path.exists():
            print(f"[WARN] Bỏ qua file do không tìm thấy: {file_path}")
            continue

        frequencies, average_spectrum = compute_average_spectrum(file_path, axis)
        label = file_path.stem
        plt.plot(frequencies, average_spectrum, label=label, linewidth=1.5)
        print(f"[PASS] {file_name} | axis={axis}")

    plt.axvspan(
        FEATURE_FREQ_MIN,
        FEATURE_FREQ_MAX,
        color="green",
        alpha=0.1,
        label=f"CAE Feature Bins ({FEATURE_FREQ_MIN:.1f} - {FEATURE_FREQ_MAX:.1f} Hz)",
    )

    plt.xlabel("Frequency (Hz)", fontsize=11)
    plt.ylabel("Average FFT Magnitude", fontsize=11)
    plt.title(f"Average Spectrum Comparison (Bandpass 10-135 Hz) - Axis {axis.upper()}", fontsize=13)
    plt.xlim(0, SAMPLING_RATE_HZ / 2)
    plt.grid(True, alpha=0.3)
    plt.legend(loc="upper right")
    plt.tight_layout()

    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"spectrum_{axis}.png"
    plt.savefig(output_path, dpi=150)
    plt.close()

    print(f"[SAVED] {output_path}")


def parse_args():
    parser = argparse.ArgumentParser(description="So sánh phổ tín hiệu sau lọc bandpass.")
    parser.add_argument("--input", type=Path, default=Path("data"), help="Thư mục chứa các file CSV thô.")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("output/spectrum_comparison"),
        help="Thư mục lưu ảnh phổ.",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    print("=" * 70)
    print("SPECTRUM COMPARISON STARTED (BANDPASS: 10 - 135 Hz)")
    print(f"Input dir : {args.input.resolve()}")
    print(f"Output dir: {args.output.resolve()}")
    print("=" * 70)

    for axis in ["x", "y", "z"]:
        plot_axis_spectra(axis, args.input, args.output)

    print("=" * 70)
    print("SPECTRUM COMPARISON COMPLETE")
    print("=" * 70)


if __name__ == "__main__":
    main()