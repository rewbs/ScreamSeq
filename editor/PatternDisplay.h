#ifndef SCREAMSEQ_PATTERN_DISPLAY_H
#define SCREAMSEQ_PATTERN_DISPLAY_H

#include <float.h>
#include <stdint.h>

// Pure presentation rules, usable from native C++, Objective-C and Swift's C
// importer. No song mutation, elapsed-time estimate or audio work belongs here.
typedef struct ScreamSeqPatternGridMetrics {
  uint32_t rowsPerBeat;
  uint32_t rowsPerMeasure;
} ScreamSeqPatternGridMetrics;

static inline ScreamSeqPatternGridMetrics ScreamSeqPatternGridMetricsMake(uint32_t beat, uint32_t measure) {
  ScreamSeqPatternGridMetrics result;
  result.rowsPerBeat = beat ? beat : 4;
  result.rowsPerMeasure = measure ? measure : 16;
  if(result.rowsPerMeasure < result.rowsPerBeat) result.rowsPerMeasure = result.rowsPerBeat;
  return result;
}

// 0 = ordinary row, 1 = beat, 2 = measure. Measures need not be a whole number
// of beats in imported tracker songs; a measure boundary still takes precedence.
static inline unsigned ScreamSeqPatternRowAccent(ScreamSeqPatternGridMetrics metrics, uint32_t row) {
  if(metrics.rowsPerMeasure && row % metrics.rowsPerMeasure == 0) return 2;
  if(metrics.rowsPerBeat && row % metrics.rowsPerBeat == 0) return 1;
  return 0;
}

// Graph lane hex display truncates a bounded normalized value to 0..255. This
// display convention never quantizes the stored double or changes playback.
static inline unsigned ScreamSeqGraphCommandDisplayByte(double amount) {
  if(amount != amount || amount > DBL_MAX || amount < -DBL_MAX) amount = 0;
  if(amount < 0) amount = 0;
  if(amount > 1) amount = 1;
  return (unsigned)(amount * 255.0);
}

#endif
