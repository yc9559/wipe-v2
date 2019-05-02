#ifndef __DUMP_H
#define __DUMP_H

#include <algorithm>
#include <string>
#include <vector>
#include "cpumodel.h"
#include "openga_helper.h"
#include "sim.h"

class Dumper {
#define PERF_LEVEL_NUM 7
public:
    Dumper() = delete;
    Dumper(const Soc &soc, const std::string &output_path) : soc_(soc), output_path_(output_path){};
    void DumpToTXT(const std::vector<OpengaAdapter::Result> &results) const;
    void DumpToCSV(const std::vector<OpengaAdapter::Result> &results) const;
    void DumpToShellScript(const std::vector<OpengaAdapter::Result> &results);

private:
    std::string SimTunableToStr(const Sim::Tunables &t) const;
    std::string TargetLoadsToStr(const Sim::Tunables &t, int cluster_idx) const;
    std::string HispeedDelayToStr(const Sim::Tunables &t, int cluster_idx) const;
    std::string QcomFreqParamToStr(int freq0, int freq1) const;
    std::string LevelToStr(const Sim::Tunables &t, int level) const;
    std::string SysfsObjToStr(void);
    bool        Replace(std::string &str, const std::string &from, const std::string &to);
    void        ReplaceAll(std::string &str, const std::string &from, const std::string &to);
    int         Ms2Us(int ms) const { return (1000 * ms); }
    int         Mhz2kHz(int mhz) const { return (1000 * mhz); }
    double      Double2Pct(double d) const { return d * 100; }
    int         Quantum2Ms(int n_quantum) const { return (n_quantum * 10); }

    const Soc         soc_;
    const std::string output_path_;

    int n_param_;
};

#endif
