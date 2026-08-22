#include <Wire.h>

#define I2C_SDA 21
#define I2C_SCL 22
#define IMU_ADDRESS 0x68
#define IMU_REG_PWR_MGMT_1 0x6B
#define IMU_REG_ACCEL_CONFIG 0x1C
// MPU6050 registers required for FIFO and interrupt acquisition
#define IMU_INT_PIN 27
#define IMU_REG_FIFO_EN      0x23
#define IMU_REG_INT_ENABLE   0x38
#define IMU_REG_USER_CTRL    0x6A
#define IMU_REG_FIFO_COUNT_H 0x72
#define IMU_REG_FIFO_R_W     0x74

const float ACCEL_SCALE = 1.0f / 2048.0f; // +- 16g
struct AccelData
{
    int16_t x;
    int16_t y;
    int16_t z;
};
const char *CURRENT_LABEL = "imbalanced";

volatile bool samplingTriggered = false;

void IRAM_ATTR onMPUInterrupt()
{
    samplingTriggered = true;
}

/*
Input:
- MPU6050 register address
- Value to write

Output:
- true if the I2C write succeeds
- false if the I2C write fails

Why:
- Avoid duplicated I2C register-write code.
- Keep MPU6050 configuration minimal and readable.
*/
bool IMU_WriteRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(reg);
    Wire.write(value);

    return Wire.endTransmission() == 0;
}
/*
Input:
- MPU6050 FIFO count register

Output:
- Number of bytes currently stored in FIFO

Why:
- ESP32 must know how much data is available before reading FIFO.
- Prevents reading incomplete accelerometer samples.
*/
uint16_t IMU_GetFIFOCount()
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_FIFO_COUNT_H);
    if (Wire.endTransmission(false) != 0)
        return 0;

    if (Wire.requestFrom(IMU_ADDRESS, 2) < 2)
        return 0;

    uint16_t fifo_count = (Wire.read() << 8) | Wire.read();
    return fifo_count;
}
/*
Input:
- MPU6050 FIFO data register

Output:
- One accelerometer sample (X, Y, Z)

Why:
- Each accelerometer sample stored in FIFO contains 6 bytes:
  2 bytes for X, 2 for Y, and 2 for Z.
*/
bool IMU_ReadFIFO(AccelData &data)
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_FIFO_R_W);

    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(IMU_ADDRESS, 6) < 6)
        return false;

    data.x = (Wire.read() << 8) | Wire.read();
    data.y = (Wire.read() << 8) | Wire.read();
    data.z = (Wire.read() << 8) | Wire.read();

    return true;
}
bool IMU_Init()
{
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    delay(50);

    if(!IMU_WriteRegister(IMU_REG_PWR_MGMT_1, 0x00)) // Wake up the MPU6050
        return false;
    if(!IMU_WriteRegister(IMU_REG_ACCEL_CONFIG, 0x18)) // Set accelerometer range to +-16g
        return false;
    /*
    Input:
    - MPU6050 FIFO control registers

    Output:
    - FIFO reset and enabled
    - Accelerometer XYZ samples are routed into FIFO

    Why:
    - FIFO buffers sensor samples before ESP32 reads them.
    - This prepares acquisition for interrupt-based sampling.
    */
    if(!IMU_WriteRegister(IMU_REG_USER_CTRL, 0x04)) 
        return false;
    if(!IMU_WriteRegister(IMU_REG_USER_CTRL, 0x40)) 
        return false;
    if(!IMU_WriteRegister(IMU_REG_FIFO_EN, 0x08)) 
        return false;
    if(!IMU_WriteRegister(IMU_REG_INT_ENABLE, 0x01)) 
        return false;

    return true;
}

void setup()
{
    Serial.begin(921600); // To avoid bottleneck when collecting data;
    while (!Serial)
        ;

    if (!IMU_Init())
    {
        Serial.println("ERROR: IMU Initialization Failed");
        while (1)
            ;
    }
    pinMode(IMU_INT_PIN, INPUT); // Cấu hình chân ngắt của MPU6050

    attachInterrupt(digitalPinToInterrupt(IMU_INT_PIN), onMPUInterrupt, RISING);
}

void loop()
{
    if (samplingTriggered)
    {
        samplingTriggered = false;

        uint16_t fifo_count = IMU_GetFIFOCount();

        while (fifo_count >= 6)
        {
            AccelData raw_counts;

            if (!IMU_ReadFIFO(raw_counts))
            {
                return;
            }

            

            float curr_raw_x = (float)raw_counts.x * ACCEL_SCALE;
            float curr_raw_y = (float)raw_counts.y * ACCEL_SCALE;
            float curr_raw_z = (float)raw_counts.z * ACCEL_SCALE;

            float timestamp_ms = (float)micros() / 1000.0f;

            Serial.print(timestamp_ms, 3);
            Serial.print(",");
            Serial.print(curr_raw_x, 4);
            Serial.print(",");
            Serial.print(curr_raw_y, 4);
            Serial.print(",");
            Serial.print(curr_raw_z, 4);
            Serial.print(",");
            Serial.println(CURRENT_LABEL);

            fifo_count -= 6;
        }
    }
}