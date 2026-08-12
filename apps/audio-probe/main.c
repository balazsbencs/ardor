#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Mode {
  MODE_DEVICES,
  MODE_TONE,
  MODE_CAPTURE,
  MODE_DUPLEX,
  MODE_LATENCY
} Mode;

#define MAX_LATENCY_PULSES 64

typedef struct Args {
  Mode mode;
  int playback_device;
  int capture_device;
  unsigned int sample_rate;
  unsigned int buffer_frames;
  unsigned int seconds;
  unsigned int input_channel;
  float hz;
} Args;

typedef struct ProbeState {
  double phase;
  double sum_squares;
  float peak;
  unsigned long long input_samples;
  unsigned int capture_channels;
  unsigned int input_channel;
  unsigned int sample_rate;
  float hz;
  unsigned long long output_frames;
  unsigned long long capture_frames;
  unsigned int pulse_period_frames;
  unsigned int pulse_count;
  unsigned long long pulse_output_frame[MAX_LATENCY_PULSES];
  int pulse_sign[MAX_LATENCY_PULSES];
  unsigned int detected_count;
  unsigned long long detected_capture_frame[MAX_LATENCY_PULSES];
  int detected_sign[MAX_LATENCY_PULSES];
  unsigned int detection_refractory;
  float previous_capture;
  Mode mode;
} ProbeState;

static void usage(void)
{
  fprintf(stderr,
          "Usage:\n"
          "  audio-probe --devices\n"
          "  audio-probe --tone [--playback-device N] [--seconds N] [--hz HZ]\n"
          "  audio-probe --capture [--capture-device N] [--seconds N] [--input-channel N]\n"
          "  audio-probe --duplex [--playback-device N] [--capture-device N] [--seconds N]\n"
          "  audio-probe --latency [--playback-device N] [--capture-device N] [--seconds N]\n");
}

static int parse_int(const char* text, int* out)
{
  char* end = NULL;
  long value = strtol(text, &end, 10);
  if (!text[0] || *end) return 0;
  *out = (int)value;
  return 1;
}

static int parse_uint(const char* text, unsigned int* out)
{
  int value = 0;
  if (!parse_int(text, &value) || value < 0) return 0;
  *out = (unsigned int)value;
  return 1;
}

static int parse_float(const char* text, float* out)
{
  char* end = NULL;
  float value = strtof(text, &end);
  if (!text[0] || *end) return 0;
  *out = value;
  return 1;
}

static int parse_args(int argc, char** argv, Args* args)
{
  args->mode = MODE_DEVICES;
  args->playback_device = -1;
  args->capture_device = -1;
  args->sample_rate = 48000;
  args->buffer_frames = 64;
  args->seconds = 5;
  args->input_channel = 0;
  args->hz = 440.0f;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const char* v = (i + 1 < argc) ? argv[i + 1] : NULL;
    if (strcmp(a, "--devices") == 0) {
      args->mode = MODE_DEVICES;
    } else if (strcmp(a, "--tone") == 0) {
      args->mode = MODE_TONE;
    } else if (strcmp(a, "--capture") == 0) {
      args->mode = MODE_CAPTURE;
    } else if (strcmp(a, "--duplex") == 0) {
      args->mode = MODE_DUPLEX;
    } else if (strcmp(a, "--latency") == 0) {
      args->mode = MODE_LATENCY;
    } else if (strcmp(a, "--playback-device") == 0 && v) {
      if (!parse_int(v, &args->playback_device)) return 0;
      ++i;
    } else if (strcmp(a, "--capture-device") == 0 && v) {
      if (!parse_int(v, &args->capture_device)) return 0;
      ++i;
    } else if (strcmp(a, "--sample-rate") == 0 && v) {
      if (!parse_uint(v, &args->sample_rate)) return 0;
      ++i;
    } else if (strcmp(a, "--buffer-frames") == 0 && v) {
      if (!parse_uint(v, &args->buffer_frames)) return 0;
      ++i;
    } else if (strcmp(a, "--seconds") == 0 && v) {
      if (!parse_uint(v, &args->seconds)) return 0;
      ++i;
    } else if (strcmp(a, "--input-channel") == 0 && v) {
      if (!parse_uint(v, &args->input_channel)) return 0;
      ++i;
    } else if (strcmp(a, "--hz") == 0 && v) {
      if (!parse_float(v, &args->hz)) return 0;
      ++i;
    } else {
      return 0;
    }
  }
  return 1;
}

static int list_devices(ma_context* context)
{
  ma_device_info* playback = NULL;
  ma_uint32 playback_count = 0;
  ma_device_info* capture = NULL;
  ma_uint32 capture_count = 0;
  if (ma_context_get_devices(context, &playback, &playback_count, &capture, &capture_count) != MA_SUCCESS) {
    fprintf(stderr, "failed to enumerate audio devices\n");
    return 1;
  }

  printf("Playback devices:\n");
  for (ma_uint32 i = 0; i < playback_count; ++i) {
    printf("  [%u] %s\n", i, playback[i].name);
  }
  printf("Capture devices:\n");
  for (ma_uint32 i = 0; i < capture_count; ++i) {
    printf("  [%u] %s\n", i, capture[i].name);
  }
  return 0;
}

static void audio_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
  ProbeState* state = (ProbeState*)device->pUserData;
  float* out = (float*)output;
  const float* in = (const float*)input;

  if (out) {
    const double step = 2.0 * 3.14159265358979323846 * (double)state->hz / (double)state->sample_rate;
    for (ma_uint32 i = 0; i < frame_count; ++i) {
      float sample = 0.0f;
      if (state->mode == MODE_LATENCY) {
        const unsigned long long frame = state->output_frames + i;
        if (frame >= state->sample_rate
            && ((frame - state->sample_rate) % state->pulse_period_frames) == 0
            && state->pulse_count < MAX_LATENCY_PULSES) {
          const int sign = (state->pulse_count & 1U) ? -1 : 1;
          sample = 0.5f * (float)sign;
          state->pulse_output_frame[state->pulse_count] = frame;
          state->pulse_sign[state->pulse_count] = sign;
          ++state->pulse_count;
        }
      } else {
        sample = (float)(0.15 * sin(state->phase));
      }
      out[i * 2] = sample;
      out[i * 2 + 1] = sample;
      state->phase += step;
      if (state->phase > 2.0 * 3.14159265358979323846) state->phase -= 2.0 * 3.14159265358979323846;
    }
    state->output_frames += frame_count;
  }

  if (in) {
    for (ma_uint32 i = 0; i < frame_count; ++i) {
      float sample = in[i * state->capture_channels + state->input_channel];
      float abs_sample = fabsf(sample);
      state->sum_squares += (double)sample * (double)sample;
      if (abs_sample > state->peak) state->peak = abs_sample;
      if (state->mode == MODE_LATENCY) {
        if (state->detection_refractory > 0) {
          --state->detection_refractory;
        } else if (abs_sample >= 0.10f && fabsf(state->previous_capture) < 0.10f
                   && state->detected_count < MAX_LATENCY_PULSES) {
          state->detected_capture_frame[state->detected_count] = state->capture_frames + i;
          state->detected_sign[state->detected_count] = sample < 0.0f ? -1 : 1;
          ++state->detected_count;
          state->detection_refractory = state->pulse_period_frames / 4;
        }
        state->previous_capture = sample;
      }
      ++state->input_samples;
    }
    state->capture_frames += frame_count;
  }
}

static int compare_u64(const void* left, const void* right)
{
  const unsigned long long a = *(const unsigned long long*)left;
  const unsigned long long b = *(const unsigned long long*)right;
  return (a > b) - (a < b);
}

static int report_latency(const ProbeState* state)
{
  unsigned long long delays[MAX_LATENCY_PULSES];
  unsigned int delay_count = 0;
  unsigned int detected = 0;
  for (unsigned int pulse = 0; pulse < state->pulse_count && detected < state->detected_count; ++pulse) {
    while (detected < state->detected_count
           && (state->detected_capture_frame[detected] < state->pulse_output_frame[pulse]
               || state->detected_sign[detected] != state->pulse_sign[pulse])) {
      ++detected;
    }
    if (detected == state->detected_count) break;
    delays[delay_count++] = state->detected_capture_frame[detected]
                            - state->pulse_output_frame[pulse];
    ++detected;
  }
  if (delay_count == 0) {
    fprintf(stderr,
            "latency: no loopback impulses detected (connect output to input or enable codec loopback)\n");
    return 1;
  }
  qsort(delays, delay_count, sizeof(delays[0]), compare_u64);
  const unsigned long long minimum = delays[0];
  const unsigned long long median = delays[delay_count / 2];
  const unsigned long long maximum = delays[delay_count - 1];
  printf("latency pulses=%u/%u frames min=%llu median=%llu max=%llu ms min=%.4f median=%.4f max=%.4f\n",
         delay_count, state->pulse_count, minimum, median, maximum,
         (double)minimum * 1000.0 / state->sample_rate,
         (double)median * 1000.0 / state->sample_rate,
         (double)maximum * 1000.0 / state->sample_rate);
  return delay_count < 3 ? 1 : 0;
}

static int run_probe(ma_context* context, const Args* args)
{
  ma_device_info* playback = NULL;
  ma_uint32 playback_count = 0;
  ma_device_info* capture = NULL;
  ma_uint32 capture_count = 0;
  if (ma_context_get_devices(context, &playback, &playback_count, &capture, &capture_count) != MA_SUCCESS) {
    fprintf(stderr, "failed to enumerate audio devices\n");
    return 1;
  }
  if (args->playback_device >= (int)playback_count || args->capture_device >= (int)capture_count) {
    fprintf(stderr, "invalid device index; run --devices\n");
    return 1;
  }

  ProbeState state;
  memset(&state, 0, sizeof(state));
  state.capture_channels = args->input_channel + 1;
  state.input_channel = args->input_channel;
  state.sample_rate = args->sample_rate;
  state.hz = args->hz;
  state.pulse_period_frames = args->sample_rate / 2;
  state.mode = args->mode;

  ma_device_type type = ma_device_type_playback;
  if (args->mode == MODE_CAPTURE) type = ma_device_type_capture;
  if (args->mode == MODE_DUPLEX || args->mode == MODE_LATENCY) type = ma_device_type_duplex;

  ma_device_config cfg = ma_device_config_init(type);
  cfg.sampleRate = args->sample_rate;
  cfg.periodSizeInFrames = args->buffer_frames;
  cfg.dataCallback = audio_callback;
  cfg.pUserData = &state;

  if (args->mode == MODE_TONE || args->mode == MODE_DUPLEX || args->mode == MODE_LATENCY) {
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    if (args->playback_device >= 0) cfg.playback.pDeviceID = &playback[args->playback_device].id;
  }
  if (args->mode == MODE_CAPTURE || args->mode == MODE_DUPLEX || args->mode == MODE_LATENCY) {
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = state.capture_channels;
    if (args->capture_device >= 0) cfg.capture.pDeviceID = &capture[args->capture_device].id;
  }

  ma_device device;
  if (ma_device_init(context, &cfg, &device) != MA_SUCCESS) {
    fprintf(stderr, "failed to open audio device\n");
    return 1;
  }
  if (ma_device_start(&device) != MA_SUCCESS) {
    ma_device_uninit(&device);
    fprintf(stderr, "failed to start audio device\n");
    return 1;
  }

  printf("running %u second probe at %u Hz, requested buffer %u, native capture %u Hz/%u frames, playback %u Hz/%u frames\n",
         args->seconds, args->sample_rate, args->buffer_frames,
         device.capture.internalSampleRate, device.capture.internalPeriodSizeInFrames,
         device.playback.internalSampleRate, device.playback.internalPeriodSizeInFrames);
  ma_sleep(args->seconds * 1000);
  ma_device_uninit(&device);

  if (args->mode == MODE_CAPTURE || args->mode == MODE_DUPLEX || args->mode == MODE_LATENCY) {
    double rms = 0.0;
    if (state.input_samples > 0) rms = sqrt(state.sum_squares / (double)state.input_samples);
    printf("capture samples=%llu rms=%.8f peak=%.8f\n", state.input_samples, rms, state.peak);
  }
  if (args->mode == MODE_LATENCY) return report_latency(&state);
  return 0;
}

int main(int argc, char** argv)
{
  Args args;
  if (!parse_args(argc, argv, &args)) {
    usage();
    return 2;
  }

  ma_context context;
  if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
    fprintf(stderr, "failed to initialize audio context\n");
    return 1;
  }

  int rc = (args.mode == MODE_DEVICES) ? list_devices(&context) : run_probe(&context, &args);
  ma_context_uninit(&context);
  return rc;
}
