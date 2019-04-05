#ifndef __DUMP_H
#define __DUMP_H

#include <algorithm>
#include <string>
#include <vector>
#include "cpumodel.h"
#include "openga_helper.h"
#include "sim.h"

class Dumper {
public:
    Dumper() = delete;
    Dumper(const Soc &soc, const std::string &output_path) : soc_(soc), output_path_(output_path){};
    void DumpToTXT(const std::vector<OpengaAdapter::Result> &results) const;
    void DumpToCSV(const std::vector<OpengaAdapter::Result> &results) const;

private:
    std::string SimTunable2String(const Sim::Tunables &t) const;
    int         Ms2Us(int ms) const { return (1000 * ms); }
    int         Mhz2kHz(int mhz) const { return (1000 * mhz); }
    double      Double2Pct(double d) const { return d * 100; }
    int         Quantum2Ms(int n_quantum) const { return (n_quantum * 10); }

    const Soc         soc_;
    const std::string output_path_;
};

#endif
