#ifndef __SIM_H
#define __SIM_H

#include <algorithm>
#include "cpumodel.h"
#include "hmp.h"
#include "interactive.h"
#include "workload.h"

const int kInputBoostParamFixedLen = 3;

class InputBoost {
public:
    typedef struct _Tunables {
        int boost_freq[2];
        int duration_quantum;
    } Tunables;

    InputBoost(const Tunables &tunables) : tunables_(tunables), input_happened_quantum_(0), is_in_boost_(false) {}

    void HandleInput(Soc &soc, int has_input, int cur_quantum) {
        if (has_input && tunables_.duration_quantum) {
            soc.clusters_[0].SetMinfreq(tunables_.boost_freq[0]);
            soc.clusters_[1].SetMinfreq(tunables_.boost_freq[1]);
            input_happened_quantum_ = cur_quantum;
            is_in_boost_            = true;
            return;
        }

        if (is_in_boost_ && cur_quantum - input_happened_quantum_ > tunables_.duration_quantum) {
            soc.clusters_[0].SetMinfreq(soc.clusters_[0].model_.min_freq);
            soc.clusters_[1].SetMinfreq(soc.clusters_[1].model_.min_freq);
            is_in_boost_ = false;
        }
        return;
    };

private:
    InputBoost();

    Tunables tunables_;
    int      input_happened_quantum_;
    bool     is_in_boost_;
};

class Sim {
public:
#define POWER_SHIFT 4

    typedef struct _Tunables {
        Interactive::Tunables interactive[2];
        WaltHmp::Tunables     sched;
        InputBoost::Tunables  input;
    } Tunables;

    typedef struct _Score {
        double performance;
        double battery_life;
    } Score;

    Sim(const Tunables &tunables, const Score &default_score) : tunables_(tunables), default_score_(default_score){};
    Score  Run(const Workload &workload, Soc soc);

private:
    int  QuantifyPower(int power);
    void AdaptLoad(int &load, int capacity) { load = std::min(load, capacity); }
    void AdaptLoad(int *loads, int n_loads, int capacity) {
        for (int i = 0; i < n_loads; ++i) {
            loads[i] = std::min(loads[i], capacity);
        }
    }
    double EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<int> &capacity_log);
    double EvalBatterylife(uint64_t power_comsumed) const {
        return (1.0 / power_comsumed / default_score_.battery_life);
    };

    Tunables tunables_;
    Score    default_score_;
};

inline int Sim::QuantifyPower(int power) {
    return (power >> POWER_SHIFT);
}

#endif
