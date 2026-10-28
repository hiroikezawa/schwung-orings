// oRings FX - Schwung audio FX API v2 adapter.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// EVERY entry point here runs on the SPI audio callback (or, for a bus
// insert, the bus worker): no allocation (instances come from a static pool),
// no file I/O, no logging, bounded work.

// The vendored Schwung headers are C and check their own layout with C11
// _Static_assert, which C++ spells static_assert. Same check, same header.
#define _Static_assert static_assert
extern "C" {
#include "host/audio_fx_api_v2.h"
}
#undef _Static_assert

#include <string.h>

#include <atomic>

#include "fx/orings_fx.h"

using oringsfx::RingsFx;

namespace {

// Four track slots, Master FX, the send buses: eight covers a full house
// plus a swap in flight.
const int kInstances = 8;

struct Instance {
  std::atomic<int> used;
  RingsFx engine;
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

void *create_instance(const char *module_dir, const char *config_json) {
  (void)module_dir;
  (void)config_json;
  for (int i = 0; i < kInstances; ++i) {
    int expected = 0;
    if (g_pool[i].used.compare_exchange_strong(expected, 1)) {
      // Bounded: rings.cc's Init() (two parts, the strummer) plus clearing
      // the instance's own 128 KB of reverb memory.
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

void process_block(void *ptr, int16_t *audio, int frames) {
  if (!ptr || !audio || frames <= 0) return;
  static_cast<Instance *>(ptr)->engine.Process(audio, frames);
}

void set_param(void *ptr, const char *key, const char *val) {
  if (!ptr || !key || !val) return;
  RingsFx &e = static_cast<Instance *>(ptr)->engine;
  if (!strcmp(key, "state")) {
    if (e.ReadState(val)) e.Snap();
    return;
  }
  (void)e.SetByKey(key, val);
}

int get_param(void *ptr, const char *key, char *buf, int size) {
  if (!ptr || !key || !buf || size <= 0) return -1;
  buf[0] = 0;
  const RingsFx &e = static_cast<Instance *>(ptr)->engine;
  if (!strcmp(key, "state")) return e.WriteState(buf, size);
  if (!strcmp(key, "name")) return CopyText(buf, size, "oRings FX");
  if (!strcmp(key, "chain_params")) return CopyText(buf, size, oringsfx::kChainParams);
  if (!strcmp(key, "ui_hierarchy")) return CopyText(buf, size, oringsfx::kUiHierarchy);
  return e.GetByKey(key, buf, size);
}

void on_midi(void *ptr, const uint8_t *msg, int len, int source) {
  (void)source;
  if (!ptr || !msg) return;
  static_cast<Instance *>(ptr)->engine.Midi(msg, len);
}

audio_fx_api_v2_t g_api = {};

}  // namespace

extern "C" __attribute__((visibility("default")))
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
  // Rings' tables and both resamplers are built for Move's 44.1 kHz.
  if (host && host->sample_rate && host->sample_rate != MOVE_SAMPLE_RATE) return NULL;
  g_api.api_version = AUDIO_FX_API_VERSION_2;
  g_api.create_instance = create_instance;
  g_api.destroy_instance = destroy_instance;
  g_api.process_block = process_block;
  g_api.set_param = set_param;
  g_api.get_param = get_param;
  g_api.on_midi = on_midi;
  return &g_api;
}
