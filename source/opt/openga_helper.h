#ifndef __OPENGA_HELPER_H
#define __OPENGA_HELPER_H

#include <string>
#include <vector>
#include "cpumodel.h"
#include "hmp_pelt.h"
#include "hmp_walt.h"
#include "input_boost.h"
#include "interactive.h"
#include "openga.hpp"
#include "rank.h"
#include "sim.hpp"
#include "sim_types.h"
#include "workload.h"

typedef Sim<Interactive, WaltHmp, InputBoost> SimQcomBL;
typedef Sim<Interactive, PeltHmp, InputBoost> SimBL;

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
    ParamDescElement sched_boost;
    ParamDescElement timer_rate;
    ParamDescElement input_duration;
    ParamDescElement load_avg_period_ms;
    ParamDescElement down_threshold;
    ParamDescElement up_threshold;
    ParamDescElement boost;
} ParamDescCfg;

template <typename SimType>
class OpengaAdapter {
public:
    typedef struct _GaCfg {
        int      population;
        int      generation_max;
        float    crossover_fraction;
        float    mutation_rate;
        float    eta;
        int      thread_num;
        uint64_t random_seed;
    } GaCfg;

    typedef struct _MiscConst {
        double idle_fraction;
        double work_fraction;
        double idle_lasting_min;
        double performance_max;
    } MiscConst;

    typedef struct _MiddleCost {
        double c1;
        double c2;
        double c3;
    } MiddleCost;

    struct Result {
        typename SimType::Tunables tunable;
        Rank::Score                score;
    };

    typedef std::vector<double>               ParamSeq;
    typedef std::vector<ParamDescElement>     ParamDesc;
    typedef EA::Genetic<ParamSeq, MiddleCost> GA_Type;
    typedef std::function<double(void)>       RandomFunc;

    OpengaAdapter(Soc *soc, const Workload *workload, const Workload *idleload, const std::string &ga_cfg_file);
    std::vector<OpengaAdapter::Result> Optimize(void);

private:
    OpengaAdapter();
    std::vector<double> CalcMultiObjectives(const typename GA_Type::thisChromosomeType &X) {
        // result.c1 = score.performance;   // 卡顿程度，越小越好
        // result.c2 = score.battery_life;  // 亮屏续航，越大越好
        // result.c3 = score.idle_lasting   // 灭屏待机，越大越好
        return {X.middle_costs.c1,
                -(misc_.work_fraction * X.middle_costs.c2 + misc_.idle_fraction * X.middle_costs.c3)};
    }

    ParamSeq Mutate(const ParamSeq &X_base, const RandomFunc &rnd01, double shrink_scale);
    ParamSeq Crossover(const ParamSeq &X1, const ParamSeq &X2, const RandomFunc &rnd01);

    typename SimType::Tunables TranslateParamSeq(const ParamSeq &p) const;
    typename SimType::Tunables GenerateDefaultTunables(void) const;
    void                       InitParamDesc(const ParamDescCfg &p);

    void MO_report_generation(int generation_number, const EA::GenerationType<ParamSeq, MiddleCost> &last_generation,
                              const std::vector<unsigned int> &pareto_front);

    void InitParamSeq(ParamSeq &p, const RandomFunc &rnd01);
    bool EvalParamSeq(const ParamSeq &param_seq, MiddleCost &result);
    void InitDefaultScore();
    void InitDefaultPowersum();
    void ParseCfgFile(const std::string &ga_cfg_file);

    Soc *           soc_;
    const Workload *workload_;
    const Workload *idleload_;
    Rank::Score     default_score_;
    int             param_len_;
    ParamDesc       param_desc_;
    GaCfg           ga_cfg_;
    MiscConst       misc_;

    typename SimType::MiscConst sim_misc_;
    Rank::MiscConst             rank_misc_;
};

#endif
