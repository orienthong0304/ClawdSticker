#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-1.8 — portrait AMOLED kit.
// 368x448 SH8601 + FT3168 touch + AXP2101 PMU + QMI8658 IMU + XCA9554 expander.
// IMU is present (initialized for I2C bus health) but rotation is disabled
// because the panel mounts in a fixed orientation in the kit's enclosure.

#define BOARD_NAME           "Waveshare AMOLED 1.8"

// ---- Display geometry (portrait) ----
#define LCD_WIDTH            368
#define LCD_HEIGHT           448

// ---- QSPI display pins (SH8601) ----
#define LCD_CS               12
#define LCD_SCLK             11        // different from 2.16 board (was GPIO 38)
#define LCD_SDIO0            4
#define LCD_SDIO1            5
#define LCD_SDIO2            6
#define LCD_SDIO3            7
// LCD reset is routed through the XCA9554 IO expander (EXIO1). The Arduino
// GFX driver gets GFX_NOT_DEFINED; the expander releases reset before
// gfx->begin() runs.

// ---- I2C bus (touch + PMU + IMU + IO expander all share one bus) ----
#define IIC_SDA              15
#define IIC_SCL              14

// ---- Touch (FT3168 via vendored minimal I2C reader) ----
#define TP_INT               21
#define FT3168_ADDR          0x38

// ---- PMU ----
#define AXP2101_ADDR         0x34

// ---- IO expander (XCA9554/PCA9554 compatible) ----
// Gates LCD_RST, TP_RST, audio amp enable, and reads the PWR button.
#define XCA9554_ADDR         0x20
#define IOX_PIN_TP_RST       0     // EXIO0 → touch reset (active LOW)
#define IOX_PIN_LCD_RST      1     // EXIO1 → display reset (active LOW)
#define IOX_PIN_PA_EN        2     // EXIO2 → audio amp enable
#define IOX_PIN_PWR_BTN      4     // EXIO4 → PWR button input, active HIGH

// ---- Audio (ES8311 codec + onboard speaker) ----
// Pins per Waveshare's official board pin_config.h / 15_ES8311 example.
#define ES8311_ADDR          0x18
#define I2S_MCLK_PIN         16
#define I2S_BCLK_PIN         9
#define I2S_WS_PIN           45
#define I2S_DOUT_PIN         8     // ESP32 → codec (speaker playback)
#define I2S_DIN_PIN          10    // codec → ESP32 (mic; unused in MVP)
// Speaker power-amp enable. Waveshare's official example drives GPIO 46
// directly (digitalWrite(46, HIGH)); the IO-expander map above also labels
// EXIO2 as the amp enable. The two disagree (audio was never implemented in
// this port), so audio.cpp asserts BOTH on start to be safe.
#define AUDIO_PA_EN_GPIO     46

// ---- Buttons ----
#define BTN_BACK_GPIO        0     // BOOT — primary, Space (PTT)
// PWR comes via XCA9554 EXIO4 (see power.cpp); there is no secondary button.

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              1    // present + initialized, but rotation off
#define BOARD_HAS_BATTERY          1
#define BOARD_HAS_IO_EXPANDER      1
