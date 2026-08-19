import argparse
import csv
import json
import math
from pathlib import Path
from statistics import median


def _parse_float(value, column_name):
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Invalid numeric value in '{column_name}': {value!r}") from exc


def load_accelerometer_csv(path):
    samples = []
    with open(path, newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        if not reader.fieldnames:
            raise ValueError("CSV file must include a header")

        required = {"ax", "ay", "az"}
        missing = required - set(reader.fieldnames)
        if missing:
            raise ValueError(f"Missing required columns: {', '.join(sorted(missing))}")

        for row in reader:
            ax = _parse_float(row.get("ax"), "ax")
            ay = _parse_float(row.get("ay"), "ay")
            az = _parse_float(row.get("az"), "az")
            magnitude = math.sqrt((ax * ax) + (ay * ay) + (az * az))
            samples.append(magnitude)

    if not samples:
        raise ValueError("No data rows found in CSV input")
    return samples


def window_rms_features(magnitudes, window_size=32, stride=16):
    if window_size <= 0 or stride <= 0:
        raise ValueError("window_size and stride must be positive")
    if len(magnitudes) < window_size:
        raise ValueError("Not enough data to create at least one window")

    features = []
    for start in range(0, len(magnitudes) - window_size + 1, stride):
        window = magnitudes[start : start + window_size]
        rms = math.sqrt(sum(value * value for value in window) / window_size)
        mean = sum(window) / window_size
        variance = sum((value - mean) ** 2 for value in window) / window_size
        std = math.sqrt(variance)
        features.append((rms, std))
    return features


def _quantile(sorted_values, q):
    if not sorted_values:
        raise ValueError("Cannot compute quantile of empty data")
    if q <= 0:
        return sorted_values[0]
    if q >= 1:
        return sorted_values[-1]
    idx = q * (len(sorted_values) - 1)
    lo = int(math.floor(idx))
    hi = int(math.ceil(idx))
    if lo == hi:
        return sorted_values[lo]
    ratio = idx - lo
    return (sorted_values[lo] * (1 - ratio)) + (sorted_values[hi] * ratio)


def _anomaly_scores(features, center_rms, center_std, scale_rms, scale_std):
    scores = []
    for rms, std in features:
        z_rms = abs(rms - center_rms) / scale_rms
        z_std = abs(std - center_std) / scale_std
        scores.append(math.sqrt((z_rms * z_rms) + (z_std * z_std)))
    return scores


def train_profile(features, contamination=0.05):
    if not 0 < contamination < 0.5:
        raise ValueError("contamination must be in (0, 0.5)")
    if not features:
        raise ValueError("No features available for training")

    rms_values = [item[0] for item in features]
    std_values = [item[1] for item in features]
    center_rms = median(rms_values)
    center_std = median(std_values)

    mad_rms = median([abs(value - center_rms) for value in rms_values])
    mad_std = median([abs(value - center_std) for value in std_values])
    scale_rms = max(1e-9, 1.4826 * mad_rms)
    scale_std = max(1e-9, 1.4826 * mad_std)

    scores = _anomaly_scores(features, center_rms, center_std, scale_rms, scale_std)
    threshold = _quantile(sorted(scores), 1 - contamination)

    return {
        "center_rms": center_rms,
        "center_std": center_std,
        "scale_rms": scale_rms,
        "scale_std": scale_std,
        "threshold": threshold,
        "window_size": 32,
        "stride": 16,
    }


def predict_windows(features, model):
    scores = _anomaly_scores(
        features,
        model["center_rms"],
        model["center_std"],
        model["scale_rms"],
        model["scale_std"],
    )
    return [1 if score > model["threshold"] else 0 for score in scores], scores


def save_model(path, model):
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(model, indent=2), encoding="utf-8")


def load_model(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _train_command(args):
    magnitudes = load_accelerometer_csv(args.input)
    features = window_rms_features(magnitudes, window_size=args.window_size, stride=args.stride)
    model = train_profile(features, contamination=args.contamination)
    model["window_size"] = args.window_size
    model["stride"] = args.stride
    save_model(args.model, model)
    print(f"Saved model to {args.model}")


def _predict_command(args):
    model = load_model(args.model)
    magnitudes = load_accelerometer_csv(args.input)
    features = window_rms_features(
        magnitudes,
        window_size=int(model.get("window_size", 32)),
        stride=int(model.get("stride", 16)),
    )
    labels, _scores = predict_windows(features, model)
    for label in labels:
        print(label)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Anomaly detection for DC fan faults from accelerometer data"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    train_parser = subparsers.add_parser("train", help="Train model on mostly-normal data")
    train_parser.add_argument("--input", required=True, help="Path to accelerometer CSV")
    train_parser.add_argument("--model", required=True, help="Output model file path")
    train_parser.add_argument("--window-size", type=int, default=32)
    train_parser.add_argument("--stride", type=int, default=16)
    train_parser.add_argument("--contamination", type=float, default=0.05)
    train_parser.set_defaults(func=_train_command)

    predict_parser = subparsers.add_parser("predict", help="Predict anomaly windows")
    predict_parser.add_argument("--input", required=True, help="Path to accelerometer CSV")
    predict_parser.add_argument("--model", required=True, help="Model file path")
    predict_parser.set_defaults(func=_predict_command)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
