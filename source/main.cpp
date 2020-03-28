#include <fstream>
#include <iostream>
#include <string>

#include "cpumodel.h"
#include "dump.h"
#include "json.hpp"
#include "openga_helper.h"
#include "sim.hpp"
#include "workload.h"

template <typename T>
void DoOpt(Soc &soc, const Workload &work, const Workload &idle) {
    auto nsga3_opt = OpengaAdapter<T>(&soc, &work, &idle, "./conf.json");
    auto ret       = nsga3_opt.Optimize();
    auto dumper    = Dumper<T>(soc, "./output/");
    dumper.DumpToTXT(ret);
    dumper.DumpToCSV(ret);
    dumper.DumpToShellScript(ret);
    dumper.DumpToUperfJson(ret);
}

int main() {
    nlohmann::json j;
    {
        std::ifstream ifs("./conf.json");
        if (!ifs.good()) {
            using namespace std;
            cout << "WIPE-v2 config file access ERROR: "
                 << "./conf.json" << endl;
            throw std::runtime_error("file access error");
        }
        ifs >> j;
    }

    auto todo_models = j["todoModels"];
    auto workload    = j["mergedWorkload"];
    auto idleload    = j["idleWorkload"];
    auto use_uperf   = j["useUperf"];

    Workload work(workload);
    Workload idle(idleload);

    for (const auto &model : todo_models) {
        Soc soc(model);
        if (use_uperf) {
            if (soc.GetSchedType() == Soc::kWalt) {
                DoOpt<SimQcomUp>(soc, work, idle);
            }
            if (soc.GetSchedType() == Soc::kPelt) {
                DoOpt<SimUp>(soc, work, idle);
            }
        } else {
            if (soc.GetSchedType() == Soc::kWalt) {
                DoOpt<SimQcomBL>(soc, work, idle);
            }
            if (soc.GetSchedType() == Soc::kPelt) {
                DoOpt<SimBL>(soc, work, idle);
            }
        }
    }

    return 0;
}
