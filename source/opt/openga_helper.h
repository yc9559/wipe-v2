#ifndef __OPENGA_HELPER_H
#define __OPENGA_HELPER_H

#include <string>
#include <vector>
#include "cpumodel.h"
#include "openga.hpp"
#include "sim.h"
#include "workload.h"

class OpengaAdapter {
public:
    typedef struct _ParamDescElement {
        int range_start;
        int range_end;
    } ParamDescElement;

    typedef struct _ParamDescCfg {
        ParamDescElement above_hispeed_delay;
        ParamDescElement go_hispeed_load;
        ParamDescElement max_freq_hysteresis;
        ParamDescElement min_sample_time;
        ParamDescElement target_loads;
        ParamDescElement sched_downmigrate;
        ParamDescElement sched_upmigrate;
        ParamDescElement sched_freq_aggregate_threshold_pct;
        ParamDescElement sched_ravg_hist_size;
        ParamDescElement sched_window_stats_policy;
        ParamDescElement timer_rate;
        ParamDescElement input_duration;
    } ParamDescCfg;

    typedef struct _GaCfg {
        int      population;
        int      generation_max;
        float    crossover_fraction;
        float    mutation_rate;
        float    eta;
        int      thread_num;
        uint64_t random_seed;
    } GaCfg;

    typedef struct _MiddleCost {
        double c1;
        double c2;
    } MiddleCost;

    typedef struct _Result {
        Sim::Tunables tunable;
        Sim::Score    score;
    } Result;

    typedef std::vector<double>               ParamSeq;
    typedef std::vector<ParamDescElement>     ParamDesc;
    typedef EA::Genetic<ParamSeq, MiddleCost> GA_Type;
    typedef std::function<double(void)>       RandomFunc;

    OpengaAdapter(Soc *soc, Workload *workload, const std::string &ga_cfg_file);
    std::vector<OpengaAdapter::Result> Optimize(void);

private:
    OpengaAdapter();
    std::vector<double> CalcMultiObjectives(const GA_Type::thisChromosomeType &X);
    ParamSeq            Mutate(const ParamSeq &X_base, const RandomFunc &rnd01, double shrink_scale);
    ParamSeq            Crossover(const ParamSeq &X1, const ParamSeq &X2, const RandomFunc &rnd01);
    Sim::Tunables       TranslateParamSeq(const ParamSeq &p) const;
    Sim::Tunables       GenerateDefaultTunables(void) const;
    void MO_report_generation(int generation_number, const EA::GenerationType<ParamSeq, MiddleCost> &last_generation,
                              const std::vector<unsigned int> &pareto_front);

    void InitParamSeq(ParamSeq &p, const RandomFunc &rnd01);
    bool EvalParamSeq(const ParamSeq &param_seq, MiddleCost &result);
    void InitDefaultScore();
    void InitParamDesc(const ParamDescCfg &p);
    void InitDefaultPowersum();
    void ParseCfgFile(const std::string &ga_cfg_file);

    Soc *      soc_;
    Workload * workload_;
    Sim::Score default_score_;
    int        param_len_;
    ParamDesc  param_desc_;
    GaCfg      ga_cfg_;
};

inline std::vector<double> OpengaAdapter::CalcMultiObjectives(const GA_Type::thisChromosomeType &X) {
    // result.c1 = score.performance;   // 卡顿程度，越小越好
    // result.c2 = score.battery_life;  // 续航，越大越好
    return {X.middle_costs.c1, -X.middle_costs.c2};
}

#endif
