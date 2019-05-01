#include "cpumodel.h"
#include <fstream>
#include <iostream>
#include "json.hpp"

Cluster::Cluster(Model model) : model_(model) {
    busy_pct_    = 0;
    min_opp_idx_ = 0;
    cur_opp_idx_ = 0;
    SetMinfreq(model.min_freq);
    SetCurfreq(model.max_freq);
}

Soc::Soc(const std::string &model_file) {
    std::ifstream  ifs(model_file);
    nlohmann::json j;
    ifs >> j;

    // SOC的型号
    name_ = j["name"];

    // 提供的容量大于SOC最大容量xx%的跳过卡顿判断
    enough_capacity_pct_ = j["enoughCapacityPct"];

    // 使用的调度器类型
    if (j["sched"] == "walt")
        sched_type_ = kWalt;
    else if (j["sched"] == "walt")
        sched_type_ = kPelt;
    else
        sched_type_ = kLegacy;

    // 多核心模式
    if (j["intra"] == "asmp")
        intra_type_ = kASMP;
    else
        intra_type_ = kSMP;
        
    // 频点与功耗
    for (const auto &it : j["cluster"]) {
        Cluster::Model m;
        m.core_num   = it["coreNum"];
        m.efficiency = it["efficiency"];
        m.min_freq   = it["minFreq"];
        m.max_freq   = it["maxFreq"];
        m.opp_model.reserve(it["opp"].size());
        for (uint32_t i = 0; i < it["opp"].size(); ++i) {
            m.opp_model.push_back({it["opp"][i], it["corePower"][i], it["clusterPower"][i]});
        }
        clusters_.push_back(Cluster(m));
    }
}
