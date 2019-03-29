#include "sim.h"
#include <vector>
#include <iostream>

Sim::Score Sim::Run(const Workload &workload, Soc &soc) {
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

    std::vector<int> capacity_log;
    capacity_log.reserve(workload.windowed_load_.size());

    for (Workload::LoadSlice w : workload.windowed_load_) {
        capacity_log.push_back(capacity);

        AdaptLoad(w.max_load, capacity);
        AdaptLoad(w.load, workload.core_num_, capacity);
        for (const auto &cl : soc.clusters_) {
            power_comsumed += QuantifyPower(cl.CalcPower(w.load));
        }

        input.HandleInput(soc, w.has_input_event, quantum_cnt);
        capacity = hmp.WaltScheduler(w.max_load, w.load, workload.core_num_, quantum_cnt);

        quantum_cnt++;
    }

    Score ret = {EvalPerformance(workload, soc, capacity_log), EvalBatterylife(power_comsumed)};
    return ret;
}

double Sim::EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<int> &capacity_log) {
    auto is_lag = [=](int required, int provided) { return required > provided; };

    std::vector<bool> common_lag_seq;
    common_lag_seq.reserve(capacity_log.size());

    auto iter_log = capacity_log.begin();
    for (const auto &loadslice : workload.windowed_load_) {
        common_lag_seq.push_back(is_lag(loadslice.max_load, *iter_log));
        ++iter_log;
    }

    std::vector<bool> render_lag_seq;
    render_lag_seq.reserve(capacity_log.size());
    int n_lag = 0;

    for (const auto &r : workload.render_load_) {
        int aggreated_capacity = 0;
        aggreated_capacity += capacity_log[r.window_idxs[0]] * r.window_quantums[0];
        aggreated_capacity += capacity_log[r.window_idxs[1]] * r.window_quantums[1];
        aggreated_capacity += capacity_log[r.window_idxs[2]] * r.window_quantums[2];
        aggreated_capacity /= workload.frame_quantum_;
        render_lag_seq.push_back(r.frame_load > aggreated_capacity);
        if (r.frame_load > aggreated_capacity) {
            ++n_lag;
        }
    }

    float ratio = (float)n_lag / workload.render_load_.size();

    return (ratio / default_score_.performance);
}
