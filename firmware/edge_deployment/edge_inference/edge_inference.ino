#include <Chirale_TensorFlowLite.h>

#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include "../model_data.h"


constexpr int TENSOR_ARENA_SIZE = 40 * 1024;

alignas(16) uint8_t tensorArena[TENSOR_ARENA_SIZE];

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;

TfLiteTensor* inputSignal = nullptr;
TfLiteTensor* conditionInput = nullptr;

TfLiteTensor* reconstructionOutput = nullptr;
TfLiteTensor* latentOutput = nullptr;


void setup() {
    Serial.begin(921600);
    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("EDGE MODEL TEST");
    Serial.println("==============================");

    // Load model
    model = tflite::GetModel(main_model_tflite);

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("ERROR: TFLite schema mismatch.");
        return;
    }

    Serial.println("Model schema: PASS");

    // Register all available TFLite Micro operators
    static tflite::AllOpsResolver resolver;

    // Create interpreter
    static tflite::MicroInterpreter staticInterpreter(
        model,
        resolver,
        tensorArena,
        TENSOR_ARENA_SIZE
    );

    interpreter = &staticInterpreter;

    // Allocate tensors
    TfLiteStatus allocateStatus = interpreter->AllocateTensors();

    if (allocateStatus != kTfLiteOk) {
        Serial.println("ERROR: AllocateTensors failed.");
        Serial.println("Tensor arena may be too small.");
        return;
    }

    Serial.println("AllocateTensors: PASS");

    Serial.print("Inputs: ");
    Serial.println(interpreter->inputs_size());

    Serial.print("Outputs: ");
    Serial.println(interpreter->outputs_size());

    // Get model tensors
    inputSignal = interpreter->input(0);
    conditionInput = interpreter->input(1);

    reconstructionOutput = interpreter->output(0);
    latentOutput = interpreter->output(1);

    // Validate tensor count
    if (interpreter->inputs_size() != 2) {
        Serial.println("ERROR: Expected 2 model inputs.");
        return;
    }

    if (interpreter->outputs_size() != 2) {
        Serial.println("ERROR: Expected 2 model outputs.");
        return;
    }

    Serial.println("Tensor count: PASS");

    // Fill input_signal [1, 3, 128] with dummy zeros
    for (int i = 0; i < 3 * 128; i++) {
        inputSignal->data.f[i] = 0.0f;
    }

    // Fill condition [1, 2] with dummy values
    conditionInput->data.f[0] = 0.0f;
    conditionInput->data.f[1] = 0.0f;

    Serial.println("Dummy inputs: READY");

    // Run inference
    unsigned long startMicros = micros();

    TfLiteStatus invokeStatus = interpreter->Invoke();

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

    // Print first output values
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