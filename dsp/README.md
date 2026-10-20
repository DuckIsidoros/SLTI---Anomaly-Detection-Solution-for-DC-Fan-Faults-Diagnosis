# MPU6050 Vibration Preprocessing Pipeline — Technical Specification

## 1. Pipeline Overview

This pipeline transforms raw triaxial accelerometer recordings (MPU6050, sampled at $f_s = 500\text{ Hz}$) into leak-free spectral feature datasets for unsupervised anomaly detection via a Conditional Autoencoder (CAE).

**Execution Flow:**

1. **Data Validation (`windowing.py`)** $\rightarrow$ Schema, length, numeric integrity, and label consistency checks.
2. **Continuous Signal Conditioning (`signal_conditioning.py`)** $\rightarrow$ Causal 4th-order Butterworth bandpass filtering ($8.0\text{–}142.0\text{ Hz}$) across each full recording.
3. **Temporal Segmentation (`windowing.py`)** $\rightarrow$ Sliding window extraction ($N = 512$ samples, $50\%$ overlap).
4. **Spectral Feature Extraction (`fft_processor.py`, `feature_pipeline.py`)** $\rightarrow$ Per-window DC removal, Hann tapering, real FFT, passband bin selection ($k \in [11, 138]$), and metadata tagging.
5. **Leak-Free Dataset Partitioning (`dataset_split_pipeline.py`)** $\rightarrow$ Transient rejection, chronological train/test splitting with an overlap guard window, and test-only routing for fault recordings.

## 2. Module Specifications & Signal Processing Parameters

| Stage                        | Module                   | Key Parameters                                                                                                                                                               | Mathematical / Algorithmic Operation                                                                                                                                                                                                                           |
| :--------------------------- | :----------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1. Validation**            | `windowing.py`           | Required cols: `timestamp_ms`, `accX_raw`, `accY_raw`, `accZ_raw`, `label`<br>Min length: $N \ge 512$                                                                        | Coerces sensor channels to float, rejects files with `NaN` values, and enforces a single normalized label per file.                                                                                                                                            |
| **2. Bandpass Filter**       | `signal_conditioning.py` | $f_s = 500\text{ Hz}$ ($f_N = 250\text{ Hz}$)<br>Passband: $[8.0, 142.0]\text{ Hz}$<br>Order: $4$ (SOS format)                                                               | Designs a 4th-order Butterworth bandpass filter (`scipy.signal.butter`). Initializes step-response state via $\mathbf{z}_i = \text{sosfilt\_zi}(\text{SOS}) \cdot x[0]$ and applies forward IIR filtering (`sosfilt`) to continuous channels before windowing. |
| **3. Windowing**             | `windowing.py`           | Window $N = 512$ ($1.024\text{ s}$)<br>Stride $S = 256$ ($0.512\text{ s}$)<br>Overlap: $50\%$                                                                                | Segments continuous filtered time series into overlapping frames indexed by $m$: $x_m[n] = x[mS + n]$ for $n \in [0, N-1]$.                                                                                                                                    |
| **4. Spectral Analysis**     | `fft_processor.py`       | $\Delta f = \frac{500}{512} \approx 0.9766\text{ Hz/bin}$<br>Total rFFT bins: $257$<br>Retained bins: $k \in [11, 138]$ ($128\text{ bins}$, $10.74\text{–}134.77\text{ Hz}$) | Removes per-window mean $\tilde{x}_m[n] = x_m[n] - \mu_m$, applies a Hann window $w[n]$, and computes the normalized one-sided magnitude spectrum:<br>$M_m[k] = \frac{1}{N}\left\|\sum_{n=0}^{N-1} \tilde{x}_m[n]w[n]e^{-j2\pi kn/N}\right\|$                  |
| **5. Conditioning & Labels** | `feature_pipeline.py`    | Environments: `floor`, `table`<br>Classes: `normal`, `imbalanced`, `obstruction`                                                                                             | Encodes one-hot environment conditioning variables (`env_floor`, `env_table` $\in \{0,1\}$) and binary anomaly targets (`is_abnormal` $\in \{0,1\}$) from filenames.                                                                                           |
| **6. Visual Diagnostics**    | `spectrum_compare.py`    | Axes: `x`, `y`, `z`<br>Highlight band: $10.7\text{–}134.8\text{ Hz}$                                                                                                         | Plots ensemble-averaged FFT magnitude spectra across all windows per recording file to verify passband energy concentration.                                                                                                                                   |

## 3. Data Leakage Prevention & Partitioning Protocol

Because adjacent windows overlap by $50\%$ ($256$ shared samples) and causal IIR filtering introduces startup transients, `dataset_split_pipeline.py` enforces three partitioning constraints across the six source recordings (`table_healthy`, `table_healthy_2`, `floor_healthy`, `floor_healthy_2`, `table_imbalance`, `table_obstruction`):

1. **IIR Startup Transient Rejection:** Window $0$ (`df.iloc[0]`) is discarded from every recording (`df.iloc[1:]`) so filter settling artifacts do not contaminate training or evaluation.
2. **Chronological Split with Overlap Guard Band:** For each healthy recording of $W$ windows and test fraction $\alpha$ (default $\alpha = 0.10$ via CLI; $0.20$ in function default), let $N_{\text{test}} = \max(1, \text{round}(\alpha W))$ and $i_{\text{split}} = W - N_{\text{test}}$:
   - **Training Partition:** Windows $m \in [1, i_{\text{split}} - 2]$ (`df.iloc[1 : split_idx - 1]`).
   - **Guard Window (Dropped):** Window $m = i_{\text{split}} - 1$ is omitted to eliminate the $256$-sample overlap between the final training window and first testing window.
   - **Testing Partition (Healthy):** Windows $m \in [i_{\text{split}}, W - 1]$ (`df.iloc[split_idx:]`).
3. **Anomaly Isolation:** Fault recordings (`imbalanced`, `obstruction`) are excluded from training; $100\%$ of their post-transient windows ($m \in [1, W - 1]$) are routed to the test set, followed by independent random shuffling (`seed = 42`) of `training_cae3_{axis}.csv` and `testing_cae3_{axis}.csv`.

## 4. Output Schema

| Column Group                   | Column Names                                                              | Count                  | Description                                                        |
| :----------------------------- | :------------------------------------------------------------------------ | :--------------------- | :----------------------------------------------------------------- |
| **Window Metadata**            | `source_file`, `window_id`, `start_index`, `end_index_exclusive`, `label` | $5$                    | Recording origin and sample index bounds $[mS, mS+512)$.           |
| **Spectral Features**          | `fft_{axis}_11` $\dots$ `fft_{axis}_138`                                  | $128 \text{ per axis}$ | Normalized rFFT magnitudes across $10.74\text{–}134.77\text{ Hz}$. |
| **CAE Conditioning & Targets** | `env`, `env_floor`, `env_table`, `fault_type`, `is_abnormal`              | $5$                    | One-hot surface condition inputs and ground-truth anomaly labels.  |

## 5. Implementation Audit Notes

Review of the source scripts identified three minor implementation discrepancies to note before execution:

- **Multi-Axis Feature Loop Indentation (`fft_processor.py`, lines 148–149):** In `create_fft_feature_row`, the `for bin_index in range(START_BIN_INDEX, END_BIN_INDEX + 1):` loop is indented outside the `for axis in selected_axes:` loop. Consequently, `axis="xyz"` only writes the final axis (`z`) bins ($128$ features instead of $384$) to `feature_row`. Indenting lines 148–149 by one level inside `for axis in selected_axes:` restores full triaxial feature extraction.
- **Missing Imports (`spectrum_compare.py`):** `argparse`, `apply_bandpass_filter`, and `condition_window` are invoked but not imported at the top of `spectrum_compare.py`, and the plot title references a `10-135 Hz` bandpass while `signal_conditioning.py` configures an `8.0-142.0 Hz` filter.
- **Duplicate Argument Parser (`dataset_split_pipeline.py`):** Both `parse_args()` and `parse_arguments()` are defined; `main()` invokes `parse_args()`.
