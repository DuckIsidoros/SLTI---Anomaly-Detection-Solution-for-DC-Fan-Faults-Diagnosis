import argparse
from pathlib import Path

import pandas as pd


SAMPLING_RATE_HZ = 500
WINDOW_SIZE = 512
STEP_SIZE = 256

REQUIRED_COLUMNS = [
    "timestamp_ms",
    "accX_raw",
    "accY_raw",
    "accZ_raw",
    "label",
]


def parse_arguments():
    """
    Input:
    - Command-line arguments.

    Output:
    - Parsed input CSV path.

    Why:
    - Keep the windowing script reusable for different recording files.
    """

    parser = argparse.ArgumentParser(
        description="Create sliding windows from validated MPU6050 data."
    )

    parser.add_argument(
        "--input",
        required=True,
        help="Input CSV file.",
    )

    return parser.parse_args()


def validate_dataframe(df, file_path):
    """
    Input:
    - Raw sensor dataframe.
    - Source file path.

    Output:
    - Normalized session label.

    Why:
    - Prevent invalid data from entering the windowing stage.
    - Each recording file must contain one physical condition only.
    """

    missing_columns = [
        column
        for column in REQUIRED_COLUMNS
        if column not in df.columns
    ]

    if missing_columns:
        raise ValueError(
            f"Missing columns in {file_path}: {missing_columns}"
        )

    if len(df) < WINDOW_SIZE:
        raise ValueError(
            f"Not enough samples: {len(df)} "
            f"(minimum={WINDOW_SIZE})"
        )

    numeric_columns = [
        "timestamp_ms",
        "accX_raw",
        "accY_raw",
        "accZ_raw",
    ]

    for column in numeric_columns:
        df[column] = pd.to_numeric(
            df[column],
            errors="coerce",
        )

    if df[numeric_columns].isna().any().any():
        raise ValueError(
            f"NaN or non-numeric values detected: {file_path}"
        )

    labels = (
        df["label"]
        .astype(str)
        .str.strip()
        .str.lower()
        .unique()
    )

    if len(labels) != 1:
        raise ValueError(
            f"Expected one label per file, found: {labels}"
        )

    df["label"] = labels[0]

    return labels[0]


def create_windows(df):
    """
    Input:
    - Validated sensor dataframe.

    Output:
    - Sliding windows containing:
      window_id,
      start_index,
      end_index,
      window dataframe.

    Why:
    - FFT requires fixed-length input.
    - 512 samples provide about 1.024 seconds per window at 500 Hz.
    - A 256-sample step gives 50% overlap between consecutive windows.
    """

    window_id = 0

    for start_index in range(
        0,
        len(df) - WINDOW_SIZE + 1,
        STEP_SIZE,
    ):
        end_index = start_index + WINDOW_SIZE

        window = df.iloc[
            start_index:end_index
        ].copy()

        yield (
            window_id,
            start_index,
            end_index,
            window,
        )

        window_id += 1


def process_file(file_path):
    """
    Input:
    - Path to one validated raw CSV file.

    Output:
    - List of generated sliding windows.

    Why:
    - Provide one entry point for loading, validating,
      and windowing a recording session.
    """

    file_path = Path(file_path)

    if not file_path.exists():
        raise FileNotFoundError(
            f"Input file not found: {file_path}"
        )

    df = pd.read_csv(file_path)

    label = validate_dataframe(
        df,
        file_path,
    )

    windows = list(create_windows(df))

    print("=" * 60)
    print("WINDOWING COMPLETE")
    print("=" * 60)
    print(f"Input file      : {file_path}")
    print(f"Label           : {label}")
    print(f"Total samples   : {len(df)}")
    print(f"Sampling rate   : {SAMPLING_RATE_HZ} Hz")
    print(f"Window size     : {WINDOW_SIZE}")
    print(f"Step size       : {STEP_SIZE}")
    print("Overlap         : 50%")
    print(
        f"Window duration : "
        f"{WINDOW_SIZE / SAMPLING_RATE_HZ:.3f} s"
    )
    print(
        f"Step duration   : "
        f"{STEP_SIZE / SAMPLING_RATE_HZ:.3f} s"
    )
    print(f"Total windows   : {len(windows)}")
    print("=" * 60)

    return windows


def main():
    """
    Input:
    - CLI input file path.

    Output:
    - Windowing summary printed to terminal.

    Why:
    - Keep script execution separate from reusable functions.
    """

    args = parse_arguments()

    process_file(
        args.input
    )


if __name__ == "__main__":
    main()