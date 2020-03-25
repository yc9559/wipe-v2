#ifndef __INPUT_BOOST_H
#define __INPUT_BOOST_H

#include "cpumodel.h"
#include "interactive.h"

class InputBoost {
public:
    typedef struct _Tunables {
        int duration_quantum;
        _Tunables() : duration_quantum(0) {}
    } Tunables;

    InputBoost() : tunables_(), input_happened_quantum_(0), is_in_boost_(false) {}
    InputBoost(const Tunables &tunables) : tunables_(tunables), input_happened_quantum_(0), is_in_boost_(false) {}

    template <typename SchedT>
    void DoBoost(Soc &soc, Interactive &little, Interactive &big, SchedT &sched) {}

    template <typename SchedT>
    void DoResume(Soc &soc, Interactive &little, Interactive &big, SchedT &sched) {}

    template <typename SchedT>
    void HandleInput(Soc &soc, Interactive &little, Interactive &big, SchedT &sched, int has_input, int cur_quantum) {
        if (has_input && tunables_.duration_quantum) {
            DoBoost(soc, little, big, sched);
            input_happened_quantum_ = cur_quantum;
            is_in_boost_            = true;
            return;
        }
        if (is_in_boost_ && cur_quantum - input_happened_quantum_ > tunables_.duration_quantum) {
            DoResume(soc, little, big, sched);
            is_in_boost_ = false;
        }
        return;
    };

private:
    Tunables tunables_;
    int      input_happened_quantum_;
    bool     is_in_boost_;
};

class TouchBoost : public InputBoost {
public:
    typedef struct _Tunables : public InputBoost::Tunables {
        int boost_freq[2];
        _Tunables() : boost_freq{0, 0} {}
    } Tunables;

    TouchBoost() : tunables_() {}
    TouchBoost(const Tunables &tunables) : tunables_(tunables) {}

    template <typename SchedT>
    void DoBoost(Soc &soc, Interactive &little, Interactive &big, SchedT &sched);

    template <typename SchedT>
    void DoResume(Soc &soc, Interactive &little, Interactive &big, SchedT &sched);

private:
    Tunables tunables_;
};

#endif