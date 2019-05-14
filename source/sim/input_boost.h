#ifndef __INPUT_BOOST_H
#define __INPUT_BOOST_H

#include "cpumodel.h"

class InputBoost {
public:
    typedef struct _Tunables {
        int boost_freq[2];
        int duration_quantum;
        _Tunables() : boost_freq{0, 0}, duration_quantum(0) {}
    } Tunables;

    InputBoost() : tunables_(), input_happened_quantum_(0), is_in_boost_(false) {}
    InputBoost(const Tunables &tunables) : tunables_(tunables), input_happened_quantum_(0), is_in_boost_(false) {}

    void HandleInput(Soc &soc, int has_input, int cur_quantum) {
        int cluster_num = soc.clusters_.size();
        if (has_input && tunables_.duration_quantum) {
            for (int i = 0; i < cluster_num; ++i)
                soc.clusters_[i].SetMinfreq(tunables_.boost_freq[i]);
            input_happened_quantum_ = cur_quantum;
            is_in_boost_            = true;
            return;
        }

        if (is_in_boost_ && cur_quantum - input_happened_quantum_ > tunables_.duration_quantum) {
            for (int i = 0; i < cluster_num; ++i)
                soc.clusters_[i].SetMinfreq(soc.clusters_[i].model_.min_freq);
            is_in_boost_ = false;
        }
        return;
    };

private:
    Tunables tunables_;
    int      input_happened_quantum_;
    bool     is_in_boost_;
};

#endif