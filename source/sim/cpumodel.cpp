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
    name_ = j["name"];
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
    enough_capacity_pct_ = j["enoughCapacityPct"];
}

int Soc::GetEnoughCapacity(void) const {
    return (clusters_.back().model_.max_freq * clusters_.back().model_.efficiency * enough_capacity_pct_);
}
