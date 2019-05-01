#include <iostream>
#include <fstream>
#include <string>
#include "cpumodel.h"
#include "sim.h"
#include "workload.h"
#include "openga_helper.h"
#include "json.hpp"
#include "dump.h"

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
    auto idleload = j["idleWorkload"];

    Workload work(workload);
    Workload idle(idleload);

    for (const auto &model : todo_models) {
        Soc soc(model);
        OpengaAdapter nsga3_opt(&soc, &work, &idle, "./conf.json");
        auto ret = nsga3_opt.Optimize();
        Dumper dumper(soc, "./output/");
        dumper.DumpToTXT(ret);
        dumper.DumpToCSV(ret);
        dumper.DumpToShellScript(ret);
    }

    return 0;
}
