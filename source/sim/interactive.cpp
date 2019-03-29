#include "interactive.h"
#include <algorithm>
extern "C" {
#include <limits.h>
#include <stdbool.h>
}

Interactive::Interactive(Tunables tunables, Cluster *cm)
    : cluster_(cm),
      tunables_(tunables),
      target_freq(cm->model_.max_freq),
      floor_freq(cm->model_.max_freq),
      max_freq_hyst_start_time(0),
      hispeed_validate_time(0),
      floor_validate_time(0) {
}

int Interactive::choose_freq(int freq, int load) const {
    const uint32_t loadadjfreq = freq * load;
    uint32_t       prevfreq, freqmin, freqmax, tl;

    freqmin = 0;
    freqmax = UINT_MAX;

    do {
        prevfreq = freq;
        tl       = freq_to_targetload(freq);
        freq     = cluster_->freq_floor_to_opp(loadadjfreq / tl);

        if (freq > prevfreq) {
            /* The previous frequency is too low. */
            freqmin = prevfreq;
            if (freq >= freqmax) {
                freq = cluster_->freq_ceiling_to_opp(freqmax - 1);
                if (freq == freqmin) {
                    freq = freqmax;
                    break;
                }
            }
        } else if (freq < prevfreq) {
            /* The previous frequency is high enough. */
            freqmax = prevfreq;
            if (freq <= freqmin) {
                freq = cluster_->freq_floor_to_opp(freqmin + 1);
                if (freq == freqmax)
                    break;
            }
        }
    } while (freq != prevfreq);

    return freq;
}

int Interactive::InteractiveTimer(int load, int now) {
    bool           skip_hispeed_logic   = false;
    bool           skip_min_sample_time = false;
    bool           jump_to_max_no_ts    = false;
    bool           jump_to_max          = false;
    constexpr bool boosted              = 0;
    // bool boosted              = now < boostpulse_endtime; // touch->store_boostpulse->boostpulse_endtime
    // 通路不再使用，改用input_boost
    int new_freq = choose_freq(target_freq, load);
    // printf("choosefreq:%d\n", new_freq);

    if (now - max_freq_hyst_start_time < tunables_.max_freq_hysteresis && load >= tunables_.go_hispeed_load) {
        skip_hispeed_logic   = true;
        skip_min_sample_time = true;
        if (!jump_to_max)
            jump_to_max_no_ts = true;
    }

    if (jump_to_max_no_ts || jump_to_max) {
        new_freq = cluster_->model_.max_freq;
    } else if (!skip_hispeed_logic) {
        if (load >= tunables_.go_hispeed_load || boosted) {
            if (target_freq < tunables_.hispeed_freq)
                new_freq = tunables_.hispeed_freq;
            else
                new_freq = std::max(new_freq, tunables_.hispeed_freq);
        }
    }

    if (now - max_freq_hyst_start_time < tunables_.max_freq_hysteresis) {
        new_freq = std::max(tunables_.hispeed_freq, new_freq);
    }
    if (!skip_hispeed_logic && target_freq >= tunables_.hispeed_freq && new_freq > target_freq &&
        now - hispeed_validate_time < freq_to_above_hispeed_delay(target_freq)) {
        return target_freq;
    }

    hispeed_validate_time = now;

    new_freq = cluster_->freq_floor_to_opp(new_freq);

    /*
     * Do not scale below floor_freq unless we have been at or above the
     * floor frequency for the minimum sample time since last validated.
     */
    if (!skip_min_sample_time && new_freq < floor_freq) {
        if (now - floor_validate_time < tunables_.min_sample_time) {
            return target_freq;
        }
    }

    /*
     * Update the timestamp for checking whether speed has been held at
     * or above the selected frequency for a minimum of min_sample_time,
     * if not boosted to hispeed_freq.  If boosted to hispeed_freq then we
     * allow the speed to drop as soon as the boostpulse duration expires
     * (or the indefinite boost is turned off). If policy->max is restored
     * for max_freq_hysteresis, don't extend the timestamp. Otherwise, it
     * could incorrectly extended the duration of max_freq_hysteresis by
     * min_sample_time.
     */
    if ((!boosted || new_freq > tunables_.hispeed_freq) && !jump_to_max_no_ts) {
        floor_freq          = new_freq;
        floor_validate_time = now;
    }

    if (new_freq >= cluster_->model_.max_freq && !jump_to_max_no_ts) {
        max_freq_hyst_start_time = now;
    }
    target_freq = new_freq;

    return target_freq;
}
