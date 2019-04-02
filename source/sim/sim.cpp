#include "sim.h"
#include <cmath>
#include <vector>
#include <iostream>

Sim::Score Sim::Run(const Workload &workload, Soc soc) {
    const int base_pwr = QuantifyPower(800 * 100);

    Interactive little_governor(tunables_.interactive[0], &soc.clusters_[0]);
    Interactive big_governor(tunables_.interactive[1], &soc.clusters_[1]);
    InputBoost  input(tunables_.input);

    WaltHmp::Cfg waltcfg;
    waltcfg.tunables        = tunables_.sched;
    waltcfg.little          = &soc.clusters_[0];
    waltcfg.big             = &soc.clusters_[1];
    waltcfg.governor_little = &little_governor;
    waltcfg.governor_big    = &big_governor;
    WaltHmp hmp(waltcfg);

    uint64_t power_comsumed = base_pwr * workload.windowed_load_.size();
    int      quantum_cnt    = 0;
    int      capacity       = soc.clusters_[0].CalcCapacity();

    std::vector<uint32_t> capacity_log;
    capacity_log.reserve(workload.windowed_load_.size());

    for (Workload::LoadSlice w : workload.windowed_load_) {
        capacity_log.push_back(capacity);

        AdaptLoad(w.max_load, capacity);
        AdaptLoad(w.load, workload.core_num_, capacity);

        power_comsumed += QuantifyPower(hmp.CalcPower(w.load));

        input.HandleInput(soc, w.has_input_event, quantum_cnt);
        capacity = hmp.WaltScheduler(w.max_load, w.load, workload.core_num_, quantum_cnt);

        quantum_cnt++;
    }

    Score ret = {EvalPerformance(workload, soc, capacity_log), EvalBatterylife(power_comsumed)};
    return ret;
}

inline double PartitionEval(const std::vector<bool> &lag_seq) {
    const int kPartitionLen = 200;

    int n_partition = lag_seq.size() / kPartitionLen;
    std::vector<int> period_lag_arr;
    period_lag_arr.reserve(n_partition);

    int cnt = 1;
    int period_lag_cnt = 0;
    for (const auto &is_lag : lag_seq) {
        if (cnt == kPartitionLen) {
            period_lag_arr.push_back(period_lag_cnt);
            period_lag_cnt = 0;
            cnt = 0;
        }
        period_lag_cnt += is_lag;
        ++cnt;
    }

    // std::sort(period_lag_arr.begin(), period_lag_arr.end());

    int64_t sum = 0;
    for (const auto &l : period_lag_arr) {
        sum += l * l;
    }
    double l2_regularization = std::sqrt(sum);

    return (l2_regularization / kPartitionLen);
}

double Sim::EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<uint32_t> &capacity_log) {
    auto is_lag = [=](int required, int provided) { return (required < 1958 * 100 * 1638) && (required > provided); };

    std::vector<bool> common_lag_seq;
    common_lag_seq.reserve(capacity_log.size());

    auto iter_log = capacity_log.begin();
    for (const auto &loadslice : workload.windowed_load_) {
        common_lag_seq.push_back(is_lag(loadslice.max_load, *iter_log));
        ++iter_log;
    }

    std::vector<bool> render_lag_seq;
    render_lag_seq.reserve(capacity_log.size());

    for (const auto &r : workload.render_load_) {
        uint64_t aggreated_capacity = 0;
        aggreated_capacity += capacity_log[r.window_idxs[0]] * r.window_quantums[0];
        aggreated_capacity += capacity_log[r.window_idxs[1]] * r.window_quantums[1];
        aggreated_capacity += capacity_log[r.window_idxs[2]] * r.window_quantums[2];
        aggreated_capacity /= workload.frame_quantum_;
        render_lag_seq.push_back(is_lag(r.frame_load, aggreated_capacity));
    }

    double render_lag_ratio = PartitionEval(render_lag_seq);
    double common_lag_ratio = PartitionEval(common_lag_seq);

    double score = 0.85 * render_lag_ratio + 0.15 * common_lag_ratio;

    return (score / default_score_.performance);
}
