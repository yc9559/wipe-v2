#ifndef __RANK_H
#define __RANK_H

#include <algorithm>
#include <cmath>
#include <vector>
#include "cpumodel.h"
#include "sim_types.h"
#include "workload.h"

// 评分
class Rank {
public:
#define POWER_SHIFT 4

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
        int    perf_partition_len;
        int    seq_lag_l1;
        int    seq_lag_l2;
        int    seq_lag_max;
        double seq_lag_l0_scale;
        double seq_lag_l1_scale;
        double seq_lag_l2_scale;
        double enough_penalty; 
        int    batt_partition_len;
    } MiscConst;

    using LagSeq = std::vector<float>;

    Rank() = delete;
    Rank(const Score &default_score, const MiscConst &misc) : misc_(misc), default_score_(default_score){};
    Score Eval(const Workload &workload, const Workload &idleload, const SimResultPack &rp, Soc soc, bool is_init);

private:
    int QuantifyPower(int power) const { return (power >> POWER_SHIFT); }

    void AdaptLoad(int &load, int capacity) const { load = std::min(load, capacity); }
    void AdaptLoad(int *loads, int n_loads, int capacity) const {
        for (int i = 0; i < n_loads; ++i) {
            loads[i] = std::min(loads[i], capacity);
        }
    }

    double PerfPartitionEval(const LagSeq &lag_seq) const;
    double BattPartitionEval(const SimSeq &power_seq) const;

    double EvalPerformance(const Workload &workload, const Soc &soc, const SimSeq &capacity_log);
    double EvalBatterylife(const SimSeq &power_log) const;

    std::vector<uint64_t> InitRefBattPartition(const SimSeq &power_seq) const;

    double EvalIdleLasting(uint64_t idle_power_comsumed) const {
        return (1.0 / (idle_power_comsumed * default_score_.idle_lasting));
    }

    MiscConst misc_;
    Score     default_score_;
};

#endif
