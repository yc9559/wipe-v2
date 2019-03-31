#include <iostream>
#include <fstream>
#include <string>
#include "cpumodel.h"
#include "sim.h"
#include "workload.h"
#include "openga_helper.h"
#include "json.hpp"

void PrintResultCSV(const std::vector<OpengaAdapter::Result> &r) {
    for (const auto &s : r) {
        printf("%f,%f\n", s.score.performance, s.score.battery_life);
    }
}

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
        PrintResultCSV(ret);
    }

    return 0;
}
