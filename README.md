# SLTI---Anomaly-Detection-Solution-for-DC-Fan-Faults-Diagnosis

Minimal end-to-end anomaly detection for DC fan fault diagnosis using accelerometer data (`ax`, `ay`, `az`).

## Input format

CSV file with header containing:
- `ax`
- `ay`
- `az`

Optional columns are ignored.

## Train a baseline model

```bash
python /home/runner/work/SLTI---Anomaly-Detection-Solution-for-DC-Fan-Faults-Diagnosis/SLTI---Anomaly-Detection-Solution-for-DC-Fan-Faults-Diagnosis/fan_anomaly.py train \
  --input /absolute/path/to/normal_data.csv \
  --model /absolute/path/to/model.json
```

## Detect anomalies

```bash
python /home/runner/work/SLTI---Anomaly-Detection-Solution-for-DC-Fan-Faults-Diagnosis/SLTI---Anomaly-Detection-Solution-for-DC-Fan-Faults-Diagnosis/fan_anomaly.py predict \
  --input /absolute/path/to/data.csv \
  --model /absolute/path/to/model.json
```

The command prints one result per window as:
- `0` for normal
- `1` for anomaly