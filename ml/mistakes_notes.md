### 1st Design of the ML Pipeline:

The first proposed ML algorithm is "Dense Autoencoder" since we want to experience with feature engineering.
However, as CSE students, our knowledge in signal processing is limited so we don't pursue this path anymore!

- Performance: NULL (Since we just develop algorithms, haven't run it yet!)
- Challenges:
  - Develop mathematical functions for feature engineering without domain knowledge.

### 2nd Design of the ML Pipeline:

The second propose of ML algorthms shift our project from Dense AE to 1D-CNN Autoencoder with these details:

- Reason: CNN is powerful for learning deep features inside the datasets
- However, we still develop FFT algorithms to process the data
- In this phase, we rely on StandardScaler() and also apply Relational-based Knowledge Distillation but it failed because we didn't observe the thresholds of density distribution.
- Challenges:
  - Low accuracy ~49% (All are abnormal triggers)
  - Basic scaling methods
  - Over-engineering for ML

### 3rd Design of the ML Pipeline:

The third phase we aim to minimize the ML model, fix bugs of Knowledge Distillation and set thresholds based on percentiles and observation from density charts

- Performance: ~96% (For deploying in the same environment)
- Scaling methods: Add root compression to treat signals more fairly
- Set thresholds based on percentiles
- Challenges:
  - Good acurracy for identical environment but low accuracy (approx 70%) for others.

### 4th Design of the ML Pipeline (In this push):

The fourth phase we aim to research for better scaling methods, developing dynamic thresholds for environments, research and implement Conditional AE
