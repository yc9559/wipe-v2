#include "openga_helper.h"

#include <algorithm>
#include <fstream>
#include <functional>

#include "interactive.h"
#include "json.hpp"

template <typename SimType>
OpengaAdapter<SimType>::OpengaAdapter(Soc *soc, const Workload *workload, const Workload *idleload,
                                      const std::string &ga_cfg_file)
    : soc_(soc), workload_(workload), idleload_(idleload) {
    ParseCfgFile(ga_cfg_file);
    InitDefaultScore();
};

template <typename SimType>
void OpengaAdapter<SimType>::ParseCfgFile(const std::string &ga_cfg_file) {
    nlohmann::json j;
    {
        std::ifstream ifs(ga_cfg_file);
        if (!ifs.good()) {
            using namespace std;
            cout << "GA config file access ERROR: " << ga_cfg_file << endl;
            throw std::runtime_error("file access error");
        }
        ifs >> j;
    }

    // 解析NSGA3相关参数
    auto p                     = j["gaParameter"];
    ga_cfg_.population         = p["population"];
    ga_cfg_.generation_max     = p["generationMax"];
    ga_cfg_.crossover_fraction = p["crossoverFraction"];
    ga_cfg_.mutation_rate      = p["mutationRate"];
    ga_cfg_.eta                = p["eta"];
    ga_cfg_.thread_num         = p["threadNum"];
    ga_cfg_.random_seed        = p["randomSeed"];

    // 解析结果的分数限制和可调占比
    auto misc              = j["miscSettings"];
    misc_.idle_fraction    = misc["ga.cost.batteryScore.idleFraction"];
    misc_.work_fraction    = misc["ga.cost.batteryScore.workFraction"];
    misc_.idle_lasting_min = misc["ga.cost.limit.idleLastingMin"];
    misc_.performance_max  = misc["ga.cost.limit.performanceMax"];

    sim_misc_.working_base_mw = misc["sim.power.workingBase_mw"];
    sim_misc_.idle_base_mw    = misc["sim.power.idleBase_mw"];

    rank_misc_.common_fraction     = misc["eval.perf.commonFraction"];
    rank_misc_.render_fraction     = misc["eval.perf.renderFraction"];
    rank_misc_.perf_partition_len  = misc["eval.perf.partitionLen"];
    rank_misc_.batt_partition_len  = misc["eval.power.partitionLen"];
    rank_misc_.seq_lag_l1          = misc["eval.perf.seqLagL1"];
    rank_misc_.seq_lag_l2          = misc["eval.perf.seqLagL2"];
    rank_misc_.seq_lag_max         = misc["eval.perf.seqLagMax"];
    rank_misc_.complexity_fraction = misc["eval.complexityFraction"];

    // 解析参数搜索空间范围
    ParamDescCfg desc_cfg;

    auto get_range = [j](const std::string &key) {
        ParamDescElement el;
        el.range_start = j["parameterRange"][key]["min"];
        el.range_end   = j["parameterRange"][key]["max"];
        return el;
    };

    desc_cfg.above_hispeed_delay       = get_range("above_hispeed_delay");
    desc_cfg.go_hispeed_load           = get_range("go_hispeed_load");
    desc_cfg.max_freq_hysteresis       = get_range("max_freq_hysteresis");
    desc_cfg.min_sample_time           = get_range("min_sample_time");
    desc_cfg.target_loads              = get_range("target_loads");
    desc_cfg.sched_downmigrate         = get_range("sched_downmigrate");
    desc_cfg.sched_upmigrate           = get_range("sched_upmigrate");
    desc_cfg.sched_ravg_hist_size      = get_range("sched_ravg_hist_size");
    desc_cfg.sched_window_stats_policy = get_range("sched_window_stats_policy");
    desc_cfg.sched_boost               = get_range("sched_boost");
    desc_cfg.timer_rate                = get_range("timer_rate");
    desc_cfg.input_duration            = get_range("input_duration");
    desc_cfg.load_avg_period_ms        = get_range("load_avg_period_ms");
    desc_cfg.down_threshold            = get_range("down_threshold");
    desc_cfg.up_threshold              = get_range("up_threshold");
    desc_cfg.boost                     = get_range("boost");

    InitParamDesc(desc_cfg);
}

template <typename SimType>
void OpengaAdapter<SimType>::InitParamSeq(ParamSeq &p, const RandomFunc &rnd01) {
    p.reserve(param_len_);
    for (int i = 0; i < param_len_; ++i) {
        p.push_back(rnd01());
    }
}

// mutPolynomialBounded
// Polynomial mutation as implemented in original NSGA-II algorithm in C by Deb.
template <typename SimType>
ParamSeq OpengaAdapter<SimType>::Mutate(const ParamSeq &X_base, const RandomFunc &rnd01, double shrink_scale) {
    // 假设X1，X2等长
    const int    size    = X_base.size();
    const double eta     = ga_cfg_.eta;
    const double eta_1   = eta + 1.0;
    const double mut_pow = 1.0 / eta_1;
    ParamSeq     ret(size);

    for (int idx = 0; idx < size; ++idx) {
        if (rnd01() >= 0.5) {
            ret[idx] = X_base[idx];
            continue;
        }

        const double delta_1 = X_base[idx];
        const double delta_2 = 1.0 - X_base[idx];
        const double rnd     = rnd01();
        double       delta_q, val;

        if (rnd < 0.5) {
            val     = 2.0 * rnd + (1.0 - 2.0 * rnd) * std::pow(1.0 - delta_1, eta_1);
            delta_q = std::pow(val, mut_pow) - 1.0;
        } else {
            val     = 2.0 * (1.0 - rnd) + 2.0 * (rnd - 0.5) * std::pow(1.0 - delta_2, eta_1);
            delta_q = 1.0 - std::pow(val, mut_pow);
        }

        ret[idx] = std::min(std::max(X_base[idx] + delta_q, 0.0), 1.0);
    }
    return ret;
}

// cxSimulatedBinaryBounded
// Executes a simulated binary crossover that modify in-place the input individuals. The simulated binary crossover
// expects :term:`sequence` individuals of floating point numbers
template <typename SimType>
ParamSeq OpengaAdapter<SimType>::Crossover(const ParamSeq &X1, const ParamSeq &X2, const RandomFunc &rnd01) {
    // 假设X1，X2等长
    const int    size  = X1.size();
    const double eta   = ga_cfg_.eta;
    const double eta_1 = eta + 1.0;
    ParamSeq     ret(size);

    for (int idx = 0; idx < size; ++idx) {
        if (rnd01() >= 0.5) {
            ret[idx] = X1[idx];
            continue;
        }
        if (fabs(X1[idx] - X2[idx]) < 0.0) {
            ret[idx] = X2[idx];
            continue;
        }

        const double x1    = std::min(X1[idx], X2[idx]);
        const double x2    = std::max(X1[idx], X2[idx]);
        const double rnd   = rnd01();
        const double x2_x1 = x2 - x1;
        double       beta_q;

        double beta  = 1.0 + (2.0 * x1 / x2_x1);
        double alpha = 2.0 - std::pow(beta, -eta_1);
        if (rnd <= 1.0 / alpha) {
            beta_q = std::pow(rnd * alpha, 1.0 / eta_1);
        } else {
            beta_q = std::pow(1.0 / (2.0 - rnd * alpha), 1.0 / eta_1);
        }

        double c1 = 0.5 * (x1 + x2 - beta_q * x2_x1);

        beta  = 1.0 + (2.0 * (1.0 - x2) / x2_x1);
        alpha = 2.0 - std::pow(beta, -eta_1);
        if (rnd <= 1.0 / alpha) {
            beta_q = std::pow(rnd * alpha, 1.0 / eta_1);
        } else {
            beta_q = std::pow(1.0 / (2.0 - rnd * alpha), 1.0 / eta_1);
        }

        double c2 = 0.5 * (x1 + x2 + beta_q * x2_x1);

        c1 = std::min(std::max(c1, 0.0), 1.0);
        c2 = std::min(std::max(c2, 0.0), 1.0);

        if (rnd01() <= 0.5) {
            ret[idx] = c2;
        } else {
            ret[idx] = c1;
        }
    }
    return ret;
}

template <typename SimType>
void OpengaAdapter<SimType>::MO_report_generation(int                                             generation_number,
                                                  const EA::GenerationType<ParamSeq, MiddleCost> &last_generation,
                                                  const std::vector<unsigned int> &               pareto_front) {
    return;
}

template <typename SimType>
bool OpengaAdapter<SimType>::EvalParamSeq(const ParamSeq &param_seq, MiddleCost &result) {
    typename SimType::Tunables t = TranslateParamSeq(param_seq);

    SimResultPack rp;
    rp.onscreen.capacity.reserve(workload_->windowed_load_.size());
    rp.onscreen.power.reserve(workload_->windowed_load_.size());

    SimType sim(t, sim_misc_);
    sim.Run(*workload_, *idleload_, *soc_, &rp);
    Rank        rank(default_score_, rank_misc_);
    Rank::Score score = rank.Eval(*workload_, *idleload_, rp, *soc_, false);

    result.c1 = score.performance;
    result.c2 = score.battery_life;
    result.c3 = score.idle_lasting;

    bool pass = (score.idle_lasting > misc_.idle_lasting_min) && (score.performance < misc_.performance_max);
    return pass;
}

template <typename SimType>
void OpengaAdapter<SimType>::InitDefaultScore() {
    typename SimType::Tunables t = GenerateDefaultTunables();
    Rank::Score                s = {1.0, 1.0, 1.0};

    SimResultPack rp;
    rp.onscreen.capacity.reserve(workload_->windowed_load_.size());
    rp.onscreen.power.reserve(workload_->windowed_load_.size());

    SimType sim(t, sim_misc_);
    sim.Run(*workload_, *idleload_, *soc_, &rp);
    Rank rank(s, rank_misc_);
    default_score_ = rank.Eval(*workload_, *idleload_, rp, *soc_, true);
}

template <typename SimType>
std::vector<typename OpengaAdapter<SimType>::Result> OpengaAdapter<SimType>::Optimize(void) {
    using namespace std::placeholders;
    EA::Chronometer timer;
    timer.tic();

    GA_Type ga_obj(ga_cfg_.random_seed);
    ga_obj.problem_mode            = EA::GA_MODE::NSGA_III;
    ga_obj.verbose                 = false;
    ga_obj.population              = ga_cfg_.population;
    ga_obj.generation_max          = ga_cfg_.generation_max;
    ga_obj.calculate_MO_objectives = std::bind(&OpengaAdapter<SimType>::CalcMultiObjectives, this, _1);
    ga_obj.init_genes              = std::bind(&OpengaAdapter<SimType>::InitParamSeq, this, _1, _2);
    ga_obj.eval_solution           = std::bind(&OpengaAdapter<SimType>::EvalParamSeq, this, _1, _2);
    ga_obj.mutate                  = std::bind(&OpengaAdapter<SimType>::Mutate, this, _1, _2, _3);
    ga_obj.crossover               = std::bind(&OpengaAdapter<SimType>::Crossover, this, _1, _2, _3);
    ga_obj.MO_report_generation    = std::bind(&OpengaAdapter<SimType>::MO_report_generation, this, _1, _2, _3);
    ga_obj.crossover_fraction      = ga_cfg_.crossover_fraction;
    ga_obj.mutation_rate           = ga_cfg_.mutation_rate;
    ga_obj.dynamic_threading       = false;
    ga_obj.multi_threading         = false;
    ga_obj.N_threads               = ga_cfg_.thread_num;
    ga_obj.idle_delay_us           = 1;  // switch between threads quickly

    if (ga_cfg_.thread_num > 1) {
        ga_obj.multi_threading   = true;
        ga_obj.dynamic_threading = true;
    }

    std::cout << "\nTarget: " << soc_->name_ << std::endl;

    ga_obj.solve();

    std::cout << "\nOptimized in " << timer.toc() << " seconds." << std::endl;

    std::vector<Result> ret;
    ret.reserve(ga_obj.last_generation.fronts[0].size());
    auto paretofront_indices = ga_obj.last_generation.fronts[0];
    for (const auto &i : paretofront_indices) {
        Result      r;
        const auto &chromosome = ga_obj.last_generation.chromosomes[i];
        r.tunable              = TranslateParamSeq(chromosome.genes);
        r.score.performance    = chromosome.middle_costs.c1;
        r.score.battery_life   = chromosome.middle_costs.c2;
        r.score.idle_lasting   = chromosome.middle_costs.c3;
        ret.push_back(r);
    }

    return ret;
}

int Quantify(double ratio, const ParamDescElement &desc) {
    return (desc.range_start + std::round((desc.range_end - desc.range_start) * ratio));
}

int QuatFreqParam(double ratio, const Cluster &cluster, const ParamDescElement &desc) {
    return cluster.freq_floor_to_opp(Quantify(ratio, desc));
}

int QuatLoadParam(double ratio, const ParamDescElement &desc) {
    int target_load = Quantify(ratio, desc);
    // 减少targetload没必要的参数档位，降低参数复杂度
    // if (target_load > 15 && target_load < 85) {
    //     target_load = target_load >> 2 << 2;
    // }
    return target_load;
}

int QuatLargeParam(double ratio, int step, const ParamDescElement &desc) {
    return (Quantify(ratio, desc) / step) * step;
}

template <typename T>
void DefineBlock(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    return;
}

template <typename T>
T TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc, const Soc *soc) {
    return T();
}

template <>
void DefineBlock<GovernorTs<Interactive>>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    for (const auto &cluster : soc->clusters_) {
        ParamDescElement hispeed_freq_desc = {cluster.model_.min_freq, cluster.model_.max_freq};
        desc.push_back(hispeed_freq_desc);
        desc.push_back(p.go_hispeed_load);
        desc.push_back(p.min_sample_time);
        desc.push_back(p.max_freq_hysteresis);

        int n_opp         = cluster.model_.opp_model.size();
        int n_above       = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
        int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);

        for (int i = 0; i < n_above; ++i) {
            desc.push_back(p.above_hispeed_delay);
        }
        for (int i = 0; i < n_targetloads; ++i) {
            desc.push_back(p.target_loads);
        }
    }
}

template <>
GovernorTs<Interactive> TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc,
                                       const Soc *soc) {
    GovernorTs<Interactive> t;

    int idx = 0;
    for (const auto &cluster : soc->clusters_) {
        t.t[idx].hispeed_freq        = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.t[idx].go_hispeed_load     = QuatLoadParam(*it_seq++, *it_desc++);
        t.t[idx].min_sample_time     = Quantify(*it_seq++, *it_desc++);
        t.t[idx].max_freq_hysteresis = Quantify(*it_seq++, *it_desc++);

        int n_opp         = cluster.model_.opp_model.size();
        int n_above       = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
        int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);

        for (int i = 0; i < n_above; ++i) {
            t.t[idx].above_hispeed_delay[i] = Quantify(*it_seq++, *it_desc++);
        }
        for (int i = 0; i < n_targetloads; ++i) {
            t.t[idx].target_loads[i] = QuatLoadParam(*it_seq++, *it_desc++);
        }
        idx++;
    }

    // 时长类参数取整到一个timer_rate
    idx = 0;
    for (const auto &cluster : soc->clusters_) {
        auto & tunable       = t.t[idx];
        double timer_quantum = 2;  // timer_rate 固定为20ms

        tunable.min_sample_time     = std::max(1.0, std::round(tunable.min_sample_time / timer_quantum));
        tunable.max_freq_hysteresis = std::max(1.0, std::round(tunable.max_freq_hysteresis / timer_quantum));

        int n_opp   = cluster.model_.opp_model.size();
        int n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);

        for (int i = 0; i < n_above; ++i) {
            tunable.above_hispeed_delay[i] = std::max(1.0, std::round(tunable.above_hispeed_delay[i] / timer_quantum));
        }
        idx++;
    }

    return std::move(t);
}

template <>
void DefineBlock<WaltHmp::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    desc.push_back(p.sched_downmigrate);
    desc.push_back(p.sched_upmigrate);
    desc.push_back(p.sched_ravg_hist_size);
    desc.push_back(p.sched_window_stats_policy);
    desc.push_back(p.sched_boost);
    desc.push_back(p.timer_rate);
}

template <>
WaltHmp::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc, const Soc *soc) {
    WaltHmp::Tunables t;
    t.sched_downmigrate         = QuatLoadParam(*it_seq++, *it_desc++);
    t.sched_upmigrate           = QuatLoadParam(*it_seq++, *it_desc++);
    t.sched_upmigrate           = std::max(t.sched_downmigrate, t.sched_upmigrate);
    t.sched_ravg_hist_size      = Quantify(*it_seq++, *it_desc++);
    t.sched_window_stats_policy = Quantify(*it_seq++, *it_desc++);
    t.sched_boost               = Quantify(*it_seq++, *it_desc++);
    t.timer_rate                = Quantify(*it_seq++, *it_desc++);
    return std::move(t);
}

template <>
void DefineBlock<PeltHmp::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    desc.push_back(p.down_threshold);
    desc.push_back(p.up_threshold);
    desc.push_back(p.load_avg_period_ms);
    desc.push_back(p.boost);
    desc.push_back(p.timer_rate);
}

template <>
PeltHmp::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc, const Soc *soc) {
    PeltHmp::Tunables t;
    t.down_threshold     = Quantify(*it_seq++, *it_desc++);
    t.up_threshold       = Quantify(*it_seq++, *it_desc++);
    t.up_threshold       = std::max(t.down_threshold, t.up_threshold);
    t.load_avg_period_ms = Quantify(*it_seq++, *it_desc++);
    t.boost              = Quantify(*it_seq++, *it_desc++);
    t.timer_rate         = Quantify(*it_seq++, *it_desc++);
    return std::move(t);
}

template <>
void DefineBlock<InputBoostWalt::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    for (const auto &cluster : soc->clusters_) {
        ParamDescElement input_freq = {cluster.model_.min_freq, cluster.model_.max_freq};
        desc.push_back(input_freq);
    }
    desc.push_back(p.input_duration);
}

template <>
InputBoostWalt::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc,
                                        const Soc *soc) {
    InputBoostWalt::Tunables t;

    int idx = 0;
    for (const auto &cluster : soc->clusters_)
        t.boost_freq[idx++] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
    t.duration_quantum = QuatLargeParam(*it_seq++, 10, *it_desc++);

    return std::move(t);
}

template <>
void DefineBlock<InputBoostPelt::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    for (const auto &cluster : soc->clusters_) {
        ParamDescElement input_freq = {cluster.model_.min_freq, cluster.model_.max_freq};
        desc.push_back(input_freq);
    }
    desc.push_back(p.input_duration);
}

template <>
InputBoostPelt::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc,
                                        const Soc *soc) {
    InputBoostPelt::Tunables t;

    int idx = 0;
    for (const auto &cluster : soc->clusters_)
        t.boost_freq[idx++] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
    t.duration_quantum = QuatLargeParam(*it_seq++, 10, *it_desc++);

    return std::move(t);
}

template <>
void DefineBlock<UperfBoostWalt::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    for (const auto &cluster : soc->clusters_) {
        ParamDescElement freq = {cluster.model_.min_freq, cluster.model_.max_freq};
        desc.push_back(freq);
        desc.push_back(freq);
    }
    desc.push_back(p.sched_downmigrate);
    desc.push_back(p.sched_upmigrate);
    DefineBlock<GovernorTs<Interactive>>(desc, p, soc);
}

template <>
UperfBoostWalt::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc,
                                        const Soc *soc) {
    UperfBoostWalt::Tunables t;

    int idx = 0;
    for (const auto &cluster : soc->clusters_) {
        t.min_freq[idx] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.max_freq[idx] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.max_freq[idx] = std::max(t.min_freq[idx], t.max_freq[idx]);
        ++idx;
    }
    t.sched_down = Quantify(*it_seq++, *it_desc++);
    t.sched_up   = Quantify(*it_seq++, *it_desc++);
    t.sched_up   = std::max(t.sched_down, t.sched_up);
    auto iblk    = TranslateBlock<GovernorTs<Interactive>>(it_seq, it_desc, soc);
    t.little     = iblk.t[0];
    t.big        = iblk.t[1];

    return std::move(t);
}

template <>
void DefineBlock<UperfBoostPelt::Tunables>(ParamDesc &desc, const ParamDescCfg &p, const Soc *soc) {
    for (const auto &cluster : soc->clusters_) {
        ParamDescElement freq = {cluster.model_.min_freq, cluster.model_.max_freq};
        desc.push_back(freq);
        desc.push_back(freq);
    }
    desc.push_back(p.down_threshold);
    desc.push_back(p.up_threshold);
    DefineBlock<GovernorTs<Interactive>>(desc, p, soc);
}

template <>
UperfBoostPelt::Tunables TranslateBlock(ParamSeq::const_iterator &it_seq, ParamDesc::const_iterator &it_desc,
                                        const Soc *soc) {
    UperfBoostPelt::Tunables t;

    int idx = 0;
    for (const auto &cluster : soc->clusters_) {
        t.min_freq[idx] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.max_freq[idx] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.max_freq[idx] = std::max(t.min_freq[idx], t.max_freq[idx]);
        ++idx;
    }
    t.sched_down = Quantify(*it_seq++, *it_desc++);
    t.sched_up   = Quantify(*it_seq++, *it_desc++);
    t.sched_up   = std::max(t.sched_down, t.sched_up);
    auto iblk    = TranslateBlock<GovernorTs<Interactive>>(it_seq, it_desc, soc);
    t.little     = iblk.t[0];
    t.big        = iblk.t[1];

    return std::move(t);
}

template <typename SimType>
typename SimType::Tunables OpengaAdapter<SimType>::TranslateParamSeq(const ParamSeq &p) const {
    typename SimType::Tunables t;

    ParamSeq::const_iterator  it_seq  = p.begin();
    ParamDesc::const_iterator it_desc = param_desc_.begin();
    // cpufreq调速器参数上下限
    t.governor = TranslateBlock<GovernorTs<typename SimType::Governor>>(it_seq, it_desc, soc_);
    // sched任务调度器参数上下限
    t.sched = TranslateBlock<typename SimType::Sched::Tunables>(it_seq, it_desc, soc_);
    // boost升频参数上下限
    t.boost = TranslateBlock<typename SimType::Boost::Tunables>(it_seq, it_desc, soc_);

    return t;
}

template <typename SimType>
void OpengaAdapter<SimType>::InitParamDesc(const ParamDescCfg &p) {
    // cpufreq调速器参数上下限
    DefineBlock<GovernorTs<typename SimType::Governor>>(param_desc_, p, soc_);
    // sched任务调度器参数上下限
    DefineBlock<typename SimType::Sched::Tunables>(param_desc_, p, soc_);
    // boost升频参数上下限
    DefineBlock<typename SimType::Boost::Tunables>(param_desc_, p, soc_);
    param_len_ = param_desc_.size();
}

template <typename SimType>
typename SimType::Tunables OpengaAdapter<SimType>::GenerateDefaultTunables(void) const {
    typename SimType::Tunables t;
    // cpufreq调速器参数上下限
    int idx = 0;
    for (const auto &cluster : soc_->clusters_)
        t.governor.t[idx++] = typename SimType::Governor::Tunables(cluster);
    // sched任务调度器参数上下限
    t.sched = typename SimType::Sched::Tunables();
    // boost升频参数上下限
    t.boost = typename SimType::Boost::Tunables(soc_);
    return t;
}

template class OpengaAdapter<SimQcomBL>;
template class OpengaAdapter<SimBL>;
template class OpengaAdapter<SimQcomUp>;
template class OpengaAdapter<SimUp>;
