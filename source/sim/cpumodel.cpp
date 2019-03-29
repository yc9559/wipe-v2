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
    // printf("%d,%d,%d\n", cur_freq_, min_opp_idx_, cur_opp_idx_);
    // for (auto x : model_.opp_model) {
    //     printf("%d,%d,%d\n", x.freq, x.core_power, x.cluster_power);
    // }
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
        for (int i = 0; i < it["opp"].size(); ++i) {
            m.opp_model.push_back({it["opp"][i], it["corePower"][i], it["clusterPower"][i]});
        }
        clusters_.push_back(Cluster(m));
    }
}

Soc::~Soc() {
}
