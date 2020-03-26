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

template <typename GovernorT, typename SchedT>
class UperfBoost : public Boost<GovernorT, SchedT> {
public:
    struct Tunables {
        int                 min_freq[2];
        int                 max_freq[2];
        int                 sched_up;
        int                 sched_down;
        GovernorT::Tunables little;
        GovernorT::Tunables big;
        Tunables() : min_freq{0, 0}, max_freq{0, 0}, sched_up(0), sched_down(0) {}
        Tunables(const Soc *soc);
    };

    UperfBoost()
        : Boost<GovernorT, SchedT>(), tunables_(), original_(), is_original_inited_(false), render_stop_quantum_(0) {}
    UperfBoost(const Tunables &tunables, const typename Boost<GovernorT, SchedT>::SysEnv &env)
        : Boost<GovernorT, SchedT>(env),
          tunables_(tunables),
          original_(),
          is_original_inited_(false),
          render_stop_quantum_(0) {}
    void Tick(bool has_input, bool has_render, int cur_quantum);

private:
    void DoBoost(void);
    void DoResume(void);
    void Apply(const Tunables &t);
    void Backup(void);

    Tunables tunables_;
    Tunables original_;
    bool     is_original_inited_;
    int      render_stop_quantum_;
};

#endif