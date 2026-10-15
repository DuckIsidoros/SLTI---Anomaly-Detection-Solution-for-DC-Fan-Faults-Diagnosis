import argparse
from pathlib import Path

import pandas as pd

from fft_processor import (
    create_fft_feature_row,
    get_selected_axes,
)

from signal_conditioning import (
    apply_bandpass_filter,
)

from windowing import (
    SAMPLING_RATE_HZ,
    create_windows,
    validate_dataframe,
)


def parse_arguments():

    parser = argparse.ArgumentParser(
        description="Complete MPU6050 feature engineering pipeline."
    )

    parser.add_argument(
        "--input",
        required=True,
        help="Folder containing validated raw CSV files.",
    )

    parser.add_argument(
        "--axis",
        choices=["z", "xyz"],
        default="xyz",
        help="FFT axis mode. Default: xyz.",
    )

    parser.add_argument(
        "--output",
        default="dsp/output",
        help="Output folder. Default: dsp/output.",
    )

    return parser.parse_args()

# Extract information from the files' names
def extract_metadata_from_filename(filename: str) -> dict:
  name_lower = filename.lower()

  env = "floor" if "floor" in name_lower else "table"
  env_floor = 1 if env == "floor" else 0
  env_table = 1 if env == "table" else 0

  if "healthy" in name_lower:
    fault_type = "normal"
  elif "imbalance" in name_lower:
    fault_type = "imbalanced"
  elif "obstruction" in name_lower:
    fault_type = "obstruction"
  else:
    fault_type = "normal"

  is_abnormal = 0 if fault_type == "normal" else 1

  return {
      "env": env,
      "env_floor": env_floor,
      "env_table": env_table,
      "fault_type": fault_type,
      "is_abnormal": is_abnormal,
  }

# Process FFT and add labels
def process_csv_file(file_path, axis_mode):
  file_path = Path(file_path)
  df = pd.read_csv(file_path)

  label = validate_dataframe(
      df,
      file_path,
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

  selected_axes = get_selected_axes(axis_mode)

  metadata = extract_metadata_from_filename(file_path.name)

  feature_rows = []

  for window_id, start_index, end_index, window in create_windows(df):
    feature_row = create_fft_feature_row(
        source_file=file_path.name,
        window_id=window_id,
        start_index=start_index,
        end_index=end_index,
        label=label,
        window=window,
        selected_axes=selected_axes,
    )

    feature_row.update(metadata)

    feature_rows.append(feature_row)

  report_row = {
      "source_file": file_path.name,
      "label": metadata["fault_type"],
      "env": metadata["env"],
      "axis_mode": axis_mode,
      "total_samples": len(df),
      "total_windows": len(feature_rows),
      "status": "PASS",
      "reason": "valid",
  }

  return feature_rows, report_row


def run_pipeline(
    input_folder,
    axis_mode,
    output_folder,
):

    input_folder = Path(
        input_folder
    )

    output_folder = Path(
        output_folder
    )

    if not input_folder.exists():
        raise FileNotFoundError(
            f"Input folder not found: {input_folder}"
        )

    if not input_folder.is_dir():
        raise ValueError(
            f"Input path is not a folder: {input_folder}"
        )

    csv_files = sorted(
        input_folder.glob("*.csv")
    )

    if not csv_files:
        raise ValueError(
            f"No CSV files found in: {input_folder}"
        )

    output_folder.mkdir(
        parents=True,
        exist_ok=True,
    )

    all_feature_rows = []
    report_rows = []

    print("=" * 70)
    print("FEATURE ENGINEERING PIPELINE STARTED")
    print("=" * 70)
    print(f"Input folder : {input_folder}")
    print(f"Axis mode    : {axis_mode}")
    print(f"CSV files    : {len(csv_files)}")
    print("=" * 70)

    for file_path in csv_files:
        try:
            feature_rows, report_row = process_csv_file(
                file_path=file_path,
                axis_mode=axis_mode,
            )

            all_feature_rows.extend(
                feature_rows
            )

            report_rows.append(
                report_row
            )

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
            "No FFT features were generated."
        )

    feature_df = pd.DataFrame(
        all_feature_rows
    )

    report_df = pd.DataFrame(
        report_rows
    )

    feature_output_path = (
        output_folder
        / f"features_{axis_mode}.csv"
    )

    report_output_path = (
        output_folder
        / f"feature_report_{axis_mode}.csv"
    )

    feature_df.to_csv(
        feature_output_path,
        index=False,
    )

    report_df.to_csv(
        report_output_path,
        index=False,
    )

    print("\n" + "=" * 70)
    print("FEATURE ENGINEERING PIPELINE COMPLETE")
    print("=" * 70)
    print(f"Processed files : {len(csv_files)}")
    print(
        f"Passed files    : "
        f"{(report_df['status'] == 'PASS').sum()}"
    )
    print(
        f"Failed files    : "
        f"{(report_df['status'] == 'FAIL').sum()}"
    )
    print(f"Feature rows    : {len(feature_df)}")
    print(f"Output columns  : {len(feature_df.columns)}")
    print(f"Feature file    : {feature_output_path}")
    print(f"Report file     : {report_output_path}")
    print("=" * 70)


def main():

    args = parse_arguments()

    run_pipeline(
        input_folder=args.input,
        axis_mode=args.axis,
        output_folder=args.output,
    )


if __name__ == "__main__":
    main()