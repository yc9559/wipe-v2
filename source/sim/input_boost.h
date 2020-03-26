#ifndef __INPUT_BOOST_H
#define __INPUT_BOOST_H

#include "cpumodel.h"

template <typename GovernorT, typename SchedT>
class Boost {
public:
    struct SysEnv {
        Soc *      soc;
        GovernorT *little;
        GovernorT *big;
        SchedT *   sched;
    };

    Boost() : env_(), is_in_boost_(false) {}
    Boost(const SysEnv &env) : env_(env), is_in_boost_(false) {}
    void Tick(bool has_input, bool has_render, int cur_quantum) {}

protected:
    void DoBoost(void) {}
    void DoResume(void) {}

    SysEnv env_;
    bool   is_in_boost_;
};

template <typename GovernorT, typename SchedT>
class InputBoost : public Boost<GovernorT, SchedT> {
public:
    struct Tunables {
        int boost_freq[2];
        int duration_quantum;
        Tunables() : boost_freq{0, 0}, duration_quantum(0) {}
        Tunables(const Soc *soc);
    };

    InputBoost() : Boost<GovernorT, SchedT>(), tunables_(), input_happened_quantum_(0) {}
    InputBoost(const Tunables &tunables, const typename Boost<GovernorT, SchedT>::SysEnv &env)
        : Boost<GovernorT, SchedT>(env), tunables_(tunables), input_happened_quantum_(0) {}
    void Tick(bool has_input, bool has_render, int cur_quantum);

private:
    void DoBoost(void);
    void DoResume(void);

    Tunables tunables_;
    int      input_happened_quantum_;
};

#endif