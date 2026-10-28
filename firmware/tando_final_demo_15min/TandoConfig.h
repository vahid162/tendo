#pragma once

#include <Arduino.h>

// The hardware build is the default; only the simulator opts into virtual I/O.
#ifndef TANDO_WOKWI_MODE
#define TANDO_WOKWI_MODE 0
#endif

// Hardware and timing values preserved from the monolithic sketch.
#define TFT_SCLK    4
#define TFT_MOSI    5
#define LEFT_DC     6
#define LEFT_CS     7
#define LEFT_RST    15
#define RIGHT_DC    18
#define RIGHT_CS    16
#define RIGHT_RST   17
#define MPR_SDA     8
#define MPR_SCL     9
#define RFID_SCK    10
#define RFID_MISO   11
#define RFID_MOSI   12
#define RFID_SS     13
#define RFID_RST    14
#define REACTION_LED_PIN 21
#define FRAME_W 200
#define FRAME_H 200
#define FRAME_X 20
#define FRAME_Y 20

// RFID identities; keep these synchronized with the physical tags.
const byte FOOD1_UID[] = { 0x96, 0x2B, 0xCD, 0xAB };
const byte FOOD1_UID_SIZE = sizeof(FOOD1_UID);
const byte FOOD2_UID[] = { 0xF6, 0x33, 0x11, 0xAA };
const byte FOOD2_UID_SIZE = sizeof(FOOD2_UID);
const byte SLEEP_UID[] = { 0xC6, 0x34, 0xBD, 0xAA };
const byte SLEEP_UID_SIZE = sizeof(SLEEP_UID);

// Shared eye-canvas geometry.
const float SCREEN_CX = 100.0f;
const float SCREEN_CY = 100.0f;
const float EYE_BASE_Y = 96.0f;

// Demo time and SLEEP thresholds use Active Demo Time.
const uint32_t STAGE_MS = 10UL * 60UL * 1000UL;
const uint32_t TOTAL_DEMO_MS = 30UL * 60UL * 1000UL;
const uint32_t SLEEP_CREDIT_WINDOW_START_MS = 6UL * 60UL * 1000UL;
const uint32_t SLEEP_REQUEST_START_MS = 9UL * 60UL * 1000UL;
const uint32_t INACTIVITY_PAUSE_MS = 60UL * 1000UL;
const uint32_t NVS_CHECKPOINT_MS = 15UL * 1000UL;
const uint8_t TOTAL_VISUAL_CREDITS = 9;

// One care credit per family in the current stage.
const uint8_t CARE_PET_BIT   = 0x01;
const uint8_t CARE_FOOD_BIT  = 0x02;
const uint8_t CARE_SLEEP_BIT = 0x04;

// Keep the rc.8 v5 persistence format; modularization does not reset state.
const uint32_t NVS_STATE_VERSION = 5;

// System reaction queue bits.
const uint8_t PENDING_UNLOCK_STAGE2 = 0x01;
const uint8_t PENDING_UNLOCK_STAGE3 = 0x02;

// Visual and reaction-LED timing.
const uint32_t RING_PULSE_MS = 850;
const uint32_t LED_PWM_FREQ = 5000;
const uint8_t LED_PWM_RES_BITS = 8;
const uint8_t LED_PWM_OLD_CHANNEL = 0;
const uint8_t LED_BASE_DUTY = 31;
const uint8_t LED_REACTION_PEAK = 180;
const uint8_t LED_STAGE_PEAK = 230;
const uint8_t LED_COMPLETE_PEAK = 255;
const uint32_t LED_ATTACK_MS = 180;
const uint32_t LED_HOLD_MS = 220;
const uint32_t LED_RELEASE_MS = 450;
const uint32_t LED_GAP_MS = 150;
const uint32_t LED_CYCLE_MS = LED_ATTACK_MS + LED_HOLD_MS + LED_RELEASE_MS + LED_GAP_MS;

// Sleep, blink, and neutral reaction handoff timing.
const uint32_t SLEEP_WAKE_MS = 1700;
const uint32_t BLINK_CLOSE_MS = 90;
const uint32_t BLINK_HOLD_MS = 45;
const uint32_t BLINK_OPEN_MS = 135;
const uint32_t REACTION_SETTLE_MS = 240;

// Generic eye-only autonomous personality timing.
const uint32_t AUTO_SHORT_MIN_MS = 8UL * 1000UL;
const uint32_t AUTO_SHORT_MAX_MS = 20UL * 1000UL;
const uint32_t AUTO_MEDIUM_MIN_MS = 20UL * 1000UL;
const uint32_t AUTO_MEDIUM_MAX_MS = 55UL * 1000UL;
const uint32_t AUTO_LONG_MIN_MS = 55UL * 1000UL;
const uint32_t AUTO_LONG_MAX_MS = 120UL * 1000UL;

// Hunger and PET requests share a stage-local wall-clock schedule.
const uint8_t HUNGER_REQUESTS_PER_STAGE = 10;
const uint8_t PET_REQUESTS_PER_STAGE = 10;
const uint32_t HUNGER_REQUEST_DURATION_MS = 10UL * 1000UL;
const uint32_t PET_REQUEST_DURATION_MS = 10UL * 1000UL;
const uint32_t CARE_REQUEST_SLOT_MS = 60UL * 1000UL;
const uint32_t CARE_REQUEST_EARLY_MIN_MS = 5UL * 1000UL;
const uint32_t CARE_REQUEST_EARLY_MAX_MS = 15UL * 1000UL;
const uint32_t CARE_REQUEST_LATE_MIN_MS = 35UL * 1000UL;
const uint32_t CARE_REQUEST_LATE_MAX_MS = 45UL * 1000UL;
const uint32_t CARE_REQUEST_RETRY_GAP_MIN_MS = 5UL * 1000UL;
const uint32_t CARE_REQUEST_RETRY_GAP_MAX_MS = 10UL * 1000UL;
const uint32_t CARE_REQUEST_COLLISION_DEFER_MIN_MS = 3UL * 1000UL;
const uint32_t CARE_REQUEST_COLLISION_DEFER_MAX_MS = 8UL * 1000UL;
const uint32_t CARE_REQUEST_VISUAL_COOLDOWN_MS = 5UL * 1000UL;

// MPR121 channels, thresholds, and PET timing; preserve measured settings.
const uint8_t PET_ELECTRODE_COUNT = 3;
const uint8_t PET_ELECTRODES[PET_ELECTRODE_COUNT] = { 0, 6, 11 };
const uint16_t PET_E0_MASK  = (1U << 0);
const uint16_t PET_E6_MASK  = (1U << 6);
const uint16_t PET_E11_MASK = (1U << 11);
const uint16_t PET_ALL_MASK = PET_E0_MASK | PET_E6_MASK | PET_E11_MASK;
const uint16_t PET_ELECTRODE_MASKS[PET_ELECTRODE_COUNT] = {
  PET_E0_MASK, PET_E6_MASK, PET_E11_MASK
};
const uint8_t MPR_TOUCH_THRESHOLD = 6;
const uint8_t MPR_RELEASE_THRESHOLD = 3;
const uint32_t PET_TWO_PAD_CONFIRM_MS = 20;
const uint32_t PET_REARM_CLEAR_MS = 220;
const uint32_t PET_EVENT_REFRACTORY_MS = 800;

// RFID removal debounce and main-loop polling intervals.
const uint32_t RFID_REARM_MS = 500;
#if TANDO_WOKWI_MODE
const uint32_t FRAME_MS = 120;
#else
const uint32_t FRAME_MS = 34;
#endif
const uint32_t TOUCH_POLL_MS = 20;
const uint32_t RFID_POLL_MS = 80;
