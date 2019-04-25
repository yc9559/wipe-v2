#include "sim.h"
#include <iostream>
#include <numeric>

Sim::Score Sim::Run(const Workload &workload, const Workload &idleload, Soc soc, bool is_init) {
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

    int quantum_cnt = 0;
    int capacity    = soc.clusters_[0].CalcCapacity();

    std::vector<uint32_t> capacity_log;
    capacity_log.reserve(workload.windowed_load_.size());

    std::vector<uint32_t> power_log(workload.windowed_load_.size(), base_pwr);

    for (Workload::LoadSlice w : workload.windowed_load_) {
        capacity_log.push_back(capacity);

        AdaptLoad(w.max_load, capacity);
        AdaptLoad(w.load, workload.core_num_, capacity);

        power_log[quantum_cnt] += QuantifyPower(hmp.CalcPower(w.load));

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

    double complexity = CalcComplexity(little_governor, big_governor) - 1.0;

    if (is_init) {
        default_score_.ref_power_comsumed = InitRefBattPartition(power_log);
    }

    double perf         = EvalPerformance(workload, soc, capacity_log) + misc_.complexity_fraction * complexity;
    double work_lasting = EvalBatterylife(power_log);
    double idle_lasting = EvalIdleLasting(idle_power_comsumed);

    if (is_init) {
        return {perf, work_lasting, idle_lasting, default_score_.ref_power_comsumed};
    } else {
        return {perf, work_lasting, idle_lasting, {0}};
    }
}

double Sim::EvalPerformance(const Workload &workload, const Soc &soc, const std::vector<uint32_t> &capacity_log) {
    const auto &big             = soc.clusters_[1].model_;
    const int   enough_capacity = soc.GetEnoughCapacity();
    auto is_lag = [=](int required, int provided) { return (provided < required) && (provided < enough_capacity); };

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

    double render_lag_ratio = PerfPartitionEval(render_lag_seq);
    double common_lag_ratio = PerfPartitionEval(common_lag_seq);

    double score = misc_.render_fraction * render_lag_ratio + misc_.common_fraction * common_lag_ratio;

    return (score / default_score_.performance);
}

double Sim::PerfPartitionEval(const std::vector<bool> &lag_seq) const {
    const int partition_len = misc_.perf_partition_len;
    const int n_partition   = lag_seq.size() / partition_len;
    const int seq_lag_l1    = misc_.seq_lag_l1;
    const int seq_lag_l2    = misc_.seq_lag_l2;
    const int seq_lag_max   = misc_.seq_lag_max;

    std::vector<int> period_lag_arr;
    period_lag_arr.reserve(n_partition);

    int cnt            = 1;
    int period_lag_cnt = 0;
    int n_recent_lag   = 0;
    for (const auto &is_lag : lag_seq) {
        if (cnt == partition_len) {
            period_lag_arr.push_back(period_lag_cnt);
            period_lag_cnt = 0;
            cnt            = 0;
        }
        if (!is_lag) {
            n_recent_lag = n_recent_lag >> 1;
        }
        n_recent_lag = std::min(seq_lag_max, n_recent_lag + is_lag);
        period_lag_cnt += (n_recent_lag >= seq_lag_l1);
        period_lag_cnt += (n_recent_lag >= seq_lag_l2);
        ++cnt;
    }

    uint64_t sum = 0;
    for (const auto &l : period_lag_arr) {
        sum += l * l;
    }

    return std::sqrt(sum / n_partition);
}

double Sim::EvalBatterylife(const std::vector<uint32_t> &power_log) const {
    double partitional = BattPartitionEval(power_log);
    return (1.0 / (partitional * default_score_.battery_life));
}

double Sim::BattPartitionEval(const std::vector<uint32_t> &power_seq) const {
    const int partition_len = misc_.batt_partition_len;
    const int n_partition   = power_seq.size() / partition_len;

    std::vector<uint64_t> period_power_arr;
    period_power_arr.reserve(n_partition);

    int      cnt                   = 1;
    uint64_t period_power_comsumed = 0;
    for (const auto &power_comsumed : power_seq) {
        if (cnt == partition_len) {
            period_power_arr.push_back(period_power_comsumed);
            period_power_comsumed = 0;
            cnt                   = 0;
        }
        period_power_comsumed += power_comsumed;
        ++cnt;
    }

    double sum = 0;
    for (int i = 0; i < n_partition; ++i) {
        double t = (double)period_power_arr[i] / default_score_.ref_power_comsumed[i];
        sum += t * t;
    }

    return std::sqrt(sum / n_partition);
}

std::vector<uint64_t> Sim::InitRefBattPartition(const std::vector<uint32_t> &power_seq) const {
    const int partition_len = misc_.batt_partition_len;
    const int n_partition   = power_seq.size() / partition_len;

    std::vector<uint64_t> period_power_arr;
    period_power_arr.reserve(n_partition);

    int      cnt                   = 1;
    uint64_t period_power_comsumed = 0;
    for (const auto &power_comsumed : power_seq) {
        if (cnt == partition_len) {
            period_power_arr.push_back(period_power_comsumed);
            period_power_comsumed = 0;
            cnt                   = 0;
        }
        period_power_comsumed += power_comsumed;
        ++cnt;
    }

    return period_power_arr;
}

double Sim::CalcComplexity(const Interactive &little, const Interactive &big) const {
    const int kAboveMinLen      = 10;
    const int kTargetloadMinLen = 10;

    double clpx[4] = {0.0, 0.0, 0.0, 0.0};
    int    i       = 0;

    clpx[i++] = std::max(kAboveMinLen, little.GetAboveHispeedDelayGearNum()) / (double)kAboveMinLen;
    clpx[i++] = std::max(kTargetloadMinLen, little.GetTargetLoadGearNum()) / (double)kTargetloadMinLen;
    clpx[i++] = std::max(kAboveMinLen, big.GetAboveHispeedDelayGearNum()) / (double)kAboveMinLen;
    clpx[i++] = std::max(kTargetloadMinLen, big.GetTargetLoadGearNum()) / (double)kTargetloadMinLen;

    double sum = 0.0;
    for (const auto &n : clpx) {
        sum += n * n;
    }

    return std::sqrt(sum / 4);
}
