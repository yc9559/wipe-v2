#ifndef __HMP_H
#define __HMP_H

#include <stdint.h>
#include "cpumodel.h"
#include "interactive.h"

const int kWaltHmpParamFixedLen = 6;

class WaltHmp {
public:
    enum { WINDOW_STATS_RECENT = 0, WINDOW_STATS_MAX, WINDOW_STATS_MAX_RECENT_AVG, WINDOW_STATS_AVG };

    typedef struct _WaltHmpTunables {
        int timer_rate;
        int sched_upmigrate;
        int sched_downmigrate;
        int sched_ravg_hist_size;
        int sched_window_stats_policy;
        int sched_freq_aggregate_threshold_pct;
    } Tunables;

    typedef struct _WaltCfg {
        Tunables     tunables;
        Cluster *    little;
        Cluster *    big;
        Interactive *governor_little;
        Interactive *governor_big;
    } Cfg;

    WaltHmp(){};
    WaltHmp(Cfg cfg);
    int WaltScheduler(int max_load, const int *loads, int n_load, int now);

private:
#define RavgHistSizeMax 5
#define NLoadsMax 4
    Tunables     tunables_;
    Cluster *    little_;
    Cluster *    big_;
    Cluster *    active_;
    Cluster *    idle_;
    Interactive *governor_little_;
    Interactive *governor_big_;
    uint64_t     demand_;
    int          up_demand_thd_;
    int          down_demand_thd_;
    int          sum_history_[RavgHistSizeMax];
    int          entry_cnt_;
    uint64_t     max_load_sum_;
    uint64_t     loads_sum_[NLoadsMax];

    int  LoadToBusyPct(const Cluster *c, uint64_t load) const;
    void update_history(int in_demand);
    int  AggregateLoadToBusyPctIfNeed(const int *loads, int n_load) const;
};

inline int WaltHmp::LoadToBusyPct(const Cluster *c, uint64_t load) const {
    return (load / (c->cur_freq_ * c->model_.efficiency));
}

inline int WaltHmp::AggregateLoadToBusyPctIfNeed(const int *loads, int n_load) const {
    uint64_t aggregated_load = 0;
    for (int i = 0; i < n_load; ++i) {
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
