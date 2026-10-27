// oRings - Schwung plugin API v2 adapter.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// EVERY entry point here runs on the SPI audio callback (plugin_api_v1.h):
// no allocation (instances come from a static pool), no file I/O, no
// logging, bounded work.

// plugin_api_v1.h is a C header and asserts its own layout with C11
// _Static_assert, which C++ spells static_assert. Same check, same header,
// unmodified.
#define _Static_assert static_assert
extern "C" {
#include "host/plugin_api_v1.h"
}
#undef _Static_assert

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

#include "orings_meta.h"
#include "wrapper/orings.h"

using orings::ORings;

namespace {

// Four chain slots can each hold one; two spare cover a swap in flight.
const int kInstances = 6;

struct Instance {
  std::atomic<int> used;
  ORings engine;
  float left[MOVE_FRAMES_PER_BLOCK];
  float right[MOVE_FRAMES_PER_BLOCK];
};

Instance g_pool[kInstances];

static_assert(std::atomic<int>::is_always_lock_free, "pool needs lock-free atomics");

int CopyText(char *buf, int size, const char *text) {
  const size_t n = strlen(text);
  if (n >= static_cast<size_t>(size)) {
    buf[0] = 0;
    return -1;
  }
  memcpy(buf, text, n + 1);
  return static_cast<int>(n);
}

// Output protection after Rings' own limiter: linear up to 0.9 (-0.9 dBFS),
// then a tanh-shaped knee that meets full scale asymptotically, so GAIN
// above 0 dB rounds off instead of hard-clipping. NaN becomes silence.
float SoftClip(float x) {
  if (!(x == x)) return 0.0f;
  const float kKnee = 0.9f;
  const float a = fabsf(x);
  if (a <= kKnee) return x;
  float z = (a - kKnee) * (1.0f / (1.0f - kKnee));
  z = z >= 3.0f ? 1.0f : z * (27.0f + z * z) / (27.0f + 9.0f * z * z);
  const float y = kKnee + (1.0f - kKnee) * z;
  return x < 0.0f ? -y : y;
}

void *create_instance(const char *module_dir, const char *json_defaults) {
  (void)module_dir;
  (void)json_defaults;
  for (int i = 0; i < kInstances; ++i) {
    int expected = 0;
    if (g_pool[i].used.compare_exchange_strong(expected, 1)) {
      // Bounded: Init() is what rings.cc's Init() does (the two parts and the
      // strummer), plus clearing the instance's own 128 KB of reverb memory.
      g_pool[i].engine.Init();
      return &g_pool[i];
    }
  }
  return NULL;
}

void destroy_instance(void *ptr) {
  if (!ptr) return;
  static_cast<Instance *>(ptr)->used.store(0);
}

void on_midi(void *ptr, const uint8_t *msg, int len, int source) {
  (void)source;
  if (!ptr || !msg) return;
  static_cast<Instance *>(ptr)->engine.Midi(msg, len);
}

void set_param(void *ptr, const char *key, const char *val) {
  if (!ptr || !key || !val) return;
  ORings &e = static_cast<Instance *>(ptr)->engine;
  if (!strcmp(key, "state")) {
    (void)e.ReadState(val);
    return;
  }
  (void)e.SetByKey(key, val);
}

int get_param(void *ptr, const char *key, char *buf, int size) {
  if (!ptr || !key || !buf || size <= 0) return -1;
  buf[0] = 0;
  const ORings &e = static_cast<Instance *>(ptr)->engine;
  if (!strcmp(key, "state")) return e.WriteState(buf, size);
  if (!strcmp(key, "chain_params")) return CopyText(buf, size, orings::kChainParams);
  if (!strcmp(key, "ui_hierarchy")) return CopyText(buf, size, orings::kUiHierarchy);
  return e.GetByKey(key, buf, size);
}

int get_error(void *ptr, char *buf, int size) {
  (void)ptr;
  if (buf && size > 0) buf[0] = 0;
  return 0;
}

void render_block(void *ptr, int16_t *out, int frames) {
  if (!out || frames <= 0) return;
  if (!ptr || frames > MOVE_FRAMES_PER_BLOCK) {
    memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
    return;
  }
  Instance *inst = static_cast<Instance *>(ptr);
  inst->engine.Process(inst->left, inst->right, static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    out[2 * i] = static_cast<int16_t>(lrintf(SoftClip(inst->left[i]) * 32767.0f));
    out[2 * i + 1] = static_cast<int16_t>(lrintf(SoftClip(inst->right[i]) * 32767.0f));
  }
}

plugin_api_v2_t g_api = {};

}  // namespace

extern "C" __attribute__((visibility("default")))
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
  if (host && host->sample_rate && host->sample_rate != MOVE_SAMPLE_RATE) return NULL;
  g_api.api_version = 2;
  g_api.create_instance = create_instance;
  g_api.destroy_instance = destroy_instance;
  g_api.on_midi = on_midi;
  g_api.set_param = set_param;
  g_api.get_param = get_param;
  g_api.get_error = get_error;
  g_api.render_block = render_block;
  return &g_api;
}
