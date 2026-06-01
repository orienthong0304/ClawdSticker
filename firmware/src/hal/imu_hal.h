#pragma once
#include <stdint.h>

// Optional accelerometer-driven orientation tracker. Returns 0..3 (quarter
// turns CW from default mounting). Boards without an IMU — or boards with
// rotation intentionally disabled, like AMOLED-1.8 fixed at 0° — return 0
// from imu_hal_rotation_quadrant() and no-op on init/tick.

void    imu_hal_init(void);
void    imu_hal_tick(void);
uint8_t imu_hal_rotation_quadrant(void);

// Edge-triggered shake detector for the desk-buddy `dizzy` mood: returns true
// ONCE when a deliberate shake has just been detected (then re-arms after a
// debounce). Boards without shake detection return false. Reads happen inside
// imu_hal_tick() so the I2C bus has a single owner.
bool    imu_hal_shake(void);
