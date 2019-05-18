#include "rank.h"
#include <numeric>

Rank::Score Rank::Eval(const Workload &workload, const Workload &idleload, const SimResultPack &rp, Soc soc, bool is_init) {
    if (is_init) {
        default_score_.ref_power_comsumed = InitRefBattPartition(rp.onscreen.power);
    }

    double perf         = EvalPerformance(workload, soc, rp.onscreen.capacity);
    double work_lasting = EvalBatterylife(rp.onscreen.power);
    double idle_lasting = EvalIdleLasting(rp.offscreen_pwr);

    if (is_init) {
        return {perf, work_lasting, idle_lasting, default_score_.ref_power_comsumed};
    } else {
        return {perf, work_lasting, idle_lasting, {0}};
    }
}

double Rank::EvalPerformance(const Workload &workload, const Soc &soc, const SimSeq &capacity_log) {
    const int enough_capacity = soc.GetEnoughCapacity();
    auto is_lag = [=](int required, int provided) { return (provided < required) && (provided < enough_capacity); };

    // std::vector<bool> common_lag_seq;
    // common_lag_seq.reserve(capacity_log.size());

    // auto iter_log = capacity_log.begin();
    // for (const auto &loadslice : workload.windowed_load_) {
    //     common_lag_seq.push_back(is_lag(loadslice.max_load, *iter_log));
    //     ++iter_log;
    // }

    std::vector<bool> render_lag_seq;
    render_lag_seq.reserve(workload.render_load_.size());

    for (const auto &r : workload.render_load_) {
        uint64_t aggreated_capacity = 0;
        aggreated_capacity += capacity_log[r.window_idxs[0]] * r.window_quantums[0];
        aggreated_capacity += capacity_log[r.window_idxs[1]] * r.window_quantums[1];
        aggreated_capacity += capacity_log[r.window_idxs[2]] * r.window_quantums[2];
        aggreated_capacity /= workload.frame_quantum_;
        render_lag_seq.push_back(is_lag(r.frame_load, aggreated_capacity));
    }

    // double common_lag_ratio = PerfPartitionEval(common_lag_seq);
    double render_lag_ratio = PerfPartitionEval(render_lag_seq);

    // double score = misc_.render_fraction * render_lag_ratio + misc_.common_fraction * common_lag_ratio;
    double score = render_lag_ratio;

    return (score / default_score_.performance);
}

double Rank::PerfPartitionEval(const std::vector<bool> &lag_seq) const {
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

double Rank::EvalBatterylife(const SimSeq &power_log) const {
    double partitional = BattPartitionEval(power_log);
    return (1.0 / (partitional * default_score_.battery_life));
}

double Rank::BattPartitionEval(const SimSeq &power_seq) const {
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

std::vector<uint64_t> Rank::InitRefBattPartition(const SimSeq &power_seq) const {
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

// double Rank::CalcComplexity(const Interactive &little, const Interactive &big) const {
//     const int kAboveMinLen      = 10;
//     const int kTargetloadMinLen = 10;

//     double clpx[4] = {0.0, 0.0, 0.0, 0.0};
//     int    i       = 0;

//     clpx[i++] = std::max(kAboveMinLen, little.GetAboveHispeedDelayGearNum()) / (double)kAboveMinLen;
//     clpx[i++] = std::max(kTargetloadMinLen, little.GetTargetLoadGearNum()) / (double)kTargetloadMinLen;
//     clpx[i++] = std::max(kAboveMinLen, big.GetAboveHispeedDelayGearNum()) / (double)kAboveMinLen;
//     clpx[i++] = std::max(kTargetloadMinLen, big.GetTargetLoadGearNum()) / (double)kTargetloadMinLen;

//     double sum = 0.0;
//     for (const auto &n : clpx) {
//         sum += n * n;
//     }

//     return std::sqrt(sum / 4);
// }
