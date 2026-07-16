#pragma once
#include <cstddef>
#include <cstdint>

namespace pedal {

constexpr float  SAMPLE_RATE     = 48000.0f;
constexpr size_t BLOCK_SIZE      = 48;
constexpr float  INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;
constexpr float  REVERB_SAMPLE_RATE     = SAMPLE_RATE * 0.5f;
constexpr float  INV_REVERB_SAMPLE_RATE = 1.0f / REVERB_SAMPLE_RATE;

// Delay: supports up to 3s; time range is 60ms–2500ms (2ms min for Lofi)
constexpr size_t MAX_DELAY_SAMPLES = static_cast<size_t>(SAMPLE_RATE * 3.0f);

// Modulation: short delay lines (max 25ms for chorus/flanger)
constexpr size_t MAX_MOD_DELAY_SAMPLES = static_cast<size_t>(SAMPLE_RATE * 0.025f);

// Number of modes per effect slot
constexpr int NUM_DELAY_MODES  = 10;
constexpr int NUM_MOD_MODES    = 7;
constexpr int NUM_REVERB_MODES = 12;

// Parameters per effect slot
constexpr int NUM_PARAMS = 7;
constexpr int NUM_POTS   = NUM_PARAMS;

// Encoder behavior
constexpr float    PARAM_STEP_SLOW        = 0.01f;
constexpr float    PARAM_STEP_FAST        = 0.05f;
constexpr uint32_t ENCODER_FAST_WINDOW_MS = 40;
constexpr float    POT_SMOOTH             = 0.033f;

// Presets
constexpr int      PRESET_SLOT_COUNT           = 100;
constexpr int      PRESET_BANK_COUNT           = 10;
constexpr int      PRESET_SLOTS_PER_BANK       = 10;
constexpr uint32_t PRESET_HOLD_MS              = 700;
constexpr uint32_t PRESET_STATUS_MS            = 1000;
constexpr uint32_t kPresetEntryHoldMs          = 1000u; // hold duration to enter browse
constexpr uint32_t kPresetInactivityMs         = 3000u; // idle ms before auto-exit browse
constexpr uint32_t kPresetLedBlinkPeriodMs     = 500u;
constexpr uint32_t kBrowseSavedDisplayMs       = 1000u;

// MIDI CC ranges — three consecutive blocks of 7
constexpr uint8_t CC_MOD_BASE    = 14;  // CC 14–20: modulation params
constexpr uint8_t CC_DELAY_BASE  = 21;  // CC 21–27: delay params
constexpr uint8_t CC_REVERB_BASE = 28;  // CC 28–34: reverb params
constexpr uint8_t CC_HOLD        = 65;  // reverb hold toggle

// Display
constexpr uint32_t DISPLAY_UPDATE_MS  = 33;   // ~30fps

// Tap tempo
constexpr int      TAP_MAX_TAPS   = 4;
constexpr float    TAP_MIN_BPM    = 40.0f;
constexpr float    TAP_MAX_BPM    = 240.0f;
constexpr uint32_t TAP_TIMEOUT_MS = 2000;

} // namespace pedal
