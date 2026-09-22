#include <Wire.h>

#define I2C_SDA 21
#define I2C_SCL 22

#define IMU_ADDRESS 0x68

#define IMU_REG_PWR_MGMT_1   0x6B
#define IMU_REG_ACCEL_CONFIG 0x1C
#define IMU_REG_ACCEL_XOUT_H 0x3B

const float ACCEL_SCALE = 1.0f / 2048.0f; // +/-16g

struct AccelData
{
    int16_t x;
    int16_t y;
    int16_t z;
};

hw_timer_t *timer = NULL;
volatile bool samplingTriggered = false;


/*
Why:
- Trigger sampling at a fixed 500 Hz.
- ISR stays minimal and only sets a flag.
*/
void IRAM_ATTR onTimer()
{
    samplingTriggered = true;
}


/*
Output:
- One raw XYZ accelerometer sample.

Why:
- Direct register read is used as a clean baseline.
- No FIFO is involved in this test.
*/
bool IMU_ReadAccel(AccelData &data)
{
    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_ACCEL_XOUT_H);

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
*/
bool IMU_Init()
{
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    delay(50);

    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_PWR_MGMT_1);
    Wire.write(0x00);

    if (Wire.endTransmission() != 0)
        return false;

    Wire.beginTransmission(IMU_ADDRESS);
    Wire.write(IMU_REG_ACCEL_CONFIG);
    Wire.write(0x18);

    if (Wire.endTransmission() != 0)
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

    // 2000 us = 2 ms = 500 Hz
    timer = timerBegin(1000000);

    timerAttachInterrupt(timer, &onTimer);

    timerAlarm(
        timer,
        2000,
        true,
        0
    );
}


void loop()
{
    if (!samplingTriggered)
        return;

    samplingTriggered = false;

    AccelData raw_counts;

    if (!IMU_ReadAccel(raw_counts))
        return;

    float acc_x = raw_counts.x * ACCEL_SCALE;
    float acc_y = raw_counts.y * ACCEL_SCALE;
    float acc_z = raw_counts.z * ACCEL_SCALE;

    float timestamp_ms = micros() / 1000.0f;

    Serial.print(timestamp_ms, 3);
    Serial.print(",");

    Serial.print(acc_x, 4);
    Serial.print(",");

    Serial.print(acc_y, 4);
    Serial.print(",");

    Serial.println(acc_z, 4);
}