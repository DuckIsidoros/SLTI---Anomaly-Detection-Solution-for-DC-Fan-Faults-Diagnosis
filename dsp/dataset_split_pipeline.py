import argparse
from dataclasses import dataclass
from pathlib import Path
from feature_pipeline import process_csv_file
import numpy as np
import pandas as pd


@dataclass
class DatasetConfig:
  filename: str
  env: str  # 'floor' hoặc 'table'
  fault_type: str  # 'healthy', 'imbalanced', 'obstruction'


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


def normalize_window_energy(
    df: pd.DataFrame, eps: float = 1e-8
) -> pd.DataFrame:
  """Normalize spectral bins per window and axis to unit L2 energy."""
  df = df.copy()
  fft_cols = [c for c in df.columns if c.startswith("fft_")]
  for axis in ["x", "y", "z"]:
    cols = [c for c in fft_cols if c.startswith(f"fft_{axis}_")]
    if cols:
      vals = df[cols].to_numpy(dtype="float32")
      norms = np.linalg.norm(vals, ord=2, axis=1, keepdims=True) + eps
      df[cols] = vals / norms
  return df


def extract_features_with_context(
    config: DatasetConfig, input_folder: Path, axis_mode: str
) -> pd.DataFrame:
  """Trích xuất cửa sổ FFT độc lập cho từng file và gắn metadata môi trường/nhãn."""
  file_path = input_folder / config.filename
  if not file_path.is_file():
    raise FileNotFoundError(f"Missing file: {file_path}")

  features, _ = process_csv_file(file_path, axis_mode)
  df = pd.DataFrame(features)

  # 1. Operational Parameters (X_o) One-Hot Encoding
  df["env_floor"] = 1 if config.env == "floor" else 0
  df["env_table"] = 1 if config.env == "table" else 0

  # 2. Nhãn tương thích hoàn toàn với logic Notebook:
  # - label: dùng chuỗi 'healthy', 'imbalanced', 'obstruction' cho Cell 5 & Cell 7
  # - is_abnormal: 0 (Healthy), 1 (Abnormal) phục vụ phân loại nhị phân
  df["label"] = config.fault_type
  df["is_abnormal"] = 0 if config.fault_type == "healthy" else 1
  df["fault_type"] = config.fault_type
  df["env"] = config.env

  print(
      f"[PASS] {config.filename:<30} | Env: {config.env:<5} | Fault:"
      f" {config.fault_type:<11} | Windows: {len(df)}"
  )
  return df


def create_window_level_cae_split(
    input_folder: Path,
    axis_mode: str = "xyz",
    test_fraction: float = 0.10,
    normalize: bool = True,
    random_state: int = 42,
) -> tuple[pd.DataFrame, pd.DataFrame]:
  """Phân tách dữ liệu an toàn cho mô hình Conditional Autoencoder."""

  healthy_configs = [
      DatasetConfig(
          "healthy_table_raw_1.csv", env="table", fault_type="healthy"
      ),
      DatasetConfig(
          "healthy_table_raw_2.csv", env="table", fault_type="healthy"
      ),
  ]
  abnormal_configs = [
      DatasetConfig(
          "raw_imbalanced_table.csv", env="table", fault_type="imbalanced"
      ),
      DatasetConfig(
          "raw_obstruction_table.csv", env="table", fault_type="obstruction"
      ),
  ]

  healthy_dfs = [
      extract_features_with_context(cfg, input_folder, axis_mode)
      for cfg in healthy_configs
  ]
  abnormal_dfs = [
      extract_features_with_context(cfg, input_folder, axis_mode)
      for cfg in abnormal_configs
  ]

  healthy_df = pd.concat(healthy_dfs, ignore_index=True)
  abnormal_df = pd.concat(abnormal_dfs, ignore_index=True)

  if normalize:
    healthy_df = normalize_window_energy(healthy_df)
    abnormal_df = normalize_window_energy(abnormal_df)

  # Stratified split healthy theo từng môi trường
  train_splits = []
  test_healthy_splits = []

  for env_name in ["floor", "table"]:
    env_subset = (
        healthy_df[healthy_df["env"] == env_name]
        .sample(frac=1.0, random_state=random_state)
        .reset_index(drop=True)
    )

    n_test = max(1, int(round(len(env_subset) * test_fraction)))
    train_splits.append(env_subset.iloc[:-n_test])
    test_healthy_splits.append(env_subset.iloc[-n_test:])

  train_df = (
      pd.concat(train_splits, ignore_index=True)
      .sample(frac=1.0, random_state=random_state)
      .reset_index(drop=True)
  )

  test_healthy_df = pd.concat(test_healthy_splits, ignore_index=True)
  test_df = (
      pd.concat([test_healthy_df, abnormal_df], ignore_index=True)
      .sample(frac=1.0, random_state=random_state)
      .reset_index(drop=True)
  )

  return train_df, test_df


def main():
  args = parse_arguments()
  args.output.mkdir(parents=True, exist_ok=True)

  # Chạy trực tiếp, không cần cờ rào cản
  train_df, test_df = create_window_level_cae_split(
      input_folder=args.input,
      axis_mode=args.axis,
      test_fraction=args.healthy_test_fraction,
      normalize=not args.no_norm,
  )

  # Lưu tên file theo chuẩn cae
  train_path = args.output / f"training_cae3_{args.axis}.csv"
  test_path = args.output / f"testing_cae3_{args.axis}.csv"

  train_df.to_csv(train_path, index=False)
  test_df.to_csv(test_path, index=False)

  print("\n" + "=" * 60)
  print("  TẬP HUẤN LUYỆN (TRAINING - 100% HEALTHY BASELINE)")
  print("=" * 60)
  print(f"Tổng số windows: {len(train_df)}")
  print(f"- Floor windows (env_floor=1): {(train_df['env_floor'] == 1).sum()}")
  print(f"- Table windows (env_table=1): {(train_df['env_table'] == 1).sum()}")

  print("\n" + "=" * 60)
  print("  TẬP KIỂM THỬ (TESTING - BENCHMARK & EVALUATION)")
  print("=" * 60)
  print(f"Tổng số windows: {len(test_df)}")
  print(
      "- Lành tính (label='healthy'):  "
      f" {(test_df['label'] == 'healthy').sum()} (Gồm cả Floor & Table)"
  )
  print(
      "- Bất thường (is_abnormal=1):   "
      f" {(test_df['is_abnormal'] == 1).sum()} (Toàn bộ trên Table)"
  )
  print(f"    + Imbalanced: {(test_df['label'] == 'imbalanced').sum()}")
  print(f"    + Obstruction: {(test_df['label'] == 'obstruction').sum()}")
  print("=" * 60)
  print(f"[✓] Đã xuất file: {train_path.name}")
  print(f"[✓] Đã xuất file: {test_path.name}")


if __name__ == "__main__":
  main()