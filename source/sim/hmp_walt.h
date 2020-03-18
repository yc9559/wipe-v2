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
        int sched_boost;
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

#endif
