#ifndef __HMP_H
#define __HMP_H

#include <stdint.h>
#include "cpumodel.h"
#include "interactive.h"

class Hmp {
public:
    struct HmpTunables {};

    struct HmpCfg {
        Cluster *    little;
        Cluster *    big;
        Interactive *governor_little;
        Interactive *governor_big;
    };

    Hmp(){};
    Hmp(HmpCfg cfg)
        : little_(cfg.little),
          big_(cfg.big),
          active_(big_),
          idle_(little_),
          governor_little_(cfg.governor_little),
          governor_big_(cfg.governor_big) {
        cluster_num_ = (big_ == little_) ? 1 : 2;
    }

    int SchedulerTick(int max_load, const int *loads, int n_load, int now) { return 0; };
    int CalcPower(const int *loads) const;
    int CalcPowerForIdle(const int *loads) const;

protected:
#define NLoadsMax 4
    int LoadToBusyPct(const Cluster *c, uint64_t load) const;

    Cluster *    little_;
    Cluster *    big_;
    Cluster *    active_;
    Cluster *    idle_;
    Interactive *governor_little_;
    Interactive *governor_big_;
    int          cluster_num_;
};

inline int Hmp::LoadToBusyPct(const Cluster *c, uint64_t load) const {
    return (load / (c->cur_freq_ * c->model_.efficiency));
}

// 外层保证已执行adaptload，负载百分比不超过100%
// loads: freq * busy_pct * efficiency
inline int Hmp::CalcPower(const int *loads) const {
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
inline int Hmp::CalcPowerForIdle(const int *loads) const {
    const int idle_load_pcts[] = {100, 0, 0, 0};
    int       pwr              = 0;
    if (active_ == little_) {
        pwr += little_->CalcPower(idle_load_pcts);
    } else {
        pwr += little_->CalcPower(idle_load_pcts);
        pwr += big_->CalcPower(idle_load_pcts);
    }
    return pwr;
}

#endif
