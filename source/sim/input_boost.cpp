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
    auto &cls = this->env_.soc->clusters_;
    int   nr  = cls.size();
    for (int i = 0; i < nr; ++i)
        cls[i].SetMinfreq(tunables_.boost_freq[i]);
}

template <typename GovernorT, typename SchedT>
void InputBoost<GovernorT, SchedT>::DoResume() {
    auto &cls = this->env_.soc->clusters_;
    int   nr  = cls.size();
    for (int i = 0; i < nr; ++i)
        cls[i].SetMinfreq(cls[i].model_.min_freq);
}

template <typename GovernorT, typename SchedT>
void InputBoost<GovernorT, SchedT>::Tick(bool has_input, bool has_render, int cur_quantum) {
    if (tunables_.duration_quantum && has_input) {
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

template <>
UperfBoost<Interactive, WaltHmp>::Tunables::Tunables(const Soc *soc) {
    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = WaltHmp::Tunables();
    for (int i = 0; i < cluster_num; ++i) {
        const auto &cl = soc->clusters_[i];
        min_freq[i]    = cl.freq_floor_to_opp(cl.model_.max_freq * 0.6);
        max_freq[i]    = cl.model_.max_freq;
    }
    sched_up   = sched_tunables.sched_upmigrate;
    sched_down = sched_tunables.sched_downmigrate;
    little     = Interactive::Tunables(soc->clusters_[soc->GetLittleClusterIdx()]);
    big        = Interactive::Tunables(soc->clusters_[soc->GetBigClusterIdx()]);
    enabled    = true;
}

template <>
UperfBoost<Interactive, PeltHmp>::Tunables::Tunables(const Soc *soc) {
    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = PeltHmp::Tunables();
    for (int i = 0; i < cluster_num; ++i) {
        const auto &cl = soc->clusters_[i];
        min_freq[i]    = cl.freq_floor_to_opp(cl.model_.max_freq * 0.6);
        max_freq[i]    = cl.model_.max_freq;
    }
    sched_up   = sched_tunables.up_threshold;
    sched_down = sched_tunables.down_threshold;
    little     = Interactive::Tunables(soc->clusters_[soc->GetLittleClusterIdx()]);
    big        = Interactive::Tunables(soc->clusters_[soc->GetBigClusterIdx()]);
    enabled    = true;
}

template <typename GovernorT, typename SchedT>
void UperfBoost<GovernorT, SchedT>::DoBoost() {
    if (is_original_inited_ == false) {
        is_original_inited_ = true;
        Backup();
    }
    Apply(tunables_);
}

template <typename GovernorT, typename SchedT>
void UperfBoost<GovernorT, SchedT>::DoResume() {
    Apply(original_);
}

template <typename GovernorT, typename SchedT>
void UperfBoost<GovernorT, SchedT>::Tick(bool has_input, bool has_render, int cur_quantum) {
    if (tunables_.enabled == false)
        return;

    if (has_render) {
        this->render_stop_quantum_ = cur_quantum;
    }
    if (!this->is_in_boost_ && has_input) {
        this->render_stop_quantum_ = cur_quantum;
        DoBoost();
        this->is_in_boost_ = true;
        return;
    }
    // uperf在渲染结束后200ms停止hint
    if (this->is_in_boost_ && cur_quantum - this->render_stop_quantum_ > 20) {
        DoResume();
        this->is_in_boost_ = false;
    }
};

template <>
void UperfBoost<Interactive, WaltHmp>::Apply(const typename UperfBoost<Interactive, WaltHmp>::Tunables &t) {
    auto soc    = this->env_.soc;
    auto little = this->env_.little;
    auto big    = this->env_.big;
    auto sched  = this->env_.sched;

    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = sched->GetTunables();
    for (int i = 0; i < cluster_num; ++i) {
        soc->clusters_[i].SetMinfreq(t.min_freq[i]);
        soc->clusters_[i].SetMaxfreq(t.max_freq[i]);
    }
    sched_tunables.sched_upmigrate   = t.sched_up;
    sched_tunables.sched_downmigrate = t.sched_down;
    sched->SetTunables(sched_tunables);
    little->SetTunables(t.little);
    big->SetTunables(t.big);
}

template <>
void UperfBoost<Interactive, PeltHmp>::Apply(const typename UperfBoost<Interactive, PeltHmp>::Tunables &t) {
    auto soc    = this->env_.soc;
    auto little = this->env_.little;
    auto big    = this->env_.big;
    auto sched  = this->env_.sched;

    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = sched->GetTunables();
    for (int i = 0; i < cluster_num; ++i) {
        soc->clusters_[i].SetMinfreq(t.min_freq[i]);
        soc->clusters_[i].SetMaxfreq(t.max_freq[i]);
    }
    sched_tunables.up_threshold   = t.sched_up;
    sched_tunables.down_threshold = t.sched_down;
    sched->SetTunables(sched_tunables);
    little->SetTunables(t.little);
    big->SetTunables(t.big);
}

template <>
void UperfBoost<Interactive, WaltHmp>::Backup() {
    const auto soc    = this->env_.soc;
    const auto little = this->env_.little;
    const auto big    = this->env_.big;
    const auto sched  = this->env_.sched;

    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = sched->GetTunables();
    for (int i = 0; i < cluster_num; ++i) {
        original_.min_freq[i] = soc->clusters_[i].model_.min_freq;
        original_.max_freq[i] = soc->clusters_[i].model_.max_freq;
    }
    original_.sched_up   = sched_tunables.sched_upmigrate;
    original_.sched_down = sched_tunables.sched_downmigrate;
    original_.little     = little->GetTunables();
    original_.big        = big->GetTunables();
}

template <>
void UperfBoost<Interactive, PeltHmp>::Backup() {
    const auto soc    = this->env_.soc;
    const auto little = this->env_.little;
    const auto big    = this->env_.big;
    const auto sched  = this->env_.sched;

    int  cluster_num    = soc->clusters_.size();
    auto sched_tunables = sched->GetTunables();
    for (int i = 0; i < cluster_num; ++i) {
        original_.min_freq[i] = soc->clusters_[i].model_.min_freq;
        original_.max_freq[i] = soc->clusters_[i].model_.max_freq;
    }
    original_.sched_up   = sched_tunables.up_threshold;
    original_.sched_down = sched_tunables.down_threshold;
    original_.little     = little->GetTunables();
    original_.big        = big->GetTunables();
}

template class InputBoost<Interactive, WaltHmp>;
template class InputBoost<Interactive, PeltHmp>;
template class UperfBoost<Interactive, WaltHmp>;
template class UperfBoost<Interactive, PeltHmp>;
