#include "dump.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "json.hpp"
#include "misc.h"

std::string TargetLoadsToStr(const Interactive::Tunables &t, const Cluster &cl) {
    using namespace std;
    ostringstream buf;

    int n_opp         = cl.model_.opp_model.size();
    int n_targetloads = min(TARGET_LOAD_MAX_LEN, n_opp);

    const int min_freq = cl.GetMinfreq();
    const int max_freq = cl.GetMaxfreq();

    int prev_tl = -1;
    for (int i = 0; i < n_targetloads; ++i) {
        int f  = cl.GetOpp(i);
        int tl = t.target_loads[i];
        if (prev_tl == tl)
            continue;
        if (f == min_freq) {
            buf << (int)tl;
            prev_tl = tl;
        } else if (f > min_freq && f <= max_freq) {
            buf << ' ' << Mhz2kHz(f) << ":" << (int)tl;
            prev_tl = tl;
        } else {
            continue;
        }
    }

    return buf.str();
}

std::string HispeedDelayToStr(const Interactive::Tunables &t, const Cluster &cl, int timer_rate) {
    using namespace std;
    ostringstream buf;

    auto multiple_to_us = [=](int multiple) { return Ms2Us(Quantum2Ms(multiple * timer_rate) - 2); };
    int  n_opp          = cl.model_.opp_model.size();
    int  n_above        = min(ABOVE_DELAY_MAX_LEN, n_opp) - 1;  // 最高频的above_delay并没有用

    const int max_freq = cl.GetMaxfreq();
    const int hispeed  = cl.freq_floor_to_opp(t.hispeed_freq);

    int prev_ahd = -1;
    for (int i = 0; i < n_above; ++i) {
        int f   = cl.GetOpp(i);
        int ahd = t.above_hispeed_delay[i];
        if (prev_ahd == ahd)
            continue;
        if (f == hispeed) {
            buf << multiple_to_us(ahd);
            prev_ahd = ahd;
        } else if (f > hispeed && f <= max_freq) {
            buf << ' ' << Mhz2kHz(f) << ":" << multiple_to_us(ahd);
            prev_ahd = ahd;
        } else {
            continue;
        }
    }

    return buf.str();
}

template <typename T>
nlohmann::json TunableToJson(const T &t, const Soc &soc) {
    return nlohmann::json();
}

template <>
nlohmann::json TunableToJson<GovernorTs<Interactive>>(const GovernorTs<Interactive> &t, const Soc &soc) {
    nlohmann::json ret;

    int cluster_num = soc.clusters_.size();
    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        const auto &g  = t.t[idx_cluster];
        const auto &cl = soc.clusters_[idx_cluster];

        auto multiple_to_us = [=](int multiple) { return Ms2Us(Quantum2Ms(multiple * 2) - 2); };

        nlohmann::json cl_param;
        cl_param["hispeed_freq"]        = Mhz2kHz(cl.freq_floor_to_opp(g.hispeed_freq));
        cl_param["go_hispeed_load"]     = g.go_hispeed_load;
        cl_param["min_sample_time"]     = multiple_to_us(g.min_sample_time);
        cl_param["max_freq_hysteresis"] = multiple_to_us(g.max_freq_hysteresis);
        cl_param["above_hispeed_delay"] = HispeedDelayToStr(g, cl, 2);
        cl_param["target_loads"]        = TargetLoadsToStr(g, cl);
        cl_param["min_freq"]            = cl.GetMinfreq();
        cl_param["max_freq"]            = cl.GetMaxfreq();

        ret.emplace_back(cl_param);
    }
    return ret;
}

template <>
nlohmann::json TunableToJson<WaltHmp::Tunables>(const WaltHmp::Tunables &t, const Soc &soc) {
    nlohmann::json ret;
    ret["sched_downmigrate"]         = t.sched_downmigrate;
    ret["sched_upmigrate"]           = t.sched_upmigrate;
    ret["sched_ravg_hist_size"]      = t.sched_ravg_hist_size;
    ret["sched_window_stats_policy"] = t.sched_window_stats_policy;
    ret["sched_boost"]               = t.sched_boost;
    ret["timer_rate"]                = t.timer_rate;
    return ret;
}

template <>
nlohmann::json TunableToJson<PeltHmp::Tunables>(const PeltHmp::Tunables &t, const Soc &soc) {
    nlohmann::json ret;
    ret["down_threshold"]     = t.down_threshold;
    ret["up_threshold"]       = t.up_threshold;
    ret["load_avg_period_ms"] = t.load_avg_period_ms;
    ret["boost"]              = t.boost;
    ret["timer_rate"]         = t.timer_rate;
    return ret;
}

template <>
nlohmann::json TunableToJson<InputBoostWalt::Tunables>(const InputBoostWalt::Tunables &t, const Soc &soc) {
    nlohmann::json ret;
    if (soc.GetInputBoostFeature() == true) {
        ret["freq"] = {t.boost_freq[0], t.boost_freq[1]};
        ret["ms"]   = Quantum2Ms(t.duration_quantum);
    }
    return ret;
}

template <>
nlohmann::json TunableToJson<InputBoostPelt::Tunables>(const InputBoostPelt::Tunables &t, const Soc &soc) {
    nlohmann::json ret;
    if (soc.GetInputBoostFeature() == true) {
        ret["freq"] = {t.boost_freq[0], t.boost_freq[1]};
        ret["ms"]   = Quantum2Ms(t.duration_quantum);
    }
    return ret;
}

template <>
nlohmann::json TunableToJson<UperfBoostWalt::Tunables>(const UperfBoostWalt::Tunables &t, const Soc &soc) {
    nlohmann::json ret;

    int little_cl_idx = soc.GetLittleClusterIdx();
    int big_cl_idx    = soc.GetBigClusterIdx();
    Soc soc_boosted   = soc;
    soc_boosted.clusters_[little_cl_idx].SetMinfreq(t.min_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMinfreq(t.min_freq[big_cl_idx]);
    soc_boosted.clusters_[little_cl_idx].SetMaxfreq(t.max_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMaxfreq(t.max_freq[big_cl_idx]);

    GovernorTs<Interactive> ts;
    ts.t[little_cl_idx] = t.little;
    ts.t[big_cl_idx]    = t.big;

    nlohmann::json sched;
    sched["sched_upmigrate"]   = t.sched_up;
    sched["sched_downmigrate"] = t.sched_down;

    ret["cpufreq"] = TunableToJson<GovernorTs<Interactive>>(ts, soc_boosted);
    ret["sched"]   = sched;
    return ret;
}

template <>
nlohmann::json TunableToJson<UperfBoostPelt::Tunables>(const UperfBoostPelt::Tunables &t, const Soc &soc) {
    nlohmann::json ret;

    int little_cl_idx = soc.GetLittleClusterIdx();
    int big_cl_idx    = soc.GetBigClusterIdx();
    Soc soc_boosted   = soc;
    soc_boosted.clusters_[little_cl_idx].SetMinfreq(t.min_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMinfreq(t.min_freq[big_cl_idx]);
    soc_boosted.clusters_[little_cl_idx].SetMaxfreq(t.max_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMaxfreq(t.max_freq[big_cl_idx]);

    GovernorTs<Interactive> ts;
    ts.t[little_cl_idx] = t.little;
    ts.t[big_cl_idx]    = t.big;

    nlohmann::json sched;
    sched["up_threshold"]   = t.sched_up;
    sched["down_threshold"] = t.sched_down;

    ret["cpufreq"] = TunableToJson<GovernorTs<Interactive>>(ts, soc_boosted);
    ret["sched"]   = sched;
    return ret;
}

template <typename T>
void TunableToStream(std::ostringstream &os, const T &t, const Soc &soc) {
    return;
}

template <>
void TunableToStream<GovernorTs<Interactive>>(std::ostringstream &os, const GovernorTs<Interactive> &t,
                                              const Soc &soc) {
    using namespace std;
    int cluster_num = soc.clusters_.size();
    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        const auto &g       = t.t[idx_cluster];
        const int   hispeed = soc.clusters_[idx_cluster].freq_floor_to_opp(g.hispeed_freq);

        auto multiple_to_us = [=](int multiple) { return Ms2Us(Quantum2Ms(multiple * 2) - 2); };

        os << "[interactive] cluster " << idx_cluster << endl << endl;
        os << "hispeed_freq: " << Mhz2kHz(hispeed) << endl;
        os << "go_hispeed_load: " << g.go_hispeed_load << endl;
        os << "min_sample_time: " << multiple_to_us(g.min_sample_time) << endl;
        os << "max_freq_hysteresis: " << multiple_to_us(g.max_freq_hysteresis) << endl;

        os << "above_hispeed_delay: ";
        os << HispeedDelayToStr(g, soc.clusters_[idx_cluster], 2) << endl;
        os << "target_loads: ";
        os << TargetLoadsToStr(g, soc.clusters_[idx_cluster]) << endl;
        os << endl;
    }
}

template <>
void TunableToStream<WaltHmp::Tunables>(std::ostringstream &os, const WaltHmp::Tunables &t, const Soc &soc) {
    using namespace std;
    os << "[hmp sched]" << endl << endl;
    os << "sched_downmigrate: " << t.sched_downmigrate << endl;
    os << "sched_upmigrate: " << t.sched_upmigrate << endl;
    os << "sched_ravg_hist_size: " << t.sched_ravg_hist_size << endl;
    os << "sched_window_stats_policy: " << t.sched_window_stats_policy << endl;
    os << "sched_boost: " << t.sched_boost << endl;
    os << "timer_rate: " << Ms2Us(Quantum2Ms(t.timer_rate)) << endl;
    os << endl;
}

template <>
void TunableToStream<PeltHmp::Tunables>(std::ostringstream &os, const PeltHmp::Tunables &t, const Soc &soc) {
    using namespace std;
    os << "[hmp sched]" << endl << endl;
    os << "down_threshold: " << t.down_threshold << endl;
    os << "up_threshold: " << t.up_threshold << endl;
    os << "load_avg_period_ms: " << t.load_avg_period_ms << endl;
    os << "boost: " << t.boost << endl;
    os << "timer_rate: " << Ms2Us(Quantum2Ms(t.timer_rate)) << endl;
    os << endl;
}

template <>
void TunableToStream<InputBoostWalt::Tunables>(std::ostringstream &os, const InputBoostWalt::Tunables &t,
                                               const Soc &soc) {
    using namespace std;
    int cluster_num = soc.clusters_.size();
    if (soc.GetInputBoostFeature() == true) {
        os << "[input boost]" << endl << endl;
        for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
            os << "cluster " << idx_cluster << ": " << t.boost_freq[idx_cluster] << endl;
        }
        os << "ms: " << Quantum2Ms(t.duration_quantum) << endl;
        os << endl;
    }
}

template <>
void TunableToStream<InputBoostPelt::Tunables>(std::ostringstream &os, const InputBoostPelt::Tunables &t,
                                               const Soc &soc) {
    using namespace std;
    int cluster_num = soc.clusters_.size();
    if (soc.GetInputBoostFeature() == true) {
        os << "[input boost]" << endl << endl;
        for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
            os << "cluster " << idx_cluster << ": " << t.boost_freq[idx_cluster] << endl;
        }
        os << "ms: " << Quantum2Ms(t.duration_quantum) << endl;
        os << endl;
    }
}

template <>
void TunableToStream<UperfBoostWalt::Tunables>(std::ostringstream &os, const UperfBoostWalt::Tunables &t,
                                               const Soc &soc) {
    using namespace std;
    int cluster_num = soc.clusters_.size();
    os << "[uperf boost]" << endl << endl;
    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        os << "cluster " << idx_cluster << ": " << t.min_freq[idx_cluster] << " - " << t.max_freq[idx_cluster] << endl;
    }
    os << "sched_up  : " << t.sched_up << endl;
    os << "sched_down: " << t.sched_down << endl;

    int little_cl_idx = soc.GetLittleClusterIdx();
    int big_cl_idx    = soc.GetBigClusterIdx();
    Soc soc_boosted   = soc;
    soc_boosted.clusters_[little_cl_idx].SetMinfreq(t.min_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMinfreq(t.min_freq[big_cl_idx]);
    soc_boosted.clusters_[little_cl_idx].SetMaxfreq(t.max_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMaxfreq(t.max_freq[big_cl_idx]);

    // GovernorTs<Interactive> ts;
    // ts.t[little_cl_idx] = t.little;
    // ts.t[big_cl_idx]    = t.big;
    // TunableToStream<GovernorTs<Interactive>>(os, ts, soc_boosted);
    os << endl;
}

template <>
void TunableToStream<UperfBoostPelt::Tunables>(std::ostringstream &os, const UperfBoostPelt::Tunables &t,
                                               const Soc &soc) {
    using namespace std;
    int cluster_num = soc.clusters_.size();
    os << "[uperf boost]" << endl << endl;
    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        os << "cluster " << idx_cluster << ": " << t.min_freq[idx_cluster] << " - " << t.max_freq[idx_cluster] << endl;
    }
    os << "sched_up  : " << t.sched_up << endl;
    os << "sched_down: " << t.sched_down << endl;

    int little_cl_idx = soc.GetLittleClusterIdx();
    int big_cl_idx    = soc.GetBigClusterIdx();
    Soc soc_boosted   = soc;
    soc_boosted.clusters_[little_cl_idx].SetMinfreq(t.min_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMinfreq(t.min_freq[big_cl_idx]);
    soc_boosted.clusters_[little_cl_idx].SetMaxfreq(t.max_freq[little_cl_idx]);
    soc_boosted.clusters_[big_cl_idx].SetMaxfreq(t.max_freq[big_cl_idx]);

    // GovernorTs<Interactive> ts;
    // ts.t[little_cl_idx] = t.little;
    // ts.t[big_cl_idx]    = t.big;
    // TunableToStream<GovernorTs<Interactive>>(os, ts, soc_boosted);
    os << endl;
}

// 生成形如"0:902000 1:0 2:0 3:0 4:1401000"的参数
std::string QcomFreqParamToStr(int freq0, int freq1, int ncore0, int ncore1) {
    using namespace std;
    ostringstream buf;

    buf << "0:" << Mhz2kHz(freq0);
    int core_idx = 1;
    for (; core_idx < ncore0; ++core_idx) {
        buf << " " << core_idx << ":0";
    }
    // 为了适应4+2而cpu模型却使用4+4的情况(避免只有2个大核相比4个小核在算功耗上有核心数量的优势)
    // 因此使用"0:902000 1:902000 2:902000 3:902000 4:1401000"这样的通配方式
    if (ncore1 > 0) {
        buf << " " << core_idx << ":" << Mhz2kHz(freq1);
    }
    return buf.str();
}

template <typename SimType>
void Dumper<SimType>::DumpToTXT(const OpengaResults &results) const {
    using namespace std;
    string   filename = soc_.name_ + ".txt";
    ofstream ofs(output_path_ + filename);

    int idx_ind = 0;
    for (const auto &r : results) {
        ofs << "================" << endl << endl;
        ofs << ">>> " << idx_ind << " <<<" << endl;
        ofs << "performance: " << Double2Pct(r.score.performance) << endl;
        ofs << "battery_life: " << Double2Pct(r.score.battery_life) << endl;
        ofs << "idle_lasting: " << Double2Pct(r.score.idle_lasting) << endl;
        ofs << endl;
        ofs << SimTunableToStr(r.tunable);
        idx_ind++;
    }
    return;
}

template <typename SimType>
void Dumper<SimType>::DumpToCSV(const OpengaResults &results) const {
    using namespace std;
    string   filename = soc_.name_ + ".csv";
    ofstream ofs(output_path_ + filename);

    int idx_ind = 0;
    for (const auto &r : results) {
        ofs << Double2Pct(r.score.performance) << ',';
        ofs << Double2Pct(r.score.battery_life) << ',';
        ofs << Double2Pct(r.score.idle_lasting) << ',';
        ofs << idx_ind;
        ofs << endl;
        idx_ind++;
    }
    return;
}

template <typename SimType>
void Dumper<SimType>::DumpToShellScript(const OpengaResults &results) {
    using namespace std;
    string filedir  = output_path_ + soc_.name_ + "/";
    string filepath = filedir + "powercfg.sh";

    if (access(filedir.c_str(), F_OK) == -1) {
        if (mkdir(filedir.c_str(), 0755) == -1) {
            cout << filedir << " cannot be created." << endl;
            return;
        }
    }

    ofstream ofs(filepath);

    string shell_template;
    {
        ifstream      ifs("./template/powercfg_template.sh");
        ostringstream ss;
        ss << ifs.rdbuf();
        shell_template = ss.str();
    }

    string datetime;
    {
        ostringstream ss;

        auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
        ss << put_time(localtime(&t), "%F %T");
        datetime = ss.str();
    }

    ReplaceAll(shell_template, "[project_name]", "Project WIPE v2");
    ReplaceAll(shell_template, "[github_url]", "https://github.com/yc9559/wipe-v2");
    ReplaceAll(shell_template, "[yourname]", "yc9559");
    ReplaceAll(shell_template, "[platform_name]", soc_.name_);
    ReplaceAll(shell_template, "[generated_time]", datetime);
    Replace(shell_template, "[sysfs_obj]", SysfsObjToStr());
    Replace(shell_template, "[param_num]", to_string(n_param_));

    double level_map[PERF_LEVEL_NUM] = {0.0, 0.15, 0.30, 0.50, 0.75, 0.99, 1.20};
    // 性能评分低于level中续航最长的
    auto find_level = [level_map, &results](int level) {
        double perf_thd      = level_map[level];
        double max_batt_life = 0.0;
        int    best_idx      = 0;
        int    idx_ind       = 0;
        for (const auto &r : results) {
            if ((r.score.performance < perf_thd) && (r.score.battery_life > max_batt_life)) {
                best_idx      = idx_ind;
                max_batt_life = r.score.battery_life;
            }
            ++idx_ind;
        }
        return best_idx;
    };

    // 替换 [levelx] 为 参数内容
    for (int level = 0; level < PERF_LEVEL_NUM; ++level) {
        int    ind_idx = find_level(level);
        string level_content;
        {
            ostringstream ss;
            const auto &  score = results[ind_idx].score;
            ss.setf(ios::fixed, ios::floatfield);
            ss.precision(1);
            ss << "# lag percent: " << Double2Pct(score.performance) << "%" << endl;
            ss << "# battery life: " << Double2Pct(score.battery_life) << "%" << endl;
            ss << LevelToStr(results[ind_idx].tunable, level);
            level_content = ss.str();
        }

        ostringstream sub_ss;
        sub_ss << "[level" << level << "]";
        Replace(shell_template, sub_ss.str(), level_content);
    }

    ofs << shell_template;
    return;
}

template <typename SimType>
void Dumper<SimType>::DumpToUperfJson(const OpengaResults &results) const {
    using namespace std;
    string   filename = soc_.name_ + ".json";
    ofstream ofs(output_path_ + filename);

    // 性能评分低于level中续航最长的
    auto find_level = [&results](float perf_thd) {
        double max_batt_life = 0.0;
        int    best_idx      = 0;
        int    idx_ind       = 0;
        for (const auto &r : results) {
            if ((r.score.performance < perf_thd) && (r.score.battery_life > max_batt_life)) {
                best_idx      = idx_ind;
                max_batt_life = r.score.battery_life;
            }
            ++idx_ind;
        }
        return best_idx;
    };

    auto make_mode = [=](float perf_thd) {
        const auto &r = results[find_level(perf_thd)];
        const auto &t = r.tunable;
        const auto &s = r.score;

        nlohmann::json normal;
        normal["cpufreq"] = TunableToJson<GovernorTs<typename SimType::Governor>>(t.governor, soc_);
        normal["sched"]   = TunableToJson<typename SimType::Sched::Tunables>(t.sched, soc_);

        nlohmann::json boost = TunableToJson<typename SimType::Boost::Tunables>(t.boost, soc_);

        nlohmann::json mode;
        mode["lag_percent"]  = s.performance;
        mode["battery_life"] = s.battery_life;
        mode["normal"]       = normal;
        mode["boost"]        = boost;
        return mode;
    };

    nlohmann::json uperf_data;
    uperf_data["performance"] = make_mode(0.05);
    uperf_data["balance"]     = make_mode(0.50);
    uperf_data["powersave"]   = make_mode(1.05);

    // pretty printing
    ofs << uperf_data.dump(4);
}

template <>
void Dumper<SimQcomBL>::DumpToUperfJson(const OpengaResults &results) const {
    // 只有uperf支持json
}

template <>
void Dumper<SimBL>::DumpToUperfJson(const OpengaResults &results) const {
    // 只有uperf支持json
}

template <>
void Dumper<SimQcomUp>::DumpToShellScript(const std::vector<OpengaAdapter<SimQcomUp>::Result> &results) {
    // Uperf类型不使用shell脚本控制
    return;
}

template <>
void Dumper<SimUp>::DumpToShellScript(const std::vector<OpengaAdapter<SimUp>::Result> &results) {
    // Uperf类型不使用shell脚本控制
    return;
}

template <typename SimType>
std::string Dumper<SimType>::SimTunableToStr(const typename SimType::Tunables &t) const {
    using namespace std;
    ostringstream buf;
    TunableToStream<GovernorTs<typename SimType::Governor>>(buf, t.governor, soc_);
    TunableToStream<typename SimType::Sched::Tunables>(buf, t.sched, soc_);
    TunableToStream<typename SimType::Boost::Tunables>(buf, t.boost, soc_);
    return buf.str();
}

template <>
std::string Dumper<SimQcomBL>::LevelToStr(const SimQcomBL::Tunables &t, int level) const {
    using namespace std;
    ostringstream buf;

    int cluster_num = soc_.clusters_.size();
    int n           = 0;

    string prefix;
    {
        ostringstream prefix_ss;
        prefix_ss << "level" << level << "_val";
        prefix = prefix_ss.str();
    }

    // /sys/module/msm_thermal/core_control/enabled
    buf << prefix << ++n << "=\"0\"" << endl;
    // /sys/module/msm_thermal/parameters/enabled
    buf << prefix << ++n << "=\"N\"" << endl;

    // level1_val1="38000"
    auto append_val     = [&](int param_val) { buf << prefix << ++n << "=\"" << param_val << "\"" << endl; };
    auto append_str_val = [&](const string &param_val) { buf << prefix << ++n << "=\"" << param_val << "\"" << endl; };
    auto multiple_to_us = [=](int multiple) { return Ms2Us(Quantum2Ms(multiple * t.sched.timer_rate) - 2); };

    // 高通平台的最低最高频率限制接口
    {
        int f0, f1;
        int ncore0, ncore1;
        if (cluster_num > 1) {
            ncore0 = soc_.clusters_[0].model_.core_num;
            ncore1 = soc_.clusters_[1].model_.core_num;
            // /sys/module/msm_performance/parameters/cpu_min_freq
            f0 = soc_.clusters_[0].model_.min_freq - 1;
            f1 = soc_.clusters_[1].model_.min_freq - 1;
            append_str_val(QcomFreqParamToStr(f0, f1, ncore0, ncore1));
            // /sys/module/msm_performance/parameters/cpu_max_freq
            f0 = soc_.clusters_[0].model_.max_freq + 1;
            f1 = soc_.clusters_[1].model_.max_freq + 1;
            append_str_val(QcomFreqParamToStr(f0, f1, ncore0, ncore1));
        } else {
            ncore0 = soc_.clusters_[0].model_.core_num;
            ncore1 = 0;
            // /sys/module/msm_performance/parameters/cpu_min_freq
            f0 = soc_.clusters_[0].model_.min_freq - 1;
            append_str_val(QcomFreqParamToStr(f0, 0, ncore0, ncore1));
            // /sys/module/msm_performance/parameters/cpu_max_freq
            f0 = soc_.clusters_[0].model_.max_freq + 1;
            append_str_val(QcomFreqParamToStr(f0, 0, ncore0, ncore1));
        }
    }

    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        const auto &g = t.governor.t[idx_cluster];
        // 核心上线 /sys/devices/system/cpu/cpu4/online
        append_val(1);
        // append_cpufreq_param("scaling_governor", idx_cluster);
        append_str_val("interactive");
        // append_cpufreq_param("scaling_min_freq", idx_cluster);
        // 假设频率表为633600 1036000，设置为632000，由于低于最小值，会被修正为633600
        // 假设频率表为400000 633600 1036000，设置为632000，由于大于最小值，不会被强行修正，对于调频器等效为最低633600
        append_val(Mhz2kHz(soc_.clusters_[idx_cluster].model_.min_freq - 1));
        // append_cpufreq_param("scaling_max_freq", idx_cluster);
        // 假设频率表为1747200 1843200，设置为1844000，由于大于最大值，会被修正为1843200
        // 假设频率表为1747200 1843200
        // 1958000，设置为1844000，由于小于最大值，不会被强行修正，对于调频器等效为最大1843200
        append_val(Mhz2kHz(soc_.clusters_[idx_cluster].model_.max_freq + 1));
        // append_interactive_param("hispeed_freq", idx_cluster);
        append_val(Mhz2kHz(g.hispeed_freq));
        // append_interactive_param("go_hispeed_load", idx_cluster);
        append_val(g.go_hispeed_load);
        // append_interactive_param("min_sample_time", idx_cluster);
        append_val(multiple_to_us(g.min_sample_time));
        // append_interactive_param("max_freq_hysteresis", idx_cluster);
        append_val(multiple_to_us(g.max_freq_hysteresis));
        // append_interactive_param("above_hispeed_delay", idx_cluster);
        append_str_val(HispeedDelayToStr(g, soc_.clusters_[idx_cluster], t.sched.timer_rate));
        // append_interactive_param("target_loads", idx_cluster);
        append_str_val(TargetLoadsToStr(g, soc_.clusters_[idx_cluster]));
        // append_interactive_param("timer_rate", idx_cluster);
        append_val(Ms2Us(Quantum2Ms(t.sched.timer_rate)));
        // append_interactive_param("timer_slack", idx_cluster);
        append_val(12345678);
        // append_interactive_param("ignore_hispeed_on_notif", idx_cluster);
        append_val(0);
        // append_interactive_param("boost", idx_cluster);
        append_val(0);
        // append_interactive_param("fast_ramp_down", idx_cluster);
        append_val(0);
        // append_interactive_param("align_windows", idx_cluster);
        append_val(0);
        // append_interactive_param("use_migration_notif", idx_cluster);
        append_val(1);
        // append_interactive_param("enable_prediction", idx_cluster);
        append_val(0);
        // append_interactive_param("use_sched_load", idx_cluster);
        append_val(1);
        // append_interactive_param("boostpulse_duration", idx_cluster);
        append_val(0);
    }

    // 避免从50/50设置到70/70时，sched_downmigrate设置失败，由于downmigrate<=upmigrate
    // append_hmp_param("sched_downmigrate");
    append_val(t.sched.sched_downmigrate);
    // append_hmp_param("sched_upmigrate");
    append_val(t.sched.sched_upmigrate);
    // append_hmp_param("sched_downmigrate");
    append_val(t.sched.sched_downmigrate);
    // append_hmp_param("sched_ravg_hist_size");
    append_val(t.sched.sched_ravg_hist_size);
    // append_hmp_param("sched_window_stats_policy");
    append_val(t.sched.sched_window_stats_policy);
    // append_hmp_param("sched_boost");
    append_val(t.sched.sched_boost);
    // append_hmp_param("sched_spill_load");
    append_val(90);
    // append_hmp_param("sched_restrict_cluster_spill");
    append_val(1);
    // append_hmp_param("sched_prefer_sync_wakee_to_waker");
    append_val(1);
    // append_hmp_param("sched_freq_inc_notify");
    append_val(200000);
    // append_hmp_param("sched_freq_dec_notify");
    append_val(400000);

    // 触摸升频
    if (soc_.GetInputBoostFeature() == true) {
        // /sys/module/msm_performance/parameters/touchboost
        append_val(0);
        // /sys/module/cpu_boost/parameters/input_boost_ms
        append_val(Quantum2Ms(t.boost.duration_quantum));
        // /sys/module/cpu_boost/parameters/input_boost_freq
        int ncore0 = soc_.clusters_[0].model_.core_num;
        int ncore1 = soc_.clusters_[1].model_.core_num;
        append_str_val(QcomFreqParamToStr(t.boost.boost_freq[0], t.boost.boost_freq[1], ncore0, ncore1));
    }

    return buf.str();
}

template <>
std::string Dumper<SimQcomBL>::SysfsObjToStr(void) {
    using namespace std;
    ostringstream buf;

    buf << "SCHED_DIR=\"/proc/sys/kernel\"" << endl;

    // 单集群情况
    if (soc_.clusters_.size() < 2) {
        buf << "C0_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpufreq/interactive\"" << endl;
        buf << "C1_GOVERNOR_DIR=\"\"" << endl;
        buf << "C0_DIR=\"/sys/devices/system/cpu/cpu0\"" << endl;
        buf << "C1_DIR=\"/sys/devices/system/cpu/cpu4\"" << endl;
    } else {
        int c0_core_num = soc_.clusters_[0].model_.core_num;
        buf << "C0_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpu0/cpufreq/interactive\"" << endl;
        buf << "C1_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpu" << c0_core_num << "/cpufreq/interactive\"" << endl;
        buf << "C0_DIR=\"/sys/devices/system/cpu/cpu0\"" << endl;
        buf << "C1_DIR=\"/sys/devices/system/cpu/cpu" << c0_core_num << "\"" << endl;
    }
    buf << endl;

    string prefix      = "sysfs_obj";
    int    cluster_num = soc_.clusters_.size();
    int    n           = 0;

    // disable hotplug to switch governor
    buf << prefix << ++n << "=\"/sys/module/msm_thermal/core_control/enabled\"" << endl;
    buf << prefix << ++n << "=\"/sys/module/msm_thermal/parameters/enabled\"" << endl;

    // sysfs_obj1="${C0_GOVERNOR_DIR}/target_loads"
    auto append_interactive_param = [&](const string &param_name, int cluster_idx) {
        buf << prefix << ++n << "=\"${C" << cluster_idx << "_GOVERNOR_DIR}/" << param_name << "\"" << endl;
    };
    auto append_cpufreq_param = [&](const string &param_name, int cluster_idx) {
        buf << prefix << ++n << "=\"${C" << cluster_idx << "_DIR}/cpufreq/" << param_name << "\"" << endl;
    };
    auto append_hmp_param = [&](const string &param_name) {
        buf << prefix << ++n << "=\"${SCHED_DIR}/" << param_name << "\"" << endl;
    };

    // 高通平台的最低最高频率限制接口
    buf << prefix << ++n << "=\"/sys/module/msm_performance/parameters/cpu_min_freq\"" << endl;
    buf << prefix << ++n << "=\"/sys/module/msm_performance/parameters/cpu_max_freq\"" << endl;

    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        // 核心上线
        buf << prefix << ++n << "=\"${C" << idx_cluster << "_DIR}/online\"" << endl;
        // 统一调速器选择和最低最高频率
        append_cpufreq_param("scaling_governor", idx_cluster);
        append_cpufreq_param("scaling_min_freq", idx_cluster);
        append_cpufreq_param("scaling_max_freq", idx_cluster);
        append_interactive_param("hispeed_freq", idx_cluster);
        append_interactive_param("go_hispeed_load", idx_cluster);
        append_interactive_param("min_sample_time", idx_cluster);
        append_interactive_param("max_freq_hysteresis", idx_cluster);
        append_interactive_param("above_hispeed_delay", idx_cluster);
        append_interactive_param("target_loads", idx_cluster);
        append_interactive_param("timer_rate", idx_cluster);
        append_interactive_param("timer_slack", idx_cluster);
        append_interactive_param("ignore_hispeed_on_notif", idx_cluster);
        append_interactive_param("boost", idx_cluster);
        append_interactive_param("fast_ramp_down", idx_cluster);
        append_interactive_param("align_windows", idx_cluster);
        append_interactive_param("use_migration_notif", idx_cluster);
        append_interactive_param("enable_prediction", idx_cluster);
        append_interactive_param("use_sched_load", idx_cluster);
        append_interactive_param("boostpulse_duration", idx_cluster);
    }

    // 避免从50/50设置到70/70时，sched_downmigrate设置失败，由于downmigrate<=upmigrate
    append_hmp_param("sched_downmigrate");
    append_hmp_param("sched_upmigrate");
    append_hmp_param("sched_downmigrate");
    append_hmp_param("sched_ravg_hist_size");
    append_hmp_param("sched_window_stats_policy");
    append_hmp_param("sched_boost");
    append_hmp_param("sched_spill_load");
    append_hmp_param("sched_restrict_cluster_spill");
    append_hmp_param("sched_prefer_sync_wakee_to_waker");
    append_hmp_param("sched_freq_inc_notify");
    append_hmp_param("sched_freq_dec_notify");

    // 触摸升频
    if (soc_.GetInputBoostFeature() == true) {
        buf << prefix << ++n << "=\"/sys/module/msm_performance/parameters/touchboost\"" << endl;
        buf << prefix << ++n << "=\"/sys/module/cpu_boost/parameters/input_boost_ms\"" << endl;
        buf << prefix << ++n << "=\"/sys/module/cpu_boost/parameters/input_boost_freq\"" << endl;
    }

    n_param_ = n;
    return buf.str();
}

template <>
std::string Dumper<SimBL>::LevelToStr(const SimBL::Tunables &t, int level) const {
    using namespace std;
    ostringstream buf;

    int cluster_num = soc_.clusters_.size();
    int n           = 0;

    string prefix;
    {
        ostringstream prefix_ss;
        prefix_ss << "level" << level << "_val";
        prefix = prefix_ss.str();
    }

    // /sys/power/cpuhotplug/enabled
    buf << prefix << ++n << "=\"0\"" << endl;
    // /sys/devices/system/cpu/cpuhotplug/enabled
    buf << prefix << ++n << "=\"0\"" << endl;

    // level1_val1="38000"
    auto append_val     = [&](int param_val) { buf << prefix << ++n << "=\"" << param_val << "\"" << endl; };
    auto append_str_val = [&](const string &param_val) { buf << prefix << ++n << "=\"" << param_val << "\"" << endl; };
    auto multiple_to_us = [=](int multiple) { return Ms2Us(Quantum2Ms(multiple * t.sched.timer_rate) - 2); };

    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        const auto &g = t.governor.t[idx_cluster];
        // 核心上线 /sys/devices/system/cpu/cpu4/online
        append_val(1);
        // append_cpufreq_param("scaling_governor", idx_cluster);
        append_str_val("interactive");
        // append_cpufreq_param("scaling_min_freq", idx_cluster);
        // 假设频率表为633600 1036000，设置为632000，由于低于最小值，会被修正为633600
        // 假设频率表为400000 633600 1036000，设置为632000，由于大于最小值，不会被强行修正，对于调频器等效为最低633600
        append_val(Mhz2kHz(soc_.clusters_[idx_cluster].model_.min_freq - 1));
        // append_cpufreq_param("scaling_max_freq", idx_cluster);
        // 假设频率表为1747200 1843200，设置为1844000，由于大于最大值，会被修正为1843200
        // 假设频率表为1747200 1843200
        // 1958000，设置为1844000，由于小于最大值，不会被强行修正，对于调频器等效为最大1843200
        append_val(Mhz2kHz(soc_.clusters_[idx_cluster].model_.max_freq + 1));
        // append_interactive_param("hispeed_freq", idx_cluster);
        append_val(Mhz2kHz(g.hispeed_freq));
        // append_interactive_param("go_hispeed_load", idx_cluster);
        append_val(g.go_hispeed_load);
        // append_interactive_param("min_sample_time", idx_cluster);
        append_val(multiple_to_us(g.min_sample_time));
        // append_interactive_param("max_freq_hysteresis", idx_cluster);
        append_val(multiple_to_us(g.max_freq_hysteresis));
        // append_interactive_param("above_hispeed_delay", idx_cluster);
        append_str_val(HispeedDelayToStr(g, soc_.clusters_[idx_cluster], t.sched.timer_rate));
        // append_interactive_param("target_loads", idx_cluster);
        append_str_val(TargetLoadsToStr(g, soc_.clusters_[idx_cluster]));
        // append_interactive_param("timer_rate", idx_cluster);
        append_val(Ms2Us(Quantum2Ms(t.sched.timer_rate)));
        // append_interactive_param("timer_slack", idx_cluster);
        append_val(12345678);
        // append_interactive_param("boost", idx_cluster);
        append_val(0);
        // append_interactive_param("boostpulse_duration", idx_cluster);
        append_val(0);
    }

    // 避免从50/50设置到70/70时，sched_downmigrate设置失败，由于downmigrate<=upmigrate
    // append_hmp_param("down_threshold");
    append_val(t.sched.down_threshold);
    // append_hmp_param("up_threshold");
    append_val(t.sched.up_threshold);
    // append_hmp_param("down_threshold");
    append_val(t.sched.down_threshold);
    // append_hmp_param("load_avg_period_ms");
    append_val(t.sched.load_avg_period_ms);
    // append_hmp_param("boost");
    append_val(t.sched.boost);

    // 触摸升频
    if (soc_.GetInputBoostFeature() == true) {
        // /sys/module/msm_performance/parameters/touchboost
        append_val(0);
        // /sys/module/cpu_boost/parameters/input_boost_ms
        append_val(Quantum2Ms(t.boost.duration_quantum));
        // /sys/module/cpu_boost/parameters/input_boost_freq
        int ncore0 = soc_.clusters_[0].model_.core_num;
        int ncore1 = soc_.clusters_[1].model_.core_num;
        append_str_val(QcomFreqParamToStr(t.boost.boost_freq[0], t.boost.boost_freq[1], ncore0, ncore1));
    }

    return buf.str();
}

template <>
std::string Dumper<SimBL>::SysfsObjToStr(void) {
    using namespace std;
    ostringstream buf;

    buf << "SCHED_DIR=\"/proc/sys/kernel/hmp\"" << endl;

    // 单集群情况
    if (soc_.clusters_.size() < 2) {
        buf << "C0_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpufreq/interactive\"" << endl;
        buf << "C1_GOVERNOR_DIR=\"\"" << endl;
        buf << "C0_DIR=\"/sys/devices/system/cpu/cpu0\"" << endl;
        buf << "C1_DIR=\"/sys/devices/system/cpu/cpu4\"" << endl;
    } else {
        int c0_core_num = soc_.clusters_[0].model_.core_num;
        buf << "C0_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpu0/cpufreq/interactive\"" << endl;
        buf << "C1_GOVERNOR_DIR=\"/sys/devices/system/cpu/cpu" << c0_core_num << "/cpufreq/interactive\"" << endl;
        buf << "C0_DIR=\"/sys/devices/system/cpu/cpu0\"" << endl;
        buf << "C1_DIR=\"/sys/devices/system/cpu/cpu" << c0_core_num << "\"" << endl;
    }
    buf << endl;

    string prefix      = "sysfs_obj";
    int    cluster_num = soc_.clusters_.size();
    int    n           = 0;

    // Exynos hotplug
    buf << prefix << ++n << "=\"/sys/power/cpuhotplug/enabled\"" << endl;
    buf << prefix << ++n << "=\"/sys/devices/system/cpu/cpuhotplug/enabled\"" << endl;

    // sysfs_obj1="${C0_GOVERNOR_DIR}/target_loads"
    auto append_interactive_param = [&](const string &param_name, int cluster_idx) {
        buf << prefix << ++n << "=\"${C" << cluster_idx << "_GOVERNOR_DIR}/" << param_name << "\"" << endl;
    };
    auto append_cpufreq_param = [&](const string &param_name, int cluster_idx) {
        buf << prefix << ++n << "=\"${C" << cluster_idx << "_DIR}/cpufreq/" << param_name << "\"" << endl;
    };
    auto append_hmp_param = [&](const string &param_name) {
        buf << prefix << ++n << "=\"${SCHED_DIR}/" << param_name << "\"" << endl;
    };

    for (int idx_cluster = 0; idx_cluster < cluster_num; ++idx_cluster) {
        // 核心上线
        buf << prefix << ++n << "=\"${C" << idx_cluster << "_DIR}/online\"" << endl;
        // 统一调速器选择和最低最高频率
        append_cpufreq_param("scaling_governor", idx_cluster);
        append_cpufreq_param("scaling_min_freq", idx_cluster);
        append_cpufreq_param("scaling_max_freq", idx_cluster);
        append_interactive_param("hispeed_freq", idx_cluster);
        append_interactive_param("go_hispeed_load", idx_cluster);
        append_interactive_param("min_sample_time", idx_cluster);
        append_interactive_param("max_freq_hysteresis", idx_cluster);
        append_interactive_param("above_hispeed_delay", idx_cluster);
        append_interactive_param("target_loads", idx_cluster);
        append_interactive_param("timer_rate", idx_cluster);
        append_interactive_param("timer_slack", idx_cluster);
        append_interactive_param("boost", idx_cluster);
        append_interactive_param("boostpulse_duration", idx_cluster);
    }

    // 避免从50/50设置到70/70时，sched_downmigrate设置失败，由于downmigrate<=upmigrate
    append_hmp_param("down_threshold");
    append_hmp_param("up_threshold");
    append_hmp_param("down_threshold");
    append_hmp_param("load_avg_period_ms");
    append_hmp_param("boost");

    // 触摸升频
    if (soc_.GetInputBoostFeature() == true) {
        buf << prefix << ++n << "=\"/sys/module/msm_performance/parameters/touchboost\"" << endl;
        buf << prefix << ++n << "=\"/sys/module/cpu_boost/parameters/input_boost_ms\"" << endl;
        buf << prefix << ++n << "=\"/sys/module/cpu_boost/parameters/input_boost_freq\"" << endl;
    }

    n_param_ = n;
    return buf.str();
}

template <>
std::string Dumper<SimQcomUp>::LevelToStr(const SimQcomUp::Tunables &t, int level) const {
    return std::string();
}

template <>
std::string Dumper<SimQcomUp>::SysfsObjToStr(void) {
    return std::string();
}

template <>
std::string Dumper<SimUp>::LevelToStr(const SimUp::Tunables &t, int level) const {
    return std::string();
}

template <>
std::string Dumper<SimUp>::SysfsObjToStr(void) {
    return std::string();
}

template class Dumper<SimQcomBL>;
template class Dumper<SimBL>;
template class Dumper<SimQcomUp>;
template class Dumper<SimUp>;
