#include "input_boost.h"

#include "hmp_pelt.h"
#include "hmp_walt.h"

template <typename GovernorT, typename SchedT>
InputBoost<GovernorT, SchedT>::Tunables::Tunables(const Soc *soc) {
    int idx = 0;
    for (const auto &cluster : soc->clusters_) {
        boost_freq[idx++] = cluster.freq_floor_to_opp(cluster.model_.max_freq * 0.6);
    }
    // 默认不拉大核的最低频率
    if (soc->clusters_.size() > 1)
        boost_freq[1] = soc->clusters_[1].model_.min_freq;
    duration_quantum = 100;
}

template <typename GovernorT, typename SchedT>
void InputBoost<GovernorT, SchedT>::DoBoost() {
    auto cls   = this->env_.soc->clusters_;
    int  nr_cl = cls.size();
    for (int i = 0; i < nr_cl; ++i)
        cls[i].SetMinfreq(tunables_.boost_freq[i]);
}

template <typename GovernorT, typename SchedT>
void InputBoost<GovernorT, SchedT>::DoResume() {
    auto cls   = this->env_.soc->clusters_;
    int  nr_cl = cls.size();
    for (int i = 0; i < nr_cl; ++i)
        cls[i].SetMinfreq(cls[i].model_.min_freq);
}

template <typename GovernorT, typename SchedT>
void InputBoost<GovernorT, SchedT>::Tick(bool has_input, bool has_render, int cur_quantum) {
    if (has_input && tunables_.duration_quantum) {
        this->input_happened_quantum_ = cur_quantum;
        DoBoost();
        this->is_in_boost_ = true;
        return;
    }
    if (this->is_in_boost_ && cur_quantum - this->input_happened_quantum_ > tunables_.duration_quantum) {
        DoResume();
        this->is_in_boost_ = false;
    }
};

template class InputBoost<Interactive, WaltHmp>;
template class InputBoost<Interactive, PeltHmp>;
