#include <fstream>
#include <iostream>
#include <string>
#include "cpumodel.h"
#include "dump.h"
#include "json.hpp"
#include "openga_helper.h"
#include "sim.hpp"
#include "workload.h"

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

    Workload work(workload);
    Workload idle(idleload);

    for (const auto &model : todo_models) {
        Soc soc(model);
        if (soc.GetSchedType() == Soc::kWalt) {
            auto nsga3_opt = OpengaAdapter<SimQcomBL>(&soc, &work, &idle, "./conf.json");
            auto ret       = nsga3_opt.Optimize();
            auto dumper    = Dumper<SimQcomBL>(soc, "./output/");
            dumper.DumpToTXT(ret);
            dumper.DumpToCSV(ret);
            dumper.DumpToShellScript(ret);
        }
        if (soc.GetSchedType() == Soc::kPelt) {
            auto nsga3_opt = OpengaAdapter<SimBL>(&soc, &work, &idle, "./conf.json");
            auto ret       = nsga3_opt.Optimize();
            auto dumper    = Dumper<SimBL>(soc, "./output/");
            dumper.DumpToTXT(ret);
            dumper.DumpToCSV(ret);
            dumper.DumpToShellScript(ret);
        }
    }

    return 0;
}
