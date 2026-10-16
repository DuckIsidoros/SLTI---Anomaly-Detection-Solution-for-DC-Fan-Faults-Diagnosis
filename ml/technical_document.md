# Introduction: Edge tinyML vibration anomaly detection pipeline

## 1. Executive summary & problem formulation:

### Problem statement:

Rotating machines have become common in both daily and industry, especially in mechanical industry. In industry, fault diagnosis is usually inefficient since the schedule is fixed and maintaining engineers need to check every single machine even though they may be normal/healthy. To solve this inefficient effort, we propose a tinyML solution for fault anomaly detection and diagnosis with reasonable prices.
This document describes the machine learning architecture for tinyML model, deployed on Edge later. Initially, we implement an 1D-CNN Autoencoder architecture but it has limited accuracy on testing dataset (containing healthy, imbalance and obstruction). To enhance this issue, we propose conditional autoencoder algorithm for this 1D-CNN Autoencoder to minimize the effects of domain shift. This document will discuss the architecture of our 1D-CNN Conditional Autoencoder.

## 2. Physical constraints - ESP32-WROOM-32:

The module specifies 520KB internal SRAM. However, FreeRTOS, other things, leave roughly 200-300 KB continuous RAM for model activation buffers. This MCU also lacks of built-in PSRAM. The flash is 4MB.
Since this module is old, it lacks dedicated neural vector instructions.

## 3. Final Metrics:

- The accuracy after training: 99.57%
- The precision after training: 0.9923
- The F1-Score after training: 0.9962
- The recall after training: 100%

# Challenges:

- Domain shift is the main problem despite the fact that our fan just has one state for the speed. Here are some main volatile variables of this project:
  - Mounting: The surface, where we place our fan on, affects the FFT signal significantly. This is primarily driven by the different characteristics of distinct environments. For example, a dense surface can absorb a small amount of vibration whereas a undense environment transmits its vibration to the fan, causing resonance. To give you more metaphors, a healthy state in one environment can be detected as imbalance in another environment due to the environmental mismatch.
    => In this project, we collect data from 2 different floors and 2 different tables. Adding many environments can make the latent space work unreliably (Area Segmentation for Different Environment).
  - Electrical instability: Household power grids are not always stable. The power can go up or down depending on the number of users, the quality of the transmitting devices.
    => We propose adding Gaussian Noise while training and also implement specialized data augumentation algorithm to minimize overfitting of the model.
  - The essen of autoencoder algorithms: Unlike supervised learning, autoencoder cannot see the fault data to extract distinguished features between environment. Because of that, overfitting is the major problem with training since autoencode algorithms just see the healthy data only.
    => We propose intentional noising algorithms, specialized data augumentation algorithm, and conditional autoencoder algorithm for our project.

## 4. Machine learning pipeline:

### Scaling techniques & Overfitting-prevent techniques:

- Unlike normal problems, we don't use StandardScaler() because:
  - Magnitude or Spectrogram are volatile through orders of magnitude (from 10^{-6} to 10^3). If we use this, the model will learn these dominant signals and skip the others. To give you some details why this is critical, even in the same environment, 2 pieces of data can have different distribution of spectrogram due to domain shift.
    -> If the model learns healthy means "big energy" from bin 30-32, it will fail at real environment since the dominant energy can be shifted between bins.
  - Another reason is that applying StandardScaler() will force small energy distribution to ~0, making them invisible for model to learn.

- The proposed scaling and processing data methods: Root compression and per-window L2 regularization:
  - We use root compression to stabilize dynamic range and prevent noise over-amplification in ML pipelines. It compresses dominant high-amplitude spectral peaks -> Prevents gradient explosion from dominant resonant peaks and overfitting based on dominant peaks.
  - Per-window L2 Regularization normalizes local frame energy, caps transient spikes, forces invariance to loudness. It clamps high-energy impulse spikes (clicks, onsets), preventing activation outliers and gradient instability in early layers. It also prevents overfitting to loudness shifts and distance changes.

- We also apply Gaussian Denoising Algorithm to prevent overfitting. It will add small noise to force model to learn meaningful features rather than learning following identical functions.
- We also design a spectral masking algorithm for forcing models to learn while some bins are hidden. Here is the algorithm:

1. For each batch, there is 50% that 8 bins will be hidden.
2. We hide some bins intentionally (not exceed the bin 128th) to force model to learn without these bins.

### Formulation of 1D CNN conditional Autoencoder:

Input tensor format: (B, 3, 128), with 128 FFT bins per axis.
Conditioning vector: (B, 2). I design one-hot encoder for "env*floor" and "env_table".
MSE: $$\mathcal{L}*{\text{rec}} = \frac{1}{B \cdot C \cdot W} \sum*{b=1}^{B} \sum*{i=1}^{C} \sum*{j=1}^{W} (x*{b,i,j} - \hat{x}\_{b,i,j})^2$$
Anomaly evaluation score: $$e(x, c) = \frac{1}{3 \times 128} \sum_{i=1}^{3} \sum_{j=1}^{128} (x_{i,j} - \hat{x}_{i,j})^2$$
Threshold trigger:

- 1 (Anomaly) if e(x, c) > threshold(c)
- 0 (Normal) if e(x, c) < threshold (c)

! Why do we just care about the right side of the threshold? Because as the chart in the notebook, imbalanced and obstruction faults appear on the right.
! You can find out there are 2 thresholds for these 2 environments. We will implement dynamic thresholds to prevent domain shift in the document of edge inference.

### ML Technique:

In this project, we use the Relational Knowledge Distillation to extract meaningful features from teacher model to student model. Here are the architectures of the 2 models:

#### Teacher model architure:

Encoder & Latent:
$x \ (B, 3, L)$ $\rightarrow$ Conv1d(32) + ReLU $\rightarrow$ Conv1d(64, d=4) + ReLU $\rightarrow$ Conv1d(64, d=8) + ReLU $\rightarrow$ AdaptiveAvgPool1d(8) $\rightarrow$ Flatten $\rightarrow$ feat $(B, 512)$
$[feat, c] \ (B, 514)$ $\rightarrow$ Linear(514, 32) $\rightarrow$ latent $(B, 32)$

Decoder & Output:
$[latent, c] \ (B, 34)$ $\rightarrow$ Linear(34, 512) $\rightarrow$ Reshape $(B, 64, 8)$$(B, 64, 8)$ $\rightarrow$ Upsample(L // 2) $\rightarrow$ Conv1d(32) + ReLU $\rightarrow$ Upsample(L) $\rightarrow$ Conv1d(3) $\rightarrow$ reconstruction $(B, 3, L)$

#### Student model architure:

**Encoder:**

$$
x \ (B, 3, L)
\;\rightarrow\; \text{Conv1d}(16) + \text{BN} + \text{ReLU}
\;\rightarrow\; \text{Conv1d}(32) + \text{BN} + \text{ReLU}
\;\rightarrow\; \text{MaxPool1d}(2)
\;\rightarrow\; \text{Conv1d}(32) + \text{ReLU}
\;\rightarrow\; \text{MaxPool1d}(2)
\;\rightarrow\; \text{Flatten}
\;\rightarrow\; \text{feat} \ (B, 32 \times \lfloor L/4 \rfloor)
$$

$$
[\text{feat}, c]
\;\rightarrow\; \text{Linear}\left(\left(32 \cdot \frac{L}{4}\right) + 2,\ 16\right)
\;\rightarrow\; \text{latent} \ (B, 16)
$$

**Decoder & Output:**

$$
[\text{latent}, c]
\;\rightarrow\; \text{Linear}\left(18,\ 32 \cdot \frac{L}{4}\right)
\;\rightarrow\; \text{Reshape} \ (B, 32, \lfloor L/4 \rfloor)
$$

$$
(B, 32, \lfloor L/4 \rfloor)
\;\rightarrow\; \text{ConvTranspose1d}(32, s=2) + \text{ReLU}
\;\rightarrow\; \text{ConvTranspose1d}(16, s=2) + \text{ReLU}
\;\rightarrow\; \text{Conv1d}(3)
\;\rightarrow\; \text{rec} \ (B, 3, L)
$$

#### RKD Loss Formula:

We learn this technique from a Git repo and implement it according the paper: https://arxiv.org/abs/1904.05068

### 1. Distance-wise Relational Loss:

Given a mini-batch of latent vectors $E = [e_1, e_2, \dots, e_N]^\top \in \mathbb{R}^{N \times d}$, pairwise Euclidean distances are computed efficiently using matrix multiplication:

$$
D\_{ij} = \psi(e_i, e_j) = \sqrt{\max\left(\Vert{}e_i\Vert{}\_2^2 + \Vert{}e_j\Vert{}\_2^2 - 2 \langle e_i, e_j \rangle, \; \epsilon\right)}
$$

To make the metric scale-invariant across different latent spaces, distances are normalized by the average distance of all strictly positive pairs:

$$
\mu = \frac{1}{\vert{}\mathcal{X}_{(2)}\vert{}} \sum_{(i, j) \in \mathcal{X}_{(2)}} D_{ij}, \quad \hat{D}_{ij} = \frac{D_{ij}}{\mu + \epsilon_{\text{scale}}}
$$

The distance loss enforces relative geometric proximity between the student ($\hat{D}^S$) and teacher ($\hat{D}^T$) embeddings using the Huber / Smooth $L_1$ loss ($\ell_\delta$):
$$\mathcal{L}_{\text{Rkd-Dist}} = \frac{1}{N^2} \sum_{i=1}^N \sum_{j=1}^N \ell_{\delta}\left(\hat{D}^S_{ij} - \hat{D}^T_{ij}\right)$$

### 2. Angle-wise Relational Loss ($\mathcal{L}_{\text{Rkd-Angle}}$)

While distances capture pairwise proximity, angles preserve higher-order local topology across triplets $(e_j, e_i, e_k)$ anchored at anchor node $e_i$. The cosine of the angle $\angle(e_j, e_i, e_k)$ is formulated as:

$$
\cos \theta_{jik} = \left\langle \frac{e_j - e_i}{\Vert{}e_j - e_i\Vert{}_2}, \; \frac{e_k - e_i}{\Vert{}e_k - e_i\Vert{}_2} \right\rangle
$$

The angle loss penalizes discrepancies between the teacher's and student's angular orientations across all sample triplets:

$$
\mathcal{L}_{\text{Rkd-Angle}} = \frac{1}{N^3} \sum_{i=1}^N \sum_{j=1}^N \sum_{k=1}^N \ell_{\delta}\left(A^S_{i, j, k} - A^T_{i, j, k}\right)
$$

## 5. Model compressing techniques for edge inference:

For a limited-resources hardware such as ESP32-WROOM-32, model compressing techniques are compulsory.

### Magnitude-Based Pruning:

- I implement iterative pruning L1-norm pruning. To ensure the stability, we prune in 5 rounds with 6% of neurons being cut off for each round.
- For each round, we have to fine-tune for 8 epochs to maintain the accuracy.
- During fine-tuning, we still conduct calculating RKD distance loss and angle loss at every training iteration to ensure the consistency.

### The transformation tricks:

Why do we have to transforms the 1D tensors (B, 3, 128) into 2D tensors (B, 1, 128, 3)?

- Reason:
  - Problem 1: In TFLM library, they mainly support for vision and 2D convolution. Designing 1D operators raise so many bugs.
  - Problem 2: We want to avoid unreliable dynamic reshape.
  - By setting height to 1 and treating width as the temporal/frequency dimension, 2x upsampling was constructed purely using Reshape and Concatenate ((1, W, 1, C) duplicated and concatenated along axis 3 $\to$ (1, 2W, C)), both of which are supported natively in INT8 with zero dynamic allocation.
- Solution: we transform 1D signals into a 2D representation with a dummy spatial dimention (height = 1).

### Post-training quantization to INT8:

Quantization schema: $$q = \text{clip}\left(\left\lfloor \frac{x}{S} \right\rceil + Z, -128, 127\right)$$
Neural network activations have dynamic ranges that cannot be determined by inspecting weights alone. A subset of normal operational data (150–200 training windows) is fed through the model. The converter observes min/max activation ranges at every layer to compute precise scale ($S$) and zero-point ($Z$) parameters.
