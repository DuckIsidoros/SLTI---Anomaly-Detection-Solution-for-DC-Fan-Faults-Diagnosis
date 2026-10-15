import argparse
from pathlib import Path
import pandas as pd
from feature_pipeline import process_csv_file

def parse_args():
    parser = argparse.ArgumentParser(description="Generate dataset")
    parser.add_argument("--input", type=Path, default=Path("data"), help="CSV of raw data")
    parser.add_argument("--axis", choices=["z", "xyz"], default="xyz", help="FFT")
    parser.add_argument("--output", type=Path, default=Path("output"), help="Exported folder")
    parser.add_argument("--healthy-test-fraction", type=float, default=0.10, help="Ratio of normal")
    return parser.parse_args()


def parse_arguments():
  parser = argparse.ArgumentParser(
      description=(
          "Build leak-free FFT train/test datasets for Conditional"
          " Autoencoder."
      )
  )
  parser.add_argument(
      "--input", type=Path, default=Path("data"), help="Raw CSV directory."
  )
  parser.add_argument(
      "--axis", choices=["z", "xyz"], default="xyz", help="FFT axis mode."
  )
  parser.add_argument(
      "--output", type=Path, default=Path("output"), help="Output directory."
  )
  parser.add_argument(
      "--healthy-test-fraction",
      type=float,
      default=0.10,
      help="Test fraction per environment.",
  )
  parser.add_argument(
      "--no-norm",
      action="store_true",
      help="Disable per-window L2 normalization.",
  )
  return parser.parse_args()


def load_and_tag(file_path: Path, env: str, fault: str, axis: str) -> pd.DataFrame:
    if not file_path.is_file():
        raise FileNotFoundError(f"Không tìm thấy: {file_path}")

    features, _ = process_csv_file(file_path, axis)
    df = pd.DataFrame(features).assign(
        env=env,
        env_floor=int(env == "floor"),
        env_table=int(env == "table"),
        label=fault,
        fault_type=fault,
        is_abnormal=int(fault not in ["normal", "healthy"]),
    )

    print(f"[PASS] {file_path.name:<25} | Env: {env:<5} | Fault: {fault:<11} | Windows: {len(df)}")
    return df

def split_recording_block(df: pd.DataFrame, test_frac: float = 0.20):
    total_windows = len(df)
    n_test = max(1, int(round(total_windows * test_frac)))
    split_idx = total_windows - n_test

    train_part = df.iloc[1 : split_idx - 1].copy()
    test_part = df.iloc[split_idx:].copy()

    return train_part, test_part


def split_dataset(
    input_dir: Path, axis: str, test_frac: float = 0.20, seed: int = 42
):
    configs = [
        ("table_healthy.csv", "table", "normal", False),
        ("table_healthy_2.csv", "table", "normal", False),
        ("floor_healthy.csv", "floor", "normal", False),
        ("floor_healthy_2.csv", "floor", "normal", False),
        ("table_imbalance.csv", "table", "imbalanced", True),
        ("table_obstruction.csv", "table", "obstruction", True),
    ]

    train_splits = []
    test_healthy_splits = []
    abnormal_dfs = []

    for fname, env, fault, is_abnormal in configs:
        df = load_and_tag(input_dir / fname, env, fault, axis)

        if is_abnormal:
            # Anomaly recordings go 100% into the test set (drop window 0 for filter settling)
            abnormal_dfs.append(df.iloc[1:])
        else:
            # Chronologically split normal recordings 80/20 with boundary buffer
            train_part, test_part = split_recording_block(
                df, test_frac=test_frac
            )
            train_splits.append(train_part)
            test_healthy_splits.append(test_part)

    # Shuffle rows within training set ONLY (to randomize batches during model training)
    train_df = (
        pd.concat(train_splits, ignore_index=True)
        .sample(frac=1.0, random_state=seed)
        .reset_index(drop=True)
    )

    # Combine healthy test slices with abnormal files
    test_df = (
        pd.concat(test_healthy_splits + abnormal_dfs, ignore_index=True)
        .sample(frac=1.0, random_state=seed)
        .reset_index(drop=True)
    )

    return train_df, test_df


def main():
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    train_df, test_df = split_dataset(args.input, args.axis, args.healthy_test_fraction)

    train_path = args.output / f"training_cae3_{args.axis}.csv"
    test_path = args.output / f"testing_cae3_{args.axis}.csv"
    train_df.to_csv(train_path, index=False)
    test_df.to_csv(test_path, index=False)

    print("\n" + "=" * 50)
    print(f"TRAIN: {len(train_df)} windows | Env: {train_df['env'].value_counts().to_dict()}")
    print(f"TEST : {len(test_df)} windows | Labels: {test_df['fault_type'].value_counts().to_dict()}")
    print("=" * 50)
    print(f"[✓] Exported: {train_path.name} & {test_path.name}")


if __name__ == "__main__":
    main()