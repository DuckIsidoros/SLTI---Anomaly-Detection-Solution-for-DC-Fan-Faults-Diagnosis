import argparse
from pathlib import Path

import pandas as pd

from feature_pipeline import process_csv_file


def parse_arguments():
    """
    Input:
    - Training CSV files.
    - Validation CSV files.
    - FFT axis mode.
    - Output folder.

    Output:
    - Parsed pipeline configuration.

    Why:
    - Keep training and validation sessions explicitly separated.
    - Prevent accidental data leakage between both datasets.
    """

    parser = argparse.ArgumentParser(
        description="Build separate training and validation FFT datasets."
    )

    parser.add_argument(
        "--train",
        nargs="+",
        required=True,
        help="Raw CSV files used for training.",
    )

    parser.add_argument(
        "--validation",
        nargs="+",
        required=True,
        help="Raw CSV files used for validation.",
    )

    parser.add_argument(
        "--axis",
        choices=["z", "xyz"],
        default="xyz",
        help="FFT axis mode. Default: xyz.",
    )

    parser.add_argument(
        "--output",
        default=str(
            Path(__file__).resolve().parent / "output"
        ),
        help="Output folder.",
    )

    return parser.parse_args()


def build_dataset(file_paths, axis_mode):
    """
    Input:
    - List of raw recording files.
    - FFT axis mode.

    Output:
    - Combined FFT feature dataframe.
    - Processing report dataframe.

    Why:
    - Each recording must be windowed independently before merging.
    - This prevents windows from crossing recording boundaries.
    """

    all_feature_rows = []
    report_rows = []

    for file_name in file_paths:
        file_path = Path(file_name)

        try:
            feature_rows, report_row = process_csv_file(
                file_path=file_path,
                axis_mode=axis_mode,
            )

            all_feature_rows.extend(feature_rows)
            report_rows.append(report_row)

            print(
                f"[PASS] {file_path.name} "
                f"| windows={len(feature_rows)}"
            )

        except Exception as error:
            report_rows.append(
                {
                    "source_file": file_path.name,
                    "label": None,
                    "axis_mode": axis_mode,
                    "total_samples": None,
                    "total_windows": 0,
                    "status": "FAIL",
                    "reason": str(error),
                }
            )

            print(
                f"[FAIL] {file_path.name}: {error}"
            )

    if not all_feature_rows:
        raise ValueError(
            "No valid FFT features were generated."
        )

    return (
        pd.DataFrame(all_feature_rows),
        pd.DataFrame(report_rows),
    )


def main():
    """
    Input:
    - Training and validation raw recording files.

    Output:
    - One training feature CSV.
    - One validation feature CSV.
    - Separate processing reports.

    Why:
    - Produce final ML-ready datasets while keeping
      training and validation data independent.
    """

    args = parse_arguments()

    output_folder = Path(args.output)

    output_folder.mkdir(
        parents=True,
        exist_ok=True,
    )

    print("=" * 70)
    print("TRAINING DATASET")
    print("=" * 70)

    training_df, training_report = build_dataset(
        file_paths=args.train,
        axis_mode=args.axis,
    )

    print("\n" + "=" * 70)
    print("VALIDATION DATASET")
    print("=" * 70)

    validation_df, validation_report = build_dataset(
        file_paths=args.validation,
        axis_mode=args.axis,
    )

    training_path = (
        output_folder
        / f"training_{args.axis}.csv"
    )

    validation_path = (
        output_folder
        / f"validation_{args.axis}.csv"
    )

    training_report_path = (
        output_folder
        / f"training_report_{args.axis}.csv"
    )

    validation_report_path = (
        output_folder
        / f"validation_report_{args.axis}.csv"
    )

    training_df.to_csv(
        training_path,
        index=False,
    )

    validation_df.to_csv(
        validation_path,
        index=False,
    )

    training_report.to_csv(
        training_report_path,
        index=False,
    )

    validation_report.to_csv(
        validation_report_path,
        index=False,
    )

    print("\n" + "=" * 70)
    print("DATASET SPLIT COMPLETE")
    print("=" * 70)
    print(f"Training rows     : {len(training_df)}")
    print(f"Validation rows   : {len(validation_df)}")
    print(f"Training file     : {training_path}")
    print(f"Validation file   : {validation_path}")
    print("=" * 70)


if __name__ == "__main__":
    main()