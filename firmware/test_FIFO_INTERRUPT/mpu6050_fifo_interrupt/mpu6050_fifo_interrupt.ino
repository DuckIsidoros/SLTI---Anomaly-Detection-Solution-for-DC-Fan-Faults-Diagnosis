#include <Wire.h>

#define I2C_SDA 21
#define I2C_SCL 22

#define IMU_ADDRESS 0x68

#define IMU_REG_PWR_MGMT_1   0x6B
#define IMU_REG_ACCEL_CONFIG 0x1C

#define IMU_INT_PIN          27
#define IMU_REG_FIFO_EN      0x23
#define IMU_REG_INT_ENABLE   0x38
#define IMU_REG_USER_CTRL    0x6A
#define IMU_REG_FIFO_COUNT_H 0x72
#define IMU_REG_FIFO_R_W     0x74

const float ACCEL_SCALE = 1.0f / 2048.0f; // +/-16g

const char CURRENT_LABEL[] = "imbalanced";

struct AccelData
{
    int16_t x;
    int16_t y;
    int16_t z;
};

volatile bool samplingTriggered = false;


/*
Why:
- ISR should remain minimal.
- Actual FIFO/I2C work is handled in loop().
*/
void IRAM_ATTR onMPUInterrupt()
{
    samplingTriggered = true;
}


/*
Input:
- MPU6050 register address
- Register value

Output:
- true if write succeeds
- false if write fails
*/
bool IMU_WriteRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(reg);
    Wire.write(value);

    return Wire.endTransmission() == 0;
}


/*
Output:
- Number of bytes currently stored in FIFO
*/
uint16_t IMU_GetFIFOCount()
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_FIFO_COUNT_H);

    if (Wire.endTransmission(false) != 0)
        return 0;

    if (Wire.requestFrom(IMU_ADDRESS, 2) < 2)
        return 0;

    return (Wire.read() << 8) | Wire.read();
}


/*
Output:
- One accelerometer XYZ sample

Why:
- Accelerometer FIFO frame contains:
  X = 2 bytes
  Y = 2 bytes
  Z = 2 bytes
  Total = 6 bytes
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


/*
Output:
- MPU6050 awake
- Accelerometer range = +/-16g
- FIFO enabled for accelerometer
- Data Ready interrupt enabled
*/
bool IMU_Init()
{
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    delay(50);

    // Wake MPU6050
    if (!IMU_WriteRegister(IMU_REG_PWR_MGMT_1, 0x00))
        return false;

    // Accelerometer range = +/-16g
    if (!IMU_WriteRegister(IMU_REG_ACCEL_CONFIG, 0x18))
        return false;

    // Reset FIFO
    if (!IMU_WriteRegister(IMU_REG_USER_CTRL, 0x04))
        return false;

    // Enable FIFO
    if (!IMU_WriteRegister(IMU_REG_USER_CTRL, 0x40))
        return false;

    // Route accelerometer XYZ into FIFO
    if (!IMU_WriteRegister(IMU_REG_FIFO_EN, 0x08))
        return false;

    // Enable Data Ready interrupt
    if (!IMU_WriteRegister(IMU_REG_INT_ENABLE, 0x01))
        return false;

    return true;
}


void setup()
{
    Serial.begin(921600);

    if (!IMU_Init())
    {
        Serial.println("ERROR: IMU Initialization Failed");

        while (1)
        {
            delay(1000);
        }
    }

    pinMode(IMU_INT_PIN, INPUT);

    attachInterrupt(
        digitalPinToInterrupt(IMU_INT_PIN),
        onMPUInterrupt,
        RISING
    );
}


void loop()
{
    if (!samplingTriggered)
        return;

    samplingTriggered = false;

    uint16_t fifo_count = IMU_GetFIFOCount();

    while (fifo_count >= 6)
    {
        AccelData raw_counts;

        if (!IMU_ReadFIFO(raw_counts))
            return;

        float acc_x = raw_counts.x * ACCEL_SCALE;
        float acc_y = raw_counts.y * ACCEL_SCALE;
        float acc_z = raw_counts.z * ACCEL_SCALE;

        float timestamp_ms = micros() / 1000.0f;

        // Output format:
        // timestamp_ms,accX_raw,accY_raw,accZ_raw,label
        Serial.print(timestamp_ms, 3);
        Serial.print(",");

        Serial.print(acc_x, 4);
        Serial.print(",");

        Serial.print(acc_y, 4);
        Serial.print(",");

        Serial.print(acc_z, 4);
        Serial.print(",");

        Serial.println(CURRENT_LABEL);

        fifo_count -= 6;
    }
/*
TODO:
Validate FIFO overflow behavior during continuous acquisition.
If overflow is frequent, optimize FIFO draining in a later iteration.
*/
    
}
