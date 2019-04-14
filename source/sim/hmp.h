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
    int CalcPower(const int *loads) const;
    int CalcPowerForIdle(const int *loads) const;

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
    uint64_t     up_demand_thd_;
    uint64_t     down_demand_thd_;
    int          sum_history_[RavgHistSizeMax];
    int          entry_cnt_;
    uint64_t     max_load_sum_;
    uint64_t     loads_sum_[NLoadsMax];
    int          governor_cnt_;

    int  LoadToBusyPct(const Cluster *c, uint64_t load) const;
    void update_history(int in_demand);
    int  AggregateLoadToBusyPctIfNeed(const int *loads, int n_load) const;
};

inline int WaltHmp::LoadToBusyPct(const Cluster *c, uint64_t load) const {
    return (load / (c->cur_freq_ * c->model_.efficiency));
}

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

// 外层保证已执行adaptload，负载百分比不超过100%
// loads: freq * busy_pct * efficiency
inline int WaltHmp::CalcPower(const int *loads) const {
    const int idle_load_pcts[] = {1, 0, 0, 0};
    int       load_pcts[NLoadsMax];
    for (int i = 0; i < NLoadsMax; ++i) {
        load_pcts[i] = loads[i] / (active_->model_.efficiency * active_->cur_freq_);
    }

    int pwr = 0;
    pwr += active_->CalcPower(load_pcts);
    pwr += idle_->CalcPower(idle_load_pcts);
    return pwr;
}

// 如果负载没有被移动到大核，则认为大核没有闲置耗电，减少待机时大核上线概率
inline int WaltHmp::CalcPowerForIdle(const int *loads) const {
    const int idle_load_pcts[] = {100, 0, 0, 0};
    int pwr = 0;
    if (active_ == little_) {
        pwr += little_->CalcPower(idle_load_pcts);
    } else {
        pwr += little_->CalcPower(idle_load_pcts);
        pwr += big_->CalcPower(idle_load_pcts);
    }
    return pwr;
}

#endif
