import csv
import tempfile
import unittest
from pathlib import Path

import fan_anomaly


def _write_csv(path, rows):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["ax", "ay", "az"])
        writer.writeheader()
        writer.writerows(rows)


class FanAnomalyTests(unittest.TestCase):
    def test_train_and_predict_detects_injected_anomaly(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmpdir = Path(tmp)
            train_csv = tmpdir / "train.csv"
            test_csv = tmpdir / "test.csv"
            model_path = tmpdir / "model.json"

            normal_rows = [{"ax": 0.1, "ay": 0.2, "az": 0.3} for _ in range(128)]
            _write_csv(train_csv, normal_rows)

            test_rows = [{"ax": 0.1, "ay": 0.2, "az": 0.3} for _ in range(64)]
            test_rows.extend({"ax": 6.0, "ay": 5.5, "az": 6.5} for _ in range(64))
            _write_csv(test_csv, test_rows)

            fan_anomaly.main(
                [
                    "train",
                    "--input",
                    str(train_csv),
                    "--model",
                    str(model_path),
                    "--window-size",
                    "32",
                    "--stride",
                    "16",
                ]
            )

            self.assertTrue(model_path.exists())
            model = fan_anomaly.load_model(model_path)
            magnitudes = fan_anomaly.load_accelerometer_csv(test_csv)
            features = fan_anomaly.window_rms_features(
                magnitudes,
                window_size=model["window_size"],
                stride=model["stride"],
            )
            labels, _scores = fan_anomaly.predict_windows(features, model)

            self.assertGreater(sum(labels), 0)

    def test_missing_required_columns_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmpdir = Path(tmp)
            bad_csv = tmpdir / "bad.csv"

            with open(bad_csv, "w", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=["ax", "ay"])
                writer.writeheader()
                writer.writerow({"ax": 1.0, "ay": 2.0})

            with self.assertRaises(ValueError):
                fan_anomaly.load_accelerometer_csv(bad_csv)


if __name__ == "__main__":
    unittest.main()
