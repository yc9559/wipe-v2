#include "interactive.h"

#include <algorithm>
extern "C" {
#include <limits.h>
#include <stdbool.h>
}

Interactive::Tunables::_InteractiveTunables(const Cluster &cm) {
    hispeed_freq        = cm.freq_floor_to_opp(cm.model_.max_freq * 0.6);
    go_hispeed_load     = 90;
    min_sample_time     = 1;
    max_freq_hysteresis = 2;

    int n_opp         = cm.model_.opp_model.size();
    int n_above       = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
    int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);

    for (int i = 0; i < n_above; ++i) {
        above_hispeed_delay[i] = 1;
    }
    for (int i = 0; i < n_targetloads; ++i) {
        target_loads[i] = 90;
    }
}

int Interactive::choose_freq(int freq, int load) const {
    const int loadadjfreq = freq * load;
    int       prevfreq, freqmin, freqmax, tl;

    freqmin = 0;
    freqmax = INT_MAX;

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

int Interactive::GetAboveHispeedDelayGearNum(void) const {
    auto get_freq = [=](int idx) { return cluster_->model_.opp_model[idx].freq; };

    const auto &t       = tunables_;
    const int   n_opp   = cluster_->model_.opp_model.size();
    const int   n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);

    int anchor_val = -1;
    int n_gears    = 0;

    for (int i = 0; i < n_above; ++i) {
        if (get_freq(i) < t.hispeed_freq) {
            continue;
        }
        if (anchor_val != t.above_hispeed_delay[i]) {
            anchor_val = t.above_hispeed_delay[i];
            n_gears++;
        }
    }

    return n_gears;
}

int Interactive::GetTargetLoadGearNum(void) const {
    const auto &t             = tunables_;
    const int   n_opp         = cluster_->model_.opp_model.size();
    const int   n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);

    int anchor_val = -1;
    int n_gears    = 0;

    for (int i = 0; i < n_targetloads; ++i) {
        if (anchor_val != t.target_loads[i]) {
            anchor_val = t.target_loads[i];
            n_gears++;
        }
    }

    return n_gears;
}
