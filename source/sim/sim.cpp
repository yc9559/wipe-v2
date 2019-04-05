#include "sim.h"
#include <iostream>
#include <vector>

Sim::Score Sim::Run(const Workload &workload, const Workload &idleload, Soc soc) {
    const int base_pwr      = QuantifyPower(misc_.working_base_mw * 100);
    const int idle_base_pwr = QuantifyPower(misc_.idle_base_mw * 100);

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

    uint64_t idle_power_comsumed = idle_base_pwr * idleload.windowed_load_.size();

    for (Workload::LoadSlice w : idleload.windowed_load_) {
        AdaptLoad(w.max_load, capacity);
        AdaptLoad(w.load, idleload.core_num_, capacity);

        idle_power_comsumed += QuantifyPower(hmp.CalcPowerForIdle(w.load));

        input.HandleInput(soc, w.has_input_event, quantum_cnt);
        capacity = hmp.WaltScheduler(w.max_load, w.load, idleload.core_num_, quantum_cnt);

        quantum_cnt++;
    }

    double perf         = EvalPerformance(workload, soc, capacity_log);
    double work_lasting = EvalBatterylife(power_comsumed);
    double idle_lasting = EvalIdleLasting(idle_power_comsumed);

    Score ret = {perf, work_lasting, idle_lasting};
    return ret;
}

double Sim::EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<uint32_t> &capacity_log) {
    const auto &big             = soc.clusters_[1].model_;
    const int   enough_capacity = big.max_freq * big.efficiency * misc_.enough_capacity_pct;
    auto is_lag = [=](int required, int provided) { return (required < enough_capacity) && (required > provided); };

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

    double score = misc_.render_fraction * render_lag_ratio + misc_.common_fraction * common_lag_ratio;

    return (score / default_score_.performance);
}
