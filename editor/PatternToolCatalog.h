#ifndef SCREAMSEQ_PATTERN_TOOL_CATALOG_H
#define SCREAMSEQ_PATTERN_TOOL_CATALOG_H
#include <stdint.h>

// Presentation/request field catalogue; musical validation remains PatternTools.
// C-compatible so native Swift and C++ forms share semantic option visibility.
#define SCREAMSEQ_PATTERN_TOOL_COUNT 14u
#define SCREAMSEQ_TOOL_FIELDS 1u
#define SCREAMSEQ_TOOL_AMOUNT 2u
#define SCREAMSEQ_TOOL_LOSS 4u
#define SCREAMSEQ_TOOL_TARGET 8u
#define SCREAMSEQ_TOOL_FROM 16u
#define SCREAMSEQ_TOOL_TO 32u
#define SCREAMSEQ_TOOL_CURVE 64u
#define SCREAMSEQ_TOOL_SEED 128u
#define SCREAMSEQ_TOOL_REMAP 256u
typedef struct ScreamSeqPatternToolDescriptor {
  const char *identifier;
  const char *title;
  uint32_t options;
} ScreamSeqPatternToolDescriptor;
static inline ScreamSeqPatternToolDescriptor ScreamSeqPatternToolAt(uint32_t index) {
  static const ScreamSeqPatternToolDescriptor tools[SCREAMSEQ_PATTERN_TOOL_COUNT]={
    {"interpolate","Interpolate",SCREAMSEQ_TOOL_TARGET|SCREAMSEQ_TOOL_FROM|SCREAMSEQ_TOOL_TO|SCREAMSEQ_TOOL_CURVE},
    {"humanize","Humanize",SCREAMSEQ_TOOL_TARGET|SCREAMSEQ_TOOL_AMOUNT|SCREAMSEQ_TOOL_SEED},
    {"randomize","Randomize",SCREAMSEQ_TOOL_TARGET|SCREAMSEQ_TOOL_FROM|SCREAMSEQ_TOOL_TO|SCREAMSEQ_TOOL_SEED},
    {"scale","Scale values",SCREAMSEQ_TOOL_TARGET|SCREAMSEQ_TOOL_AMOUNT},
    {"fill","Fill values",SCREAMSEQ_TOOL_TARGET|SCREAMSEQ_TOOL_FROM},
    {"transpose","Transpose",SCREAMSEQ_TOOL_AMOUNT},
    {"remapInstrument","Remap instrument",SCREAMSEQ_TOOL_REMAP},
    {"reverse","Reverse rows",SCREAMSEQ_TOOL_FIELDS},
    {"rotate","Rotate rows",SCREAMSEQ_TOOL_FIELDS|SCREAMSEQ_TOOL_AMOUNT},
    {"expand","Expand timing",SCREAMSEQ_TOOL_FIELDS|SCREAMSEQ_TOOL_AMOUNT|SCREAMSEQ_TOOL_LOSS},
    {"shrink","Shrink timing",SCREAMSEQ_TOOL_FIELDS|SCREAMSEQ_TOOL_AMOUNT|SCREAMSEQ_TOOL_LOSS},
    {"clear","Clear fields",SCREAMSEQ_TOOL_FIELDS},
    {"insertRows","Insert rows",SCREAMSEQ_TOOL_FIELDS|SCREAMSEQ_TOOL_AMOUNT|SCREAMSEQ_TOOL_LOSS},
    {"deleteRows","Delete rows",SCREAMSEQ_TOOL_FIELDS|SCREAMSEQ_TOOL_AMOUNT|SCREAMSEQ_TOOL_LOSS}
  };
  const ScreamSeqPatternToolDescriptor missing={0,0,0};
  return index<SCREAMSEQ_PATTERN_TOOL_COUNT?tools[index]:missing;
}
#endif
