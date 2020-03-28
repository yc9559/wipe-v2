#ifndef __DUMP_H
#define __DUMP_H

#include <algorithm>
#include <string>
#include <vector>
#include "cpumodel.h"
#include "openga_helper.h"
#include "sim.hpp"

template <typename SimType>
class Dumper {
#define PERF_LEVEL_NUM 7
public:
    using OpengaResults = std::vector<typename OpengaAdapter<SimType>::Result>;

    Dumper() = delete;
    Dumper(const Soc &soc, const std::string &output_path) : soc_(soc), output_path_(output_path){};
    void DumpToTXT(const OpengaResults &results) const;
    void DumpToCSV(const OpengaResults &results) const;
    void DumpToShellScript(const OpengaResults &results);
    void DumpToUperfJson(const OpengaResults &results) const;

private:
    std::string SimTunableToStr(const typename SimType::Tunables &t) const;
    std::string LevelToStr(const typename SimType::Tunables &t, int level) const;
    std::string SysfsObjToStr(void);

    const Soc         soc_;
    const std::string output_path_;

    int n_param_;
};

#endif
