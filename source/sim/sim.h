#ifndef __SIM_H
#define __SIM_H

#include <algorithm>
#include <cmath>
#include <vector>
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
        double                performance;
        double                battery_life;
        double                idle_lasting;
        std::vector<uint64_t> ref_power_comsumed;
    } Score;

    typedef struct _MiscConst {
        double render_fraction;
        double common_fraction;
        double complexity_fraction;
        int    working_base_mw;
        int    idle_base_mw;
        int    perf_partition_len;
        int    seq_lag_l1;
        int    seq_lag_l2;
        int    seq_lag_max;
        int    batt_partition_len;
    } MiscConst;

    Sim() = delete;
    Sim(const Tunables &tunables, const Score &default_score, const MiscConst &misc)
        : tunables_(tunables), misc_(misc), default_score_(default_score){};
    Score Run(const Workload &workload, const Workload &idleload, Soc soc, bool is_init);

private:
    int    QuantifyPower(int power) const;
    double CalcComplexity(const Interactive &little, const Interactive &big) const;

    void AdaptLoad(int *loads, int n_loads, int capacity) const;
    void AdaptLoad(int &load, int capacity) const { load = std::min(load, capacity); }

    double PerfPartitionEval(const std::vector<bool> &lag_seq) const;
    double BattPartitionEval(const std::vector<uint32_t> &power_seq) const;

    double EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<uint32_t> &capacity_log);
    double EvalBatterylife(const std::vector<uint32_t> &power_log) const;

    std::vector<uint64_t> InitRefBattPartition(const std::vector<uint32_t> &power_seq) const;

    double EvalIdleLasting(uint64_t idle_power_comsumed) const {
        return (1.0 / (idle_power_comsumed * default_score_.idle_lasting));
    }

    Tunables  tunables_;
    MiscConst misc_;
    Score     default_score_;
};

inline int Sim::QuantifyPower(int power) const {
    return (power >> POWER_SHIFT);
}

inline void Sim::AdaptLoad(int *loads, int n_loads, int capacity) const {
    for (int i = 0; i < n_loads; ++i) {
        loads[i] = std::min(loads[i], capacity);
    }
}

#endif
