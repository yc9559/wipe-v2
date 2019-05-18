#ifndef __HMP_WALT_H
#define __HMP_WALT_H

#include "hmp.h"

class WaltHmp : public Hmp {
public:
    enum { WINDOW_STATS_RECENT = 0, WINDOW_STATS_MAX, WINDOW_STATS_MAX_RECENT_AVG, WINDOW_STATS_AVG };

    struct Tunables : public HmpTunables {
        int timer_rate;
        int sched_upmigrate;
        int sched_downmigrate;
        int sched_ravg_hist_size;
        int sched_window_stats_policy;
        int sched_freq_aggregate_threshold_pct;
    };

    struct Cfg : public HmpCfg {
        Tunables tunables;
    };

    WaltHmp(){};
    WaltHmp(Cfg cfg);
    int SchedulerTick(int max_load, const int *loads, int n_load, int now);

private:
#define RavgHistSizeMax 5

    void update_history(int in_demand);
    int  AggregateLoadToBusyPctIfNeed(const int *loads, int n_load) const;

    Tunables tunables_;
    uint64_t demand_;
    uint64_t up_demand_thd_;
    uint64_t down_demand_thd_;
    int      sum_history_[RavgHistSizeMax];
    int      entry_cnt_;
    uint64_t max_load_sum_;
    uint64_t loads_sum_[NLoadsMax];
    int      governor_cnt_;
};

inline int WaltHmp::AggregateLoadToBusyPctIfNeed(const int *loads, int n_load) const {
    uint64_t aggregated_load = 0;
    for (int i = 0; i < active_->model_.core_num; ++i) {
        aggregated_load += loads[i];
    }
    int aggregated_busy_pct = LoadToBusyPct(active_, aggregated_load);
    if (aggregated_busy_pct > tunables_.sched_freq_aggregate_threshold_pct) {
        return aggregated_busy_pct;
    } else {
        return LoadToBusyPct(active_, demand_);
    }
}

#endif
