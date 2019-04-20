#include "openga_helper.h"
#include <algorithm>
#include <functional>
#include <fstream>
#include "json.hpp"
#include "interactive.h"

OpengaAdapter::OpengaAdapter(Soc *soc, const Workload *workload, const Workload *idleload, const std::string &ga_cfg_file)
    : soc_(soc), workload_(workload), idleload_(idleload) {
    ParseCfgFile(ga_cfg_file);
    InitDefaultScore();
};

void OpengaAdapter::ParseCfgFile(const std::string &ga_cfg_file) {
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
    auto p = j["gaParameter"];
    ga_cfg_.population         = p["population"];
    ga_cfg_.generation_max     = p["generationMax"];
    ga_cfg_.crossover_fraction = p["crossoverFraction"];
    ga_cfg_.mutation_rate      = p["mutationRate"];
    ga_cfg_.eta                = p["eta"];
    ga_cfg_.thread_num         = p["threadNum"];
    ga_cfg_.random_seed        = p["randomSeed"];

    // 解析结果的分数限制和可调占比
    auto misc = j["miscSettings"];
    misc_.idle_fraction    = misc["ga.cost.batteryScore.idleFraction"];
    misc_.work_fraction    = misc["ga.cost.batteryScore.workFraction"];
    misc_.idle_lasting_min = misc["ga.cost.limit.idleLastingMin"];
    misc_.performance_max  = misc["ga.cost.limit.performanceMax"];

    sim_misc_.common_fraction     = misc["sim.perf.commonFraction"];
    sim_misc_.render_fraction     = misc["sim.perf.renderFraction"];
    sim_misc_.enough_capacity_pct = misc["sim.perf.enoughCapacityPct"];
    sim_misc_.perf_partition_len  = misc["sim.perf.partitionLen"];
    sim_misc_.batt_partition_len  = misc["sim.power.partitionLen"];
    sim_misc_.seq_lag_l1          = misc["sim.perf.seqLagL1"];
    sim_misc_.seq_lag_l2          = misc["sim.perf.seqLagL2"];
    sim_misc_.seq_lag_max         = misc["sim.perf.seqLagMax"];
    sim_misc_.working_base_mw     = misc["sim.power.workingBase_mw"];
    sim_misc_.idle_base_mw        = misc["sim.power.idleBase_mw"];
    sim_misc_.complexity_fraction = misc["sim.complexityFraction"];

    // 解析参数搜索空间范围
    ParamDescCfg param_desc_cfg;
    auto range = j["parameterRange"];

    auto t = range["above_hispeed_delay"];
    param_desc_cfg.above_hispeed_delay = {t["min"], t["max"]};

    t = range["go_hispeed_load"];
    param_desc_cfg.go_hispeed_load = {t["min"], t["max"]};

    t = range["max_freq_hysteresis"];
    param_desc_cfg.max_freq_hysteresis = {t["min"], t["max"]};

    t = range["min_sample_time"];
    param_desc_cfg.min_sample_time = {t["min"], t["max"]};

    t = range["target_loads"];
    param_desc_cfg.target_loads = {t["min"], t["max"]};

    t = range["sched_downmigrate"];
    param_desc_cfg.sched_downmigrate = {t["min"], t["max"]};

    t = range["sched_upmigrate"];
    param_desc_cfg.sched_upmigrate = {t["min"], t["max"]};

    t = range["sched_freq_aggregate_threshold_pct"];
    param_desc_cfg.sched_freq_aggregate_threshold_pct = {t["min"], t["max"]};

    t = range["sched_ravg_hist_size"];
    param_desc_cfg.sched_ravg_hist_size = {t["min"], t["max"]};

    t = range["sched_window_stats_policy"];
    param_desc_cfg.sched_window_stats_policy = {t["min"], t["max"]};

    t = range["timer_rate"];
    param_desc_cfg.timer_rate = {t["min"], t["max"]};

    t = range["input_duration"];
    param_desc_cfg.input_duration = {t["min"], t["max"]};

    InitParamDesc(param_desc_cfg);
}

void OpengaAdapter::InitParamSeq(ParamSeq &p, const RandomFunc &rnd01) {
    p.reserve(param_len_);
    for (int i = 0; i < param_len_; ++i) {
        p.push_back(rnd01());
    }
}

// mutPolynomialBounded
// Polynomial mutation as implemented in original NSGA-II algorithm in C by Deb.
OpengaAdapter::ParamSeq OpengaAdapter::Mutate(const ParamSeq &X_base, const RandomFunc &rnd01, double shrink_scale) {
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
OpengaAdapter::ParamSeq OpengaAdapter::Crossover(const ParamSeq &X1, const ParamSeq &X2, const RandomFunc &rnd01) {
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

void OpengaAdapter::MO_report_generation(int                                             generation_number,
                                         const EA::GenerationType<ParamSeq, MiddleCost> &last_generation,
                                         const std::vector<unsigned int> &               pareto_front) {
    return;
}

bool OpengaAdapter::EvalParamSeq(const ParamSeq &param_seq, MiddleCost &result) {
    Sim::Tunables t = TranslateParamSeq(param_seq);

    Sim        sim(t, default_score_, sim_misc_);
    Sim::Score score = sim.Run(*workload_, *idleload_, *soc_, false);

    result.c1 = score.performance;
    result.c2 = score.battery_life;
    result.c3 = score.idle_lasting;

    bool pass = (score.idle_lasting > misc_.idle_lasting_min) && (score.performance < misc_.performance_max);
    return pass;
}

inline int Quantify(double ratio, const OpengaAdapter::ParamDescElement &desc) {
    return (desc.range_start + std::round((desc.range_end - desc.range_start) * ratio));
}

inline int QuatFreqParam(double ratio, const Cluster &cluster, const OpengaAdapter::ParamDescElement &desc) {
    return cluster.freq_floor_to_opp(Quantify(ratio, desc));
}

inline int QuatLoadParam(double ratio, const OpengaAdapter::ParamDescElement &desc) {
    int target_load = Quantify(ratio, desc);
    // 减少targetload没必要的参数档位，降低参数复杂度
    if (target_load > 15 && target_load < 85) {
        target_load = target_load >> 2 << 2;
    }
    return target_load;
}

inline int QuatLargeParam(double ratio, int step, const OpengaAdapter::ParamDescElement &desc) {
    return (Quantify(ratio, desc) / step) * step;
}

Sim::Tunables OpengaAdapter::TranslateParamSeq(const ParamSeq &p) const {
    Sim::Tunables t;

    ParamSeq::const_iterator  it_seq  = p.begin();
    ParamDesc::const_iterator it_desc = param_desc_.begin();

    // interactive 调速器参数上下限
    int idx = 0;
    for (const auto &cluster : soc_->clusters_) {
        t.interactive[idx].hispeed_freq        = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        t.interactive[idx].go_hispeed_load     = QuatLoadParam(*it_seq++, *it_desc++);
        t.interactive[idx].min_sample_time     = Quantify(*it_seq++, *it_desc++);
        t.interactive[idx].max_freq_hysteresis = Quantify(*it_seq++, *it_desc++);

        int n_opp = cluster.model_.opp_model.size();
        int n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
        int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);

        for (int i = 0; i < n_above; ++i) {
            t.interactive[idx].above_hispeed_delay[i] = Quantify(*it_seq++, *it_desc++);
        }
        for (int i = 0; i < n_targetloads; ++i) {
            t.interactive[idx].target_loads[i] = QuatLoadParam(*it_seq++, *it_desc++);
        }
        idx++;
    }
    // WALT HMP 调速器参数上下限
    t.sched.sched_downmigrate                  = QuatLoadParam(*it_seq++, *it_desc++);
    t.sched.sched_upmigrate                    = QuatLoadParam(*it_seq++, *it_desc++);
    t.sched.sched_upmigrate                    = std::max(t.sched.sched_downmigrate, t.sched.sched_upmigrate);
    t.sched.sched_freq_aggregate_threshold_pct = QuatLargeParam(*it_seq++, 25, *it_desc++);
    t.sched.sched_ravg_hist_size               = Quantify(*it_seq++, *it_desc++);
    t.sched.sched_window_stats_policy          = Quantify(*it_seq++, *it_desc++);
    t.sched.timer_rate                         = Quantify(*it_seq++, *it_desc++);
    // 输入升频参数上下限
    idx = 0;
    for (const auto &cluster : soc_->clusters_) {
        t.input.boost_freq[idx] = QuatFreqParam(*it_seq++, cluster, *it_desc++);
        idx++;
    }
    t.input.duration_quantum = QuatLargeParam(*it_seq++, 10, *it_desc++);

    idx = 0;
    for (const auto &cluster : soc_->clusters_) {
        auto & tunable       = t.interactive[idx];
        double timer_quantum = t.sched.timer_rate;

        tunable.min_sample_time     = std::max(1.0, std::round(tunable.min_sample_time / timer_quantum));
        tunable.max_freq_hysteresis = std::max(1.0, std::round(tunable.max_freq_hysteresis / timer_quantum));

        int n_opp   = cluster.model_.opp_model.size();
        int n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);

        for (int i = 0; i < n_above; ++i) {
            tunable.above_hispeed_delay[i] = std::max(1.0, std::round(tunable.above_hispeed_delay[i] / timer_quantum));
        }
        idx++;
    }

    return t;
}

void OpengaAdapter::InitParamDesc(const ParamDescCfg &p) {
    // interactive 调速器参数上下限
    for (const auto &cluster : soc_->clusters_) {
        ParamDescElement hispeed_freq_desc = {cluster.model_.min_freq, cluster.model_.max_freq};
        param_desc_.push_back(hispeed_freq_desc);
        param_desc_.push_back(p.go_hispeed_load);
        param_desc_.push_back(p.min_sample_time);
        param_desc_.push_back(p.max_freq_hysteresis);

        int n_opp = cluster.model_.opp_model.size();
        int n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
        int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);
        
        for (int i = 0; i < n_above; ++i) {
            param_desc_.push_back(p.above_hispeed_delay);
        }
        for (int i = 0; i < n_targetloads; ++i) {
            param_desc_.push_back(p.target_loads);
        }
    }
    // WALT HMP 调速器参数上下限
    param_desc_.push_back(p.sched_downmigrate);
    param_desc_.push_back(p.sched_upmigrate);
    param_desc_.push_back(p.sched_freq_aggregate_threshold_pct);
    param_desc_.push_back(p.sched_ravg_hist_size);
    param_desc_.push_back(p.sched_window_stats_policy);
    param_desc_.push_back(p.timer_rate);
    // 输入升频参数上下限
    for (const auto &cluster : soc_->clusters_) {
        ParamDescElement input_freq = {cluster.model_.min_freq, cluster.model_.max_freq};
        param_desc_.push_back(input_freq);
    }
    param_desc_.push_back(p.input_duration);
    param_len_ = param_desc_.size();
}

Sim::Tunables OpengaAdapter::GenerateDefaultTunables(void) const {
    Sim::Tunables t;
    // interactive 调速器参数上下限
    int idx = 0;
    for (const auto &cluster : soc_->clusters_) {
        t.interactive[idx].hispeed_freq        = cluster.freq_floor_to_opp(cluster.model_.max_freq * 0.6);
        t.interactive[idx].go_hispeed_load     = 90;
        t.interactive[idx].min_sample_time     = 2;
        t.interactive[idx].max_freq_hysteresis = 2;

        int n_opp = cluster.model_.opp_model.size();
        int n_above = std::min(ABOVE_DELAY_MAX_LEN, n_opp);
        int n_targetloads = std::min(TARGET_LOAD_MAX_LEN, n_opp);
        
        for (int i = 0; i < n_above; ++i) {
            t.interactive[idx].above_hispeed_delay[i] = 1;
        }
        for (int i = 0; i < n_targetloads; ++i) {
            t.interactive[idx].target_loads[i] = 90;
        }
        idx++;
    }
    // WALT HMP 调速器参数上下限
    t.sched.sched_downmigrate                  = 85;
    t.sched.sched_upmigrate                    = 95;
    t.sched.sched_freq_aggregate_threshold_pct = 1000;
    t.sched.sched_ravg_hist_size               = 5;
    t.sched.sched_window_stats_policy          = WaltHmp::WINDOW_STATS_MAX_RECENT_AVG;
    t.sched.timer_rate                         = 2;
    // 输入升频参数上下限
    idx = 0;
    for (const auto &cluster : soc_->clusters_) {
        t.input.boost_freq[idx] = cluster.freq_floor_to_opp(cluster.model_.max_freq * 0.6);
        idx++;
    }
    t.input.duration_quantum = 10;
    return t;
}

void OpengaAdapter::InitDefaultScore() {
    Sim::Tunables t = GenerateDefaultTunables();
    Sim::Score    s = {1.0, 1.0, 1.0};

    Sim sim(t, s, sim_misc_);
    default_score_ = sim.Run(*workload_, *idleload_, *soc_, true);
}

std::vector<OpengaAdapter::Result> OpengaAdapter::Optimize(void) {
    using namespace std::placeholders;
    EA::Chronometer timer;
    timer.tic();

    GA_Type ga_obj(ga_cfg_.random_seed);
    ga_obj.problem_mode            = EA::GA_MODE::NSGA_III;
    ga_obj.verbose                 = false;
    ga_obj.population              = ga_cfg_.population;
    ga_obj.generation_max          = ga_cfg_.generation_max;
    ga_obj.calculate_MO_objectives = std::bind(&OpengaAdapter::CalcMultiObjectives, this, _1);
    ga_obj.init_genes              = std::bind(&OpengaAdapter::InitParamSeq, this, _1, _2);
    ga_obj.eval_solution           = std::bind(&OpengaAdapter::EvalParamSeq, this, _1, _2);
    ga_obj.mutate                  = std::bind(&OpengaAdapter::Mutate, this, _1, _2, _3);
    ga_obj.crossover               = std::bind(&OpengaAdapter::Crossover, this, _1, _2, _3);
    ga_obj.MO_report_generation    = std::bind(&OpengaAdapter::MO_report_generation, this, _1, _2, _3);
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
