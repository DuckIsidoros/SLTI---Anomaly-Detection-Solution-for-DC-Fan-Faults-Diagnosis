# Technical Specification: Edge Anomaly Detection Pipeline

## 1. System Architecture Overview

The pipeline executes real-time vibration anomaly detection on an ESP32 dual-core SoC using an MPU-6050 3-axis accelerometer and a quantized Conditional Autoencoder (cAE).

[Core 0: FreeRTOS Task]
MPU-6050 (500 Hz via I2C)
│
▼
Biquad SOS Bandpass Filtering (8–142 Hz)
│
▼
Thread-Safe Ring Buffer (1024 samples) ──(Task Notification: STEP_SIZE=256)──┐
│
┌────────────────────────────────────────────────────────────────────────────┘
▼
[Core 1: loop() Task]
Window Snapshot (512 samples / axis)
│
▼
DSP: DC Bias Removal -> Hann Window -> FFT -> Sqrt Compression -> L2 Norm
│
▼
TFLM Inference: INT8 Conditional Autoencoder (cAE)
│
▼
Reconstruction MSE Calculation
│
├─► [CALIBRATION]: 100-window buffer -> 99th percentile threshold derivation
│
└─► [MONITORING]: Ring-buffer accumulator (10/20 threshold) -> Latched Alarm

---

## 2. Sensor Configuration & Acquisition (Core 0)

- **Sensor**: InvenSense MPU-6050
- **Bus Interface**: I2C (`SDA`: GPIO 21, `SCL`: GPIO 22) @ 100 kHz standard clock
- **Full-Scale Acceleration Range**: ±16g (Sensitivity: 2048 LSB/g, conversion scale: `1.0 / 2048.0f`)
- **Digital Low-Pass Filter (DLPF)**: Mode 3 (44 Hz bandwidth, 4.9 ms delay)
- **Sampling Rate (ODR)**: 500 Hz (`SMPLRT_DIV = 1`, loop period: 2000 µs target)
- **Execution Model**: Dedicated FreeRTOS task (`SensorTask`) pinned to Core 0 (Priority 2, 4096-byte stack)
- **Fault Handling**: Consecutive I2C read failure counter; triggers emergency halt after 10 consecutive dropped read events.

---

## 3. Digital Signal Processing (DSP) Engine

### 3.1 Temporal Pre-filtering

- **Topology**: 4th-order Butterworth bandpass filter implemented via 4 second-order sections (SOS).
- **Structure**: Transposed Direct Form II biquad filter applied per incoming axis sample.
- **Passband**: 8.0 Hz to 142.0 Hz at $F_s = 500\text{ Hz}$.

### 3.2 Sliding Window Buffer

- **Capacity**: 1024 tri-axial samples (power-of-two bitwise masking: `idx & 1023`).
- **Window Size ($N$)**: 512 samples (1.024 seconds).
- **Hop Size / Stride ($H$)**: 256 samples (50% overlap, triggering every 512 ms).
- **Inter-Core Synchronization**: Spinlock critical sections (`portMUX_TYPE windowMux`) protect ring snapshot reads and sample additions. Thread synchronization occurs via lightweight FreeRTOS direct task notifications (`xTaskNotifyGive` / `ulTaskNotifyTake`).

### 3.3 Spectral Feature Extraction

1. **Detrending**: Arithmetic mean computation and in-place subtraction across 512 samples.
2. **Windowing**: Precomputed symmetric Hann window lookup table:
   $$w[n] = 0.5 - 0.5 \cos\left(\frac{2\pi n}{N - 1}\right)$$
3. **Discrete Fourier Transform**: Single-precision `arduinoFFT` producing single-sided complex magnitude scaled by $1/N$.
4. **Band Truncation**: Bins 11 through 138 extracted inclusive (spectral span: ~10.74 Hz to ~134.77 Hz, width: 128 bins/axis).
5. **Compression & Normalization**:
   - Square-root magnitude compression: $x' = \sqrt{\max(x, 0)}$
   - Axis-wise $L_2$ vector normalization:
     $$\hat{x}_i = \frac{x'_i}{\sqrt{\sum (x'_i)^2} + 10^{-8}}$$
6. **Condition Vector**: 2-element one-hot vector indicating mechanical mounting context (`ENV_FLOOR = [1.0, 0.0]`, `ENV_TABLE = [0.0, 1.0]`).

---

## 4. Machine Learning Inference Engine (TFLM)

### 4.1 Topology & Tensor Layout

- **Model Framework**: TensorFlow Lite for Microcontrollers (TFLM).
- **Model Footprint**: 40,328 bytes FlatBuffer stored in Flash (`model_data.h`).
- **Tensor Arena**: 50 KB internal SRAM (`alignas(16)`).
- **Op Resolver**: `MicroMutableOpResolver<6>` (Conv2D, MaxPool2D, FullyConnected, Reshape, Concatenation, Quantize).
- **Input Tensors**:
  - `input_x`: INT8, shape `[1, 1, 128, 3]` (transposed from DSP channel-major layout `[3, 128]` to channels-last layout during quantization).
  - `input_c`: INT8, shape `[1, 2]` (pre-quantized cached condition constants).
- **Output Tensors**:
  - `output_rec`: INT8, shape `[1, 1, 128, 3]` (reconstructed spectrum).
  - `output_latent`: INT8, shape `[1, 16]` (16-dimensional bottleneck representation).

### 4.2 Error Metric & Anomaly Logic

- **Error Metric**: Mean Squared Error (MSE) computed across all 384 dequantized spectral elements:
  $$\text{MSE} = \frac{1}{384} \sum_{i=1}^{384} \left(\text{dequantize}(x_i) - \text{dequantize}(\hat{x}_i)\right)^2$$
- **Dynamic Threshold Calibration**:
  - Sample pool: First 100 healthy runtime windows (`CALIBRATION_WINDOW_COUNT = 100`).
  - Metric: 99th percentile of baseline MSE values (index 98 of sorted calibration array).
  - Multiplier: `THRESHOLD_BOOST = 1.00`.
- **Decision Filter**:
  - Sliding ring buffer window span: 20 windows.
  - Alarm trip condition: $\ge 10$ anomalous windows within the span (cumulative voting filter).
  - Alarm latch hold duration: 3000 ms.
  - Fault episode tracking: Episode activates on first anomalous window; resets after 3 consecutive normal windows.

---

## 5. Memory Footprint

| Memory Region     | Resource Allocation                                 | Size (Bytes) | Placement              |
| :---------------- | :-------------------------------------------------- | :----------- | :--------------------- |
| **Flash**         | TFLM Model Array (`g_model`)                        | 40,328       | `.rodata`              |
| **Flash**         | Program binary & lookup constants                   | Variable     | `.text` / `.rodata`    |
| **Internal SRAM** | Tensor Arena (`tensorArena`)                        | 51,200       | `.bss` (align 16)      |
| **Internal SRAM** | Sensor Ring Buffer (`sampleRing`)                   | 12,288       | `.bss`                 |
| **Internal SRAM** | Window Snapshot (`windowSnapshot`)                  | 6,144        | `.bss`                 |
| **Internal SRAM** | FFT Work Buffers (`fftRealBuffer`, `fftImagBuffer`) | 4,096        | `.bss`                 |
| **Internal SRAM** | Spectrum Magnitudes (`magX`, `magY`, `magZ`)        | 3,084        | `.bss`                 |
| **Internal SRAM** | Model Input / Condition Buffers                     | 1,544        | `.bss`                 |
| **Internal SRAM** | TFLM Interpreter Buffer                             | ~200         | `.bss`                 |
| **Internal SRAM** | Sensor Task FreeRTOS Stack                          | 4,096        | FreeRTOS Heap/Internal |

---

## 6. Performance Benchmarks & Runtime Telemetry

_(Placeholder section — to be populated with hardware execution profiling data)_

- **Core 0 Task Execution**:
  - I2C Transaction Duration (6-byte burst): `[ PLACEHOLDER: e.g., ~750 us ]`
  - Biquad SOS Filtering Overhead (3 axes): `[ PLACEHOLDER: us ]`
- **Core 1 Pipeline Latencies**:
  - Window Snapshot Lock & Copy (`tSnapshotStart`): `[ PLACEHOLDER: us ]`
  - DSP Stage Execution (`dspUs` - Detrend, Hann, 3x FFT, Norm): `[ PLACEHOLDER: us ]`
  - TFLM Inference Execution (`inferenceUs` - Quantize, Invoke, MSE): `[ PLACEHOLDER: us ]`
  - End-to-End Latency (`e2eUs` - Newest sample to decision): `[ PLACEHOLDER: us ]`
- **Detection Profile**:
  - Dynamic Baseline Calibration Duration: `[ PLACEHOLDER: 100 windows * 512 ms = 51.2 s ]`
  - Empirical Fault Detection Delay (`detectionDelayMs`): `[ PLACEHOLDER: ms ]`
  - Active Floor Threshold ($\text{MSE}_{\text{th}}$): `[ PLACEHOLDER ]`
  - Active Table Threshold ($\text{MSE}_{\text{th}}$): `[ PLACEHOLDER ]`
