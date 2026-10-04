#include <Chirale_TensorFlowLite.h>

#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include "model_data.h"
#include "dsp_edge.h"

constexpr int TENSOR_ARENA_SIZE = 40 * 1024;

alignas(16) uint8_t tensorArena[TENSOR_ARENA_SIZE];

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;

TfLiteTensor* inputSignal = nullptr;
TfLiteTensor* conditionInput = nullptr;

TfLiteTensor* reconstructionOutput = nullptr;
TfLiteTensor* latentOutput = nullptr;

void testFftCalibration() {
    static float signal[WINDOW_SIZE];
    static float magnitude[FFT_BINS];

    // Generate a deterministic 50 Hz sine wave
    for (int i = 0; i < WINDOW_SIZE; i++) {
        float t = static_cast<float>(i) / SAMPLING_RATE_HZ;

        signal[i] = sinf(
            2.0f * PI * 50.0f * t
        );
    }

    removeDcOffset(signal, WINDOW_SIZE);
    applyHannWindow(signal, WINDOW_SIZE);

    computeFFTMagnitude(
        signal,
        magnitude,
        WINDOW_SIZE
    );

    Serial.println("FFT_CALIBRATION_BEGIN");

    // Print only the important area around 50 Hz
    for (int i = 47; i <= 55; i++) {
        Serial.print(i);
        Serial.print(",");
        Serial.println(magnitude[i], 8);
    }

    Serial.println("FFT_CALIBRATION_END");
}

void testBandpassCalibration() {
    resetBandpassState();

    Serial.println("BANDPASS_CALIBRATION_BEGIN");

    for (int i = 0; i < 20; i++) {
        float t = static_cast<float>(i) / SAMPLING_RATE_HZ;
        float x = sinf(2.0f * PI * 50.0f * t);

        float y = applyBandpassSample(
            x,
            AXIS_X
        );

        Serial.print(i);
        Serial.print(",");
        Serial.println(y, 8);
    }

    Serial.println("BANDPASS_CALIBRATION_END");
}

void printTensorShape(
    const char* label,
    TfLiteTensor* tensor
) {
    Serial.print(label);

    for (int i = 0; i < tensor->dims->size; i++) {
        Serial.print(tensor->dims->data[i]);

        if (i < tensor->dims->size - 1) {
            Serial.print(" x ");
        }
    }

    Serial.println();
}

void setup() {
    Serial.begin(921600);
    delay(1000);

    /*
     * Calibration tests already passed.
     * Keep them commented during normal model testing
     * to avoid unnecessary Serial output.
     */
    // testBandpassCalibration();
    // testFftCalibration();

    Serial.println();
    Serial.println("==============================");
    Serial.println("EDGE MODEL TEST");
    Serial.println("==============================");

    // Load the TFLite model
    model = tflite::GetModel(main_model_tflite);

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("ERROR: TFLite schema mismatch.");
        return;
    }

    Serial.println("Model schema: PASS");

    // Register TensorFlow Lite Micro operators
    static tflite::AllOpsResolver resolver;

    // Create the interpreter
    static tflite::MicroInterpreter staticInterpreter(
        model,
        resolver,
        tensorArena,
        TENSOR_ARENA_SIZE
    );

    interpreter = &staticInterpreter;

    // Allocate tensor memory
    TfLiteStatus allocateStatus =
        interpreter->AllocateTensors();

    if (allocateStatus != kTfLiteOk) {
        Serial.println("ERROR: AllocateTensors failed.");
        return;
    }

    Serial.println("AllocateTensors: PASS");

    Serial.print("Inputs: ");
    Serial.println(interpreter->inputs_size());

    Serial.print("Outputs: ");
    Serial.println(interpreter->outputs_size());

    /*
     * Actual TFLite tensor order:
     *
     * input(0)  = condition       [1, 2]
     * input(1)  = signal          [1, 128, 3]
     *
     * output(0) = latent          [1, 16]
     * output(1) = reconstruction  [1, 128, 3]
     */
    conditionInput = interpreter->input(0);
    inputSignal = interpreter->input(1);

    latentOutput = interpreter->output(0);
    reconstructionOutput = interpreter->output(1);

    // Print tensor shapes
    printTensorShape(
        "Signal dims: ",
        inputSignal
    );

    printTensorShape(
        "Condition dims: ",
        conditionInput
    );

    printTensorShape(
        "Reconstruction dims: ",
        reconstructionOutput
    );

    printTensorShape(
        "Latent dims: ",
        latentOutput
    );

    // Validate tensor count
    if (interpreter->inputs_size() != 2) {
        Serial.println(
            "ERROR: Expected 2 model inputs."
        );
        return;
    }

    if (interpreter->outputs_size() != 2) {
        Serial.println(
            "ERROR: Expected 2 model outputs."
        );
        return;
    }

    Serial.println("Tensor count: PASS");

    
    static float signalX[WINDOW_SIZE];
    static float signalY[WINDOW_SIZE];
    static float signalZ[WINDOW_SIZE];

    static float magnitudeX[FFT_BINS];
    static float magnitudeY[FFT_BINS];
    static float magnitudeZ[FFT_BINS];

    for (int i = 0; i < WINDOW_SIZE; i++) {
        float t =
            static_cast<float>(i) /
            SAMPLING_RATE_HZ;

        signalX[i] =
            sinf(2.0f * PI * 50.0f * t);

        signalY[i] =
            sinf(2.0f * PI * 80.0f * t);

        signalZ[i] =
            sinf(2.0f * PI * 120.0f * t);
    }

    removeDcOffset(signalX, WINDOW_SIZE);
    removeDcOffset(signalY, WINDOW_SIZE);
    removeDcOffset(signalZ, WINDOW_SIZE);

    applyHannWindow(signalX, WINDOW_SIZE);
    applyHannWindow(signalY, WINDOW_SIZE);
    applyHannWindow(signalZ, WINDOW_SIZE);

    computeFFTMagnitude(
        signalX,
        magnitudeX,
        WINDOW_SIZE
    );

    computeFFTMagnitude(
        signalY,
        magnitudeY,
        WINDOW_SIZE
    );

    computeFFTMagnitude(
        signalZ,
        magnitudeZ,
        WINDOW_SIZE
    );

    prepareModelInput(
        magnitudeX,
        magnitudeY,
        magnitudeZ,
        inputSignal->data.f
    );

    Serial.println("DSP model input: READY");

    /*
     * Temporary dummy environmental condition:
     * [env_floor, env_table]
     */
    conditionInput->data.f[0] = 0.0f;
    conditionInput->data.f[1] = 0.0f;

    Serial.println("Dummy inputs: READY");

    // Run model inference
    unsigned long startMicros = micros();

    TfLiteStatus invokeStatus =
        interpreter->Invoke();

    unsigned long endMicros = micros();

    if (invokeStatus != kTfLiteOk) {
        Serial.println("ERROR: Invoke failed.");
        return;
    }

    Serial.println("Invoke: PASS");

    unsigned long inferenceTime =
        endMicros - startMicros;

    Serial.print("Inference latency: ");
    Serial.print(inferenceTime);
    Serial.println(" us");

    Serial.print("Inference latency: ");
    Serial.print(inferenceTime / 1000.0f);
    Serial.println(" ms");

    // Print example model outputs
    Serial.print("Reconstruction[0]: ");
    Serial.println(
        reconstructionOutput->data.f[0],
        8
    );

    Serial.print("Latent[0]: ");
    Serial.println(
        latentOutput->data.f[0],
        8
    );

    Serial.println("==============================");
    Serial.println("MODEL RUNNING ON EDGE");
    Serial.println("==============================");
}

void loop() {
}