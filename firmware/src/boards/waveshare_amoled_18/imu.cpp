#include "../../hal/imu_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <SensorQMI8658.hpp>

// AMOLED-1.8 ships with QMI8658 populated, but the kit's enclosure mounts the
// panel in a fixed orientation, so rotation stays disabled (always reports 0).
// We DO read the accelerometer for the desk-buddy `dizzy` mood: a shake is a
// burst of high inter-sample jerk. Reads are centralized here (single I2C
// owner) and polled at ~20 Hz from imu_hal_tick().

#define IMU_POLL_MS        50
#define SHAKE_JERK         1.4f    // sum |Δa| over 3 axes (g) to count as a jolt
#define SHAKE_HITS_NEEDED  3       // consecutive jolts within the window
#define SHAKE_WINDOW_MS    600
#define SHAKE_REARM_MS     2500    // ignore further shakes this long after firing

static SensorQMI8658 imu;
static bool     imu_ok      = false;
static uint32_t last_poll   = 0;
static float    pax = 0, pay = 0, paz = 0;
static bool     have_prev   = false;
static int      shake_hits  = 0;
static uint32_t last_hit    = 0;
static uint32_t last_fire   = 0;
static bool     shake_flag  = false;

void imu_hal_init(void) {
    if (!imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("QMI8658 init failed");
        return;
    }
    imu.configAccelerometer(
        SensorQMI8658::ACC_RANGE_4G,
        SensorQMI8658::ACC_ODR_LOWPOWER_21Hz,
        SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
    imu_ok = true;
    Serial.println("QMI8658 init OK (rotation disabled; shake detection on)");
}

void imu_hal_tick(void) {
    if (!imu_ok) return;
    uint32_t now = millis();
    if (now - last_poll < IMU_POLL_MS) return;
    last_poll = now;

    float ax, ay, az;
    if (!imu.getAccelerometer(ax, ay, az)) return;

    if (have_prev) {
        float jerk = fabsf(ax - pax) + fabsf(ay - pay) + fabsf(az - paz);
        if (jerk > SHAKE_JERK) {
            if (now - last_hit > SHAKE_WINDOW_MS) shake_hits = 0;
            shake_hits++;
            last_hit = now;
            if (shake_hits >= SHAKE_HITS_NEEDED && now - last_fire > SHAKE_REARM_MS) {
                shake_flag = true;
                last_fire = now;
                shake_hits = 0;
            }
        } else if (now - last_hit > SHAKE_WINDOW_MS) {
            shake_hits = 0;
        }
    }
    pax = ax; pay = ay; paz = az; have_prev = true;
}

uint8_t imu_hal_rotation_quadrant(void) { return 0; }

bool imu_hal_shake(void) {
    bool r = shake_flag;
    shake_flag = false;
    return r;
}
