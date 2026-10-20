# Edge TinyML Vibration Anomaly Detection Pipeline

## 1. Executive Summary & Problem Formulation

Rotating industrial equipment (e.g., DC cooling fans, motors) requires automated predictive maintenance to eliminate manual inspection overhead and prevent catastrophic mechanical failure. Conventional vibration monitoring often relies on high-power computing or cloud telemetry, which introduces bandwidth, latency, and hardware cost barriers.

This document specifies an end-to-end TinyML vibration anomaly detection pipeline deployed on resource-constrained microcontrollers (ESP32-WROOM-32). The model detects mechanical anomalies (blade imbalance, physical obstruction) from 3-axis Fast Fourier Transform (FFT) vibration signatures. To address domain shift caused by varying mounting surfaces (e.g., floor vs. table), we implement a **Conditional 1D-CNN Autoencoder (C-AE)** trained via **Relational Knowledge Distillation (RKD)**, followed by structured pruning and full INT8 post-training quantization.

### Project Artifacts & Tracking

- **Weights & Biases Run:** [`student-c-ae-distillation-run-1`](https://wandb.ai/duckisidoros-vietnamese-german-university/TinyML-Driven%20Predictive%20Maintenance%20for%20DC%20Fans%20Using%20ESP32%20and%20FFT%20Vibration%20Analysis/runs/1dzxfccb)
- **Target Hardware:** Espressif ESP32-WROOM-32 (Xtensa dual-core 32-bit LX6, 520 KB internal SRAM, 4 MB external SPI Flash, no hardware vector extensions, ~200–300 KB available contiguous heap under FreeRTOS)
- **Deliverables:** Quantized TFLite flatbuffer (`student_esp32_int8.tflite` — **39.38 KB**), Flash-mapped C array header (`model_data.h` — **40,328 bytes**).

---

## 2. Evaluation Metrics Summary

Evaluated on an independent test dataset of **2,102 samples** containing healthy operation, mechanical imbalance, and external airflow obstruction across multiple mounting domains (`env_floor`, `env_table`):

| Pipeline Stage                  | Model Size               | Accuracy | Precision | Recall  | F1-Score | ROC-AUC |
| :------------------------------ | :----------------------- | :------- | :-------- | :------ | :------- | :------ |
| **PyTorch Student C-AE (FP32)** | 86.92 KB (`.pth`)        | 99.71%   | 99.49%    | 100.00% | 0.9974   | 1.0000  |
| **Pruned Student C-AE (FP32)**  | 86.99 KB (`.pth`)        | 99.90%   | 99.83%    | 100.00% | 0.9991   | 1.0000  |
| **Quantized INT8 TFLite**       | **39.38 KB** (`.tflite`) | 99.86%   | 99.77%    | 100.00% | 0.9988   | 1.0000  |

- **Threshold Formulation:** Recalibrated 99.0th percentile of healthy validation reconstruction errors ($e(x, c)$) per environment:
  - `ENV_FLOOR`: $\tau_{\text{floor}} = 0.000589$ (FP32)
  - `ENV_TABLE`: $\tau_{\text{table}} = 0.000591$ (FP32)

---

## 3. Data Processing & Noise Robustness

### Signal Representation

- **Raw Input:** Tri-axial accelerometer FFT bins ($X, Y, Z$) spanning bins 11 to 138 (128 discrete frequency bins per axis $\to 384$ total features).
- **Conditioning Vector ($c$):** 2-element one-hot vector indicating mounting context: $[c_{\text{floor}}, c_{\text{table}}]$.
- **Tensor Dimensions:** $(B, 3, 128)$.

### Normalization Pipeline

1. **Root Compression:** Replaces standard logarithmic or z-score transforms:
   $$x_{\text{root}} = \sqrt{\max(x_{\text{raw}}, 0)}$$
   _Justification:_ Vibration spectral magnitudes fluctuate over several orders of magnitude ($10^{-6}$ to $10^{3}$). Root compression dampens high-amplitude resonant peaks without collapsing small-energy harmonic bins to zero, preventing gradient explosion while retaining harmonic sidebands.
2. **Per-Window $L_2$ Normalization:** Applied along the 128-bin frequency axis for each axis independently:
   $$x_{\text{norm}} = \frac{x_{\text{root}}}{\Vert{}x_{\text{root}}\Vert{}_2 + \epsilon}, \quad \epsilon = 10^{-8}$$
   _Justification:_ Invariant to baseline shifts in global vibration intensity (loudness shifts, sensor distance discrepancies).

### Training Data Augmentation

- **Gaussian Noise Layer:** Additive white Gaussian noise $\mathcal{N}(0, \sigma^2)$ with $\sigma = 0.05$ injected during training forward passes to break identity mappings.
- **Spectral Masking:** Random contiguous frequency masking applied with probability $p = 0.5$ per sample:
  - Random mask width $w \in [1, 8]$ bins.
  - Random start index $s \in [0, 128 - w]$.
  - Channels $X, Y, Z$ at $[s : s + w]$ are zeroed out.
  - Forces the autoencoder to reconstruct missing harmonic components from adjacent spatial and cross-axis correlations.

---

## 4. Model Architectures & Knowledge Distillation

### 4.1 Teacher Model Architecture (Capacity: Latent Dim = 32)

Designed to extract global harmonic representations across wide receptive fields:

- **Input:** $x \in \mathbb{R}^{B \times 3 \times 128}$, $c \in \mathbb{R}^{B \times 2}$
- **Encoder:**
  - $\text{Conv1D}(3 \to 32, k=5, p=2) \to \text{ReLU}$
  - $\text{Conv1D}(32 \to 64, k=3, \text{dilation}=4, p=4) \to \text{ReLU}$
  - $\text{Conv1D}(64 \to 64, k=3, \text{dilation}=8, p=8) \to \text{ReLU}$
  - $\text{AdaptiveAvgPool1D}(8) \to \text{Flatten} \to \text{feat} \in \mathbb{R}^{B \times 512}$
- **Latent Bottleneck:**
  - $[\text{feat}, c] \in \mathbb{R}^{B \times 514} \to \text{Linear}(514 \to 32) \to z_T \in \mathbb{R}^{B \times 32}$
- **Decoder:**
  - $[z_T, c] \in \mathbb{R}^{B \times 34} \to \text{Linear}(34 \to 512) \to \text{Reshape}(B, 64, 8)$
  - $\text{Upsample}(\times 8 \to 64) \to \text{Conv1D}(64 \to 32, k=3, p=1) \to \text{ReLU}$
  - $\text{Upsample}(\times 2 \to 128) \to \text{Conv1D}(32 \to 3, k=3, p=1) \to \hat{x}_T \in \mathbb{R}^{B \times 3 \times 128}$

---

### 4.2 Student Model Architecture (ESP32-Targeted: Latent Dim = 16)

Optimized for low-latency memory execution with reduced channels and pooled spatial widths:

```text
Input Tensor (B, 3, 128)
 │
 ├──> Conv1D(3 -> 16, k=5, pad=2) + BatchNorm1D(16) + ReLU
 ├──> Conv1D(16 -> 32, k=3, pad=1) + BatchNorm1D(32) + ReLU
 ├──> MaxPool1D(kernel=2, stride=2)                     --> (B, 32, 64)
 ├──> Conv1D(32 -> 32, k=3, pad=1) + ReLU
 ├──> MaxPool1D(kernel=2, stride=2)                     --> (B, 32, 32)
 ├──> Conv1D(32 -> 8, k=1) + ReLU [Channel Bottleneck]  --> (B, 8, 32)
 ├──> Flatten                                          --> (B, 256)
 │
 ├──> Concat with Condition c (B, 2)                   --> (B, 258)
 ├──> Dense Linear(258 -> 16)                           --> Latent z_S (B, 16)
 │
 ├──> Concat with Condition c (B, 2)                   --> (B, 18)
 ├──> Dense Linear(18 -> 256)                          --> (B, 256)
 ├──> Reshape(8, 32)                                   --> (B, 8, 32)
 ├──> Conv1D(8 -> 32, k=1) + ReLU [Channel Expansion]   --> (B, 32, 32)
 ├──> Upsample(scale=2, nearest)                        --> (B, 32, 64)
 ├──> Conv1D(32 -> 32, k=3, pad=1) + ReLU
 ├──> Upsample(scale=2, nearest)                        --> (B, 32, 128)
 ├──> Conv1D(32 -> 16, k=3, pad=1) + ReLU
 └──> Conv1D(16 -> 3, k=3, pad=1)                      --> Output x_rec (B, 3, 128)
```

---

### 4.3 Relational Knowledge Distillation (RKD) Objective

Instead of pointwise feature mimicry (which overconstrains a small student model), RKD transfers the geometric manifold topology of the teacher's latent embeddings:

**Distance-wise Relational Loss ($\mathcal{L}_{\text{Rkd-Dist}}$):**  
For latent batch embeddings $E = [e_1, \dots, e_N]^\top$, pairwise Euclidean distances $D_{ij} = \Vert{}e_i - e_j\Vert{}_2$ are normalized by the batch mean pairwise distance $\mu$ to ensure scale invariance:
$$\hat{D}_{ij} = \frac{D_{ij}}{\frac{1}{N^2 - N} \sum_{i \ne j} D_{ij} + \epsilon}$$
$$\mathcal{L}_{\text{Rkd-Dist}} = \frac{1}{N^2} \sum_{i=1}^N \sum_{j=1}^N \ell_{\delta}\left(\hat{D}^S_{ij} - \hat{D}^T_{ij}\right) \quad (\ell_\delta = \text{Smooth } L_1)$$

**Angle-wise Relational Loss ($\mathcal{L}_{\text{Rkd-Angle}}$):**  
Preserves high-order structural relations across sample triplets $(e_j, e_i, e_k)$ anchored at $e_i$:
$$A_{jik} = \left\langle \frac{e_j - e_i}{\Vert{}e_j - e_i\Vert{}_2}, \; \frac{e_k - e_i}{\Vert{}e_k - e_i\Vert{}_2} \right\rangle$$
$$\mathcal{L}_{\text{Rkd-Angle}} = \frac{1}{N^3} \sum_{i=1}^N \sum_{j=1}^N \sum_{k=1}^N \ell_{\delta}\left(A^S_{jik} - A^T_{jik}\right)$$

**Composite Student Training Loss:**
$$\mathcal{L}_{\text{total}} = \mathcal{L}_{\text{MSE}}(s_{\text{rec}}(\tilde{x}, c), x) + \mathcal{L}_{\text{Rkd-Dist}}(z_S, z_T) + 2.0 \cdot \mathcal{L}_{\text{Rkd-Angle}}(z_S, z_T)$$

Reconstruction loss is evaluated on spectrally masked inputs ($\tilde{x}$), while distillation losses are evaluated on clean inputs ($x$).

**Training Hyperparameters:** AdamW ($\text{lr} = 10^{-3}$, $\beta = (0.9, 0.999)$, $\text{weight decay} = 10^{-2}$ on 2D weights; no decay on biases/norms), Cosine Annealing scheduler ($T_{\max}=300$, $\eta_{\min}=10^{-5}$), gradient clipping at $\text{max\_norm} = 5.0$. Early stopping triggered at Epoch 230 (best validation reconstruction loss: 0.00033).

---

## 5. Model Compression & Edge Deployment

### 5.1 Magnitude-Based Structured Pruning

- **Strategy:** L1-norm unstructured pruning targeting convolutional layers (`enc.0`, `enc.3`, `enc.7`).
- **Schedule:** 5 consecutive rounds, pruning 6% of target weights per round followed by 8 fine-tuning epochs per round ($\text{lr} = 10^{-4}$) maintaining the full RKD distillation loss.
- **Sparsity Progression:**
  - Round 1: 6.0% Sparsity | Val MSE: 0.000325
  - Round 2: 11.6% Sparsity | Val MSE: 0.000327
  - Round 3: 16.9% Sparsity | Val MSE: 0.000327
  - Round 4: 21.9% Sparsity | Val MSE: 0.000324
  - Round 5: 26.6% Sparsity | Val MSE: 0.000327

The pruned student model achieved 99.90% Accuracy and 0.9991 F1-Score on the evaluation set. Pruning masks were folded directly into weights via `prune.remove()`.

---

### 5.2 2D Tensor Mapping for TensorFlow Lite for Microcontrollers (TFLM)

TFLM kernel support on microcontrollers is heavily optimized for 2D operations (`CONV_2D`, `MAX_POOL_2D`). Direct 1D convolutions frequently trigger runtime errors or force dynamic tensor allocations (`EXPAND_DIMS`, `STRIDED_SLICE`).

- **Transformation:** Input signals are mapped to 2D tensors with a dummy spatial height dimension:
  $$(B, 3, 128) \longrightarrow (B, 1, 128, 3) \quad (\text{Batch}, \text{Height}=1, \text{Width}=128, \text{Channels}=3)$$
- **1D Kernels as 2D Kernels:** 1D Conv kernels of size $K$ become 2D kernels of shape $(1, K)$.
- **Zero-Allocation 2x Upsampling:** Replaced unsupported `ResizeNearestNeighbor` and dynamic `EXPAND_DIMS`/`TILE` ops by combining static Reshape and Concatenation:

```python
def upsample_2x_width(tensor, width, channels):
    t_expanded = tf.keras.layers.Reshape((1, width, 1, channels))(tensor)
    t_dup = tf.keras.layers.Concatenate(axis=3)([t_expanded, t_expanded])
    return tf.keras.layers.Reshape((1, 2 * width, channels))(t_dup)
```

- **Parity Test:** PyTorch vs. Keras 2D graph output divergence confirmed:
  - Reconstruction Max Absolute Error: **$1.52 \times 10^{-6}$**
  - Latent Max Absolute Error: **$1.19 \times 10^{-7}$**

---

### 5.3 Post-Training Quantization (INT8)

- **Quantization Scheme:** Symmetric weight, asymmetric activation quantization mapping FP32 values $x \in [\alpha, \beta]$ to INT8 $q \in [-128, 127]$:
  $$q = \text{clip}\left(\left\lfloor \frac{x}{S} \right\rceil + Z, -128, 127\right)$$
- **Calibration:** 200 normal training samples fed through the model to record dynamic activation ranges and lock per-layer scale ($S$) and zero-point ($Z$) constants.
- **Model Storage:**
  - Flash Model Binary (`student_esp32_int8.tflite`): **39.38 KB**
  - C Array (`model_data.h`): **40,328 bytes** declared with `alignas(16)`:

    ```c
    #ifndef MODEL_DATA_H_
    #define MODEL_DATA_H_
    #include <stdint.h>

    const unsigned int g_model_len = 40328;
    alignas(16) const unsigned char g_model[] = {
        0x1c, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, ...
    };
    #endif
    ```

  - `alignas(16)` enables the ESP32 memory-management unit to execute model weights directly from external SPI Flash via memory-mapped caching, requiring **0 KB of internal SRAM** for static weight storage.

---

### 5.4 TFLM Operator Compliance Verification

To guarantee deterministic execution on the ESP32 micro-tensor arena without falling back to software emulation or memory faults, all operations were verified against the strict TFLM operator allowlist:

- **Verified Present Operators:** `CONV_2D`, `MAX_POOL_2D`, `FULLY_CONNECTED`, `RESHAPE`, `CONCATENATION`, `QUANTIZE`.
- **Forbidden Operators Banned:** Zero instances of `EXPAND_DIMS`, `TILE`, `SHAPE`, `PACK`, or `STRIDED_SLICE`.
