#ifndef __SIM_H
#define __SIM_H

#include <algorithm>
#include <cmath>
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
        double idle_lasting;
    } Score;

    typedef struct _MiscConst {
        double enough_capacity_pct;
        double render_fraction;
        double common_fraction;
        int    working_base_mw;
        int    idle_base_mw;
        int    partition_len;
        int    seq_lag_min;
    } MiscConst;

    Sim() = delete;
    Sim(const Tunables &tunables, const Score &default_score, const MiscConst &misc)
        : tunables_(tunables), misc_(misc), default_score_(default_score){};
    Score Run(const Workload &workload, const Workload &idleload, Soc soc);

private:
    int    QuantifyPower(int power) const;
    double PartitionEval(const std::vector<bool> &lag_seq) const;
    void   AdaptLoad(int *loads, int n_loads, int capacity) const;
    void   AdaptLoad(int &load, int capacity) const { load = std::min(load, capacity); }

    double EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<uint32_t> &capacity_log);

    double EvalBatterylife(uint64_t power_comsumed) const {
        return (1.0 / power_comsumed / default_score_.battery_life);
    }
    double EvalIdleLasting(uint64_t idle_power_comsumed) const {
        return (1.0 / idle_power_comsumed / default_score_.idle_lasting);
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

inline double Sim::PartitionEval(const std::vector<bool> &lag_seq) const {
    const int partition_len = misc_.partition_len;
    const int n_partition   = lag_seq.size() / partition_len;
    const int seq_lag_min   = misc_.seq_lag_min;

    std::vector<int> period_lag_arr;
    period_lag_arr.reserve(n_partition);

    int  cnt            = 1;
    int  period_lag_cnt = 0;
    int  n_recent_lag   = 0;
    bool is_seq_lag     = false;
    for (const auto &is_lag : lag_seq) {
        if (cnt == partition_len) {
            period_lag_arr.push_back(period_lag_cnt);
            period_lag_cnt = 0;
            cnt            = 0;
        }
        if (!is_lag) {
            n_recent_lag = 0;
        }
        n_recent_lag += is_lag;
        is_seq_lag = (n_recent_lag >= seq_lag_min);
        period_lag_cnt += is_lag + is_seq_lag;
        ++cnt;
    }

    int64_t sum = 0;
    for (const auto &l : period_lag_arr) {
        sum += l * l;
    }

    return std::sqrt(sum / n_partition);
}

#endif
