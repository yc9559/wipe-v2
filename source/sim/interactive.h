#ifndef __INTERACTIVE_H
#define __INTERACTIVE_H

#include <stdint.h>
#include "cpumodel.h"

const int kInteractiveParamFixedLen = 4;
#define TARGET_LOAD_MAX_LEN 24
#define ABOVE_DELAY_MAX_LEN 32

class Interactive {
public:
    // 64Byte，与CPU缓存对齐
    typedef struct alignas(1) _InteractiveTunables {
        int      hispeed_freq;
        uint16_t go_hispeed_load;
        uint8_t  min_sample_time;
        uint8_t  max_freq_hysteresis;
        uint8_t  above_hispeed_delay[ABOVE_DELAY_MAX_LEN];
        uint8_t  target_loads[TARGET_LOAD_MAX_LEN];
    } Tunables;

    Interactive() = delete;
    Interactive(Tunables tunables, Cluster *cm)
        : tunables_(tunables),
          cluster_(cm),
          target_freq(cm->model_.max_freq),
          floor_freq(cm->model_.max_freq),
          max_freq_hyst_start_time(0),
          hispeed_validate_time(0),
          floor_validate_time(0) {}

    int InteractiveTimer(int load, int now);
    int GetAboveHispeedDelayGearNum(void) const;
    int GetTargetLoadGearNum(void) const;

private:
    int freq_to_targetload(int freq) const;
    int freq_to_above_hispeed_delay(int freq) const;
    int choose_freq(int freq, int load) const;

    const Tunables tunables_;
    const Cluster *cluster_;

    int target_freq;
    int floor_freq;
    int max_freq_hyst_start_time;
    int hispeed_validate_time;
    int floor_validate_time;
};

inline int Interactive::freq_to_targetload(int freq) const {
    return tunables_.target_loads[std::min(TARGET_LOAD_MAX_LEN - 1, cluster_->FindIdxWithFreqFloor(freq, 0))];
}

inline int Interactive::freq_to_above_hispeed_delay(int freq) const {
    return tunables_.above_hispeed_delay[std::min(ABOVE_DELAY_MAX_LEN - 1, cluster_->FindIdxWithFreqFloor(freq, 0))];
}

#endif
