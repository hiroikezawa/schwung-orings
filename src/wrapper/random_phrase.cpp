// oRings - the RANDOM pad. MIT. See random_phrase.h.
#include "wrapper/random_phrase.h"

#include <string.h>

extern "C" {
#include "phrase/pg_generator.h"
#include "phrase/pg_library.h"
#include "phrase/pg_rng.h"
#include "phrase/pg_scale.h"
}
#include "pg_patterns_data.h"   // generated: PG_EMBEDDED_PATTERNS[]

namespace orings {

namespace {
const float kMutation = 0.5f;   // Phrase Gen's default
// One library for every instance: it is only read after this.
pg_library_t g_lib;
bool g_lib_ready = false;
}  // namespace

void RandomPhrase::LibraryInit() {
  if (g_lib_ready) return;
  pg_library_init(&g_lib, 0 /* C */, PG_MASK_PHRYGIAN, 8);
  for (int i = 0; i < PG_EMBEDDED_COUNT; ++i) {
    const pg_embedded_pattern_t &e = PG_EMBEDDED_PATTERNS[i];
    pg_library_add(&g_lib, e.name, e.data, e.len, e.start_step);
  }
  pg_library_finalize(&g_lib);
  g_lib_ready = true;
}

void RandomPhrase::Init(uint32_t seed) {
  rng_ = seed ? seed : 0x5A17C0DEu;
  last_source_ = -1;
  memset(prev_, 0, sizeof(prev_));
  have_prev_ = false;
}

static int MakeHalf(int last_source, pg_rng_t *rng, const pg_pattern_t *avoid,
                    int source, pg_pattern_t *out) {
  pg_gen_params_t gp;
  gp.mutation = kMutation;
  gp.source = source;
  gp.avoid_source = last_source;
  gp.allow_rests = 0;
  return pg_generate(&g_lib.model, &gp, rng, avoid, out, NULL);
}

bool RandomPhrase::Generate(int len, uint32_t entropy, int *notes) {
  if (!g_lib_ready || g_lib.n <= 0) return false;
  if (len != 16) len = 8;
  // When the press happened keeps two sessions from producing the same run
  // of phrases -- Phrase Gen mixes its entropy the same way.
  pg_rng_t rng;
  pg_rng_seed(&rng, rng_ ^ (entropy * 0x9E3779B1u));
  pg_pattern_t prev, a, b;
  pg_pattern_clear(&prev, 8);
  if (have_prev_) {
    for (int i = 0; i < 8; ++i) {
      prev.steps[i].note = static_cast<int8_t>(prev_[i]);
      prev.steps[i].vel = 100;
      prev.steps[i].gate = 100;
    }
  }
  const int src = MakeHalf(last_source_, &rng, have_prev_ ? &prev : NULL, -1, &a);
  if (src < 0) return false;
  if (len == 16 && MakeHalf(last_source_, &rng, &a, src, &b) < 0) return false;
  last_source_ = src;
  for (int i = 0; i < 8; ++i) {
    prev_[i] = a.steps[i].note;
    notes[i] = a.steps[i].note;
    if (len == 16) notes[8 + i] = b.steps[i].note;
  }
  have_prev_ = true;
  rng_ = pg_rng_next(&rng);
  return true;
}

}  // namespace orings
