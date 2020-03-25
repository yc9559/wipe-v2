#ifndef __HMP_PELT_H
#define __HMP_PELT_H

#include "hmp.h"

class PeltHmp : public Hmp {
public:
    struct Tunables : public HmpTunables {
        int load_avg_period_ms;
        int down_threshold;
        int up_threshold;
        int boost;
        int timer_rate;
    };

    struct Cfg : public HmpCfg {
        Tunables tunables;
    };

    PeltHmp(){};
    PeltHmp(Cfg cfg);
    int SchedulerTick(int max_load, const int *loads, int n_load, int now);

    Tunables GetTunables(void) { return tunables_; }
    void     SetTunables(const Tunables &t) { tunables_ = t; }

private:
    uint64_t UpdateBusyTime(int max_load);
    void     InitDecay(int ms, int n);

    Tunables tunables_;
    uint64_t demand_;
    uint64_t up_demand_thd_;
    uint64_t down_demand_thd_;
    int      entry_cnt_;
    uint64_t max_load_sum_;
    uint32_t decay_ratio_;
    uint32_t load_avg_max_;
    int      governor_cnt_;
};

#endif
