#include <iostream>
#include <fstream>
#include <string>
#include "cpumodel.h"
#include "sim.h"
#include "workload.h"
#include "openga_helper.h"
#include "json.hpp"

int main() {
    nlohmann::json j;
    {
        std::ifstream ifs("./conf.json");
        if (!ifs.good()) {
            using namespace std;
            cout << "WIPE-v2 config file access ERROR: " << "./conf.json" << endl;
            throw std::runtime_error("file access error");
        }
        ifs >> j;
    }

    auto todo_models = j["todoModels"];
    auto workload = j["mergedWorkload"];

    for (const auto &model : todo_models) {
        Workload w(workload);
        Soc soc(model);
        OpengaAdapter nsga3_opt(&soc, &w, "./conf.json");
        auto ret = nsga3_opt.Optimize();
    }

    // std::string a("./dataset/workload/osborn/bili-feed.json");
    // Workload    w(a);

    // std::string b("./dataset/soc_model/model_sdm660.json");
    // Soc         soc(b);

    // OpengaAdapter::ParamDescCfg cfg;
    // cfg.above_hispeed_delay = {0, 2};
    // cfg.go_hispeed_load = {10, 99};
    // cfg.max_freq_hysteresis = {0, 4};
    // cfg.min_sample_time = {0, 4};
    // cfg.target_loads = {1, 99};
    // cfg.sched_downmigrate = {1, 99};
    // cfg.sched_upmigrate = {1, 99};
    // cfg.sched_freq_aggregate_threshold_pct = {25, 400};
    // cfg.sched_ravg_hist_size = {1, 5};
    // cfg.sched_window_stats_policy = {0, 3};
    // cfg.timer_rate = {1, 5};
    // cfg.input_duration = {0, 300};

    // OpengaAdapter ga_opt(&soc, &w, "./conf.json");
    // auto ret = ga_opt.Optimize();

    return 0;
}
