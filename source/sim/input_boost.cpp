#include "input_boost.h"
#include "hmp_pelt.h"
#include "hmp_walt.h"

TouchBoost::Tunables::_Tunables(const Soc *soc)
{
    int idx = 0;
    for (const auto &cluster : soc->clusters_) {
        boost_freq[idx++] = cluster.freq_floor_to_opp(cluster.model_.max_freq * 0.6);
    }
    // 默认不拉大核的最低频率
    if (soc->clusters_.size() > 1)
        boost_freq[1] = soc->clusters_[1].model_.min_freq;
    duration_quantum = 100;
}

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
