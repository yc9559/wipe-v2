#ifndef __SIM_TYPES_H
#define __SIM_TYPES_H

#include <stdint.h>
#include <vector>

typedef std::vector<uint32_t> SimSeq;

typedef struct _SimResult {
    SimSeq capacity;
    SimSeq power;
} SimResult;

typedef struct _SimResultPack {
    SimResult onscreen;
    uint64_t  offscreen_pwr;
} SimResultPack;

#endif
