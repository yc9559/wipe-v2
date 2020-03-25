#include "input_boost.h"
#include "hmp_pelt.h"
#include "hmp_walt.h"

template <typename SchedT>
void TouchBoost::DoBoost(Soc &soc, Interactive &little, Interactive &big, SchedT &sched) {
    int cluster_num = soc.clusters_.size();
    for (int i = 0; i < cluster_num; ++i)
        soc.clusters_[i].SetMinfreq(tunables_.boost_freq[i]);
}

template <typename SchedT>
void TouchBoost::DoResume(Soc &soc, Interactive &little, Interactive &big, SchedT &sched) {
    int cluster_num = soc.clusters_.size();
    for (int i = 0; i < cluster_num; ++i)
        soc.clusters_[i].SetMinfreq(soc.clusters_[i].model_.min_freq);
}
