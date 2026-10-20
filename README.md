# TinyML Anomaly Detection for DC Fan Fault Diagnosis

Real-time, unsupervised vibration anomaly detection on an ESP32-WROOM-32 using an MPU-6050 triaxial accelerometer and an INT8-quantized 1D-CNN Conditional Autoencoder (cAE) to diagnose mechanical faults (imbalance, blade obstruction) across varying mounting surfaces.

## Architecture & Core Concepts

- **System Pattern:** Dual-Core Edge DSP + TinyML Inference Pipeline with ISA-95/UNS MQTT Telemetry.
- **Tech Stack:**
  - **Edge Firmware:** C/C++ (Arduino / ESP-IDF), FreeRTOS, TensorFlow Lite for Microcontrollers (TFLM), `arduinoFFT`.
  - **DSP & ML Pipeline:** Python 3, NumPy, SciPy, Pandas, PyTorch, TensorFlow/TFLite.
- **Data Flow & Execution Pipeline:**
  1. **Ingestion (ESP32 Core 0):** Samples MPU-6050 triaxial acceleration ($\pm 16g$) at $500\text{ Hz}$ over $\text{I}^2\text{C}$, applies a real-time 4th-order Butterworth SOS bandpass filter ($8.0\text{--}142.0\text{ Hz}$), and pushes samples into a thread-safe 1024-sample ring buffer.
  2. **Spectral Extraction (ESP32 Core 1):** Extracts 512-sample sliding windows ($50\%$ overlap / 256-sample stride), removes DC bias, applies a Hann window, computes a 512-point FFT, slices passband bins $11\text{--}138$ ($10.74\text{--}134.77\text{ Hz}$, 128 bins/axis), and applies square-root compression with axis-wise $L_2$ normalization.
  3. **Edge Inference & Decision:** Feeds the $[1, 1, 128, 3]$ INT8 spectrum tensor and a 2D one-hot mounting condition vector (`floor` vs. `table`) into the TFLM cAE. Calculates reconstruction MSE against a dynamically calibrated 99th-percentile baseline (first 100 windows) and triggers a latched alarm if $\ge 10/20$ sliding windows exceed the threshold.

## Key Technical Decisions

| Decision              | Chosen Alternative                                                           | Trade-off / Why                                                                                                                             |
| :-------------------- | :--------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------ |
| **Model Topology**    | 1D-CNN Conditional Autoencoder (cAE) over Standard AE / MLP                  | Conditioning on mounting surface (`floor` vs. `table`) prevents domain-shift false positives without requiring labeled fault data.          |
| **Model Compression** | Relational Knowledge Distillation (RKD) + Iterative $L_1$ Pruning + INT8 PTQ | Compresses model to `40.3 KB` Flash and `50 KB` SRAM arena while retaining **99.57% accuracy** and **0.9962 F1-score**.                     |
| **Tensor Layout**     | 2D Shape `[1, 1, 128, 3]` over 1D `[1, 128, 3]`                              | Bypasses missing/buggy 1D operators in TFLM; enables zero-allocation $2\times$ upsampling via native `Reshape` + `Concatenation`.           |
| **Spectral Scaling**  | $\sqrt{x}$ Compression + Per-Window $L_2$ Norm over `StandardScaler`         | Clamps resonant impulse spikes and enforces invariance to overall vibration loudness across different surfaces.                             |
| **Dataset Splitting** | Chronological Split + 1-Window Overlap Guard + Transient Drop                | Discards window 0 (IIR filter settling) and drops 1 guard window between train/test splits to eliminate $50\%$ sliding-window data leakage. |

## Repository Structure

```text
├── data/                  # Raw 500 Hz triaxial CSV recordings (healthy, imbalance, obstruction)
├── docs/                  # System & ISA-95 MQTT topic/payload specifications
├── dsp/                   # Python offline signal conditioning, FFT extraction, & leak-free splitting
├── firmware/
│   ├── data_collection_fixed_rate/  # 500 Hz hardware-timer serial data logger (.ino)
│   └── edge_deployment/main/        # Dual-core FreeRTOS DSP + TFLM inference firmware
└── ml/                    # Training notebook (RKD, pruning, INT8 quantization) & model artifacts
```

## Prerequisites & Hardware Pinout

- **Python Environment:** Python $\ge 3.10$ with `numpy`, `scipy`, `pandas`, `matplotlib`, `torch`, `tensorflow`.
- **Edge Hardware:** ESP32-WROOM-32 (520 KB SRAM, 4 MB Flash) + InvenSense MPU-6050.

| MPU-6050 Pin  | ESP32 Pin     | Protocol / Notes                                                                           |
| :------------ | :------------ | :----------------------------------------------------------------------------------------- |
| `VCC` / `GND` | `3V3` / `GND` | Power supply                                                                               |
| `SDA`         | `GPIO 21`     | $\text{I}^2\text{C}$ Data ($100\text{ kHz}$ in edge inference, $400\text{ kHz}$ in logger) |
| `SCL`         | `GPIO 22`     | $\text{I}^2\text{C}$ Clock ($\text{I}^2\text{C}$ address `0x68`)                           |

## Quickstart

### 1. Offline DSP & Dataset Generation

Run the spectral feature extraction and leak-free train/test partitioning pipeline from the repository root:

```bash
# Extract 128-bin passband FFT features across all 3 axes
python dsp/feature_pipeline.py --input data --axis xyz --output dsp/output

# Generate leak-free training and testing CSV splits for the cAE
python dsp/dataset_split_pipeline.py --input data --axis xyz --output dsp/output --healthy-test-fraction 0.10
```

### 2. Model Training & Quantization

Open `ml/adam-experience.ipynb` to run Teacher-Student Relational Knowledge Distillation (RKD), iterative magnitude pruning, and INT8 post-training quantization. Exported C byte arrays are saved to `ml/models/model_data.h`.

### 3. Edge Firmware Deployment

1. Open `firmware/edge_deployment/main/main.ino` in the Arduino IDE or PlatformIO.
2. Set the mounting condition constant in `main.ino` (`ENV_TABLE` or `ENV_FLOOR`).
3. Compile and flash to the ESP32, then monitor real-time calibration and anomaly telemetry at `115200` baud:

## MQTT Telemetry Interface (ISA-95 / UNS)

Base topic path: `factory/plant_01/workshop_a/line_02/pump_01/edge_node`

| Sub-topic    | Direction | QoS | Description                                                                    |
| :----------- | :-------- | :-- | :----------------------------------------------------------------------------- |
| `/state`     | Publish   | `1` | Node health (`ONLINE` / `OFFLINE` via LWT) and `free_heap_bytes`               |
| `/inference` | Publish   | `1` | Reconstruction `loss`, active `threshold`, boolean `anomaly`, and `latency_ms` |
| `/fft`       | Publish   | `0` | Continuous passband FFT spectrum array for live dashboard visualization        |
| `/cmd`       | Subscribe | `1` | Remote configuration (`condition`, `stream_fft`, `threshold_override`, reboot) |

## Sub-Module Documentation

- **DSP Specification:** [`dsp/README_dsp.md`](dsp/README_dsp.md)
- **ML & Compression Architecture:** [`ml/technical_document.md`](ml/technical_document.md)
- **Edge Runtime Specification:** [`firmware/edge_deployment/main/README_edge.md`](firmware/edge_deployment/main/README_edge.md)
- **MQTT Topic Hierarchy:** [`docs/mqtt-architecture/btopic-hierarchy.md`](docs/mqtt-architecture/btopic-hierarchy.md)

## License

Code is licensed under the [Apache License 2.0](LICENSE). Vibration dataset files in `data/` are licensed under [CC BY 4.0](data/LICENSE).
