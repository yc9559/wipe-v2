#include "workload.h"

#include <fstream>
#include <iostream>

#include "json.hpp"

Workload::Workload(const std::string &workload_file) {
    std::ifstream ifs(workload_file);
    if (!ifs.good()) {
        using namespace std;
        cout << "Workload access ERROR: " << workload_file << endl;
        throw runtime_error("file access error");
    }

    nlohmann::json j;
    ifs >> j;
    quantum_sec_    = j["quantumSec"];
    window_quantum_ = j["windowQuantum"];
    frame_quantum_  = j["frameQuantum"];
    efficiency_     = j["efficiency"];
    freq_           = j["freq"];
    load_scale_     = j["loadScale"];
    core_num_       = j["coreNum"];

    for (const auto &src_name : j["src"]) {
        src_.push_back(src_name);
    }

    auto loadpct_to_demand = [=](int load) { return kWorkloadScaleFactor * freq_ * efficiency_ * load; };

    if (j["renderLoad"].size() == 0) {
        using namespace std;
        cout << "renderLoad is empty: " << workload_file << endl;
        throw runtime_error("renderLoad is empty");
    }

    auto next_win_q = [=](int q) { return (q / window_quantum_ + 1) * window_quantum_; };
    render_load_.reserve(j["renderLoad"].size());
    for (const auto &render_demand : j["renderLoad"]) {
        RenderSlice r;
        memset(&r, 0, sizeof(RenderSlice));

        int begin_q = render_demand[0];
        int end_q   = begin_q + frame_quantum_;
        int idx_rec = 0;
        int left_q  = begin_q;
        int right_q = next_win_q(begin_q);
        while (left_q != right_q) {
            r.window_idxs[idx_rec]     = left_q / window_quantum_;
            r.window_quantums[idx_rec] = right_q - left_q;
            left_q                     = right_q;
            right_q                    = std::min(end_q, next_win_q(right_q));
            idx_rec++;
        }
        r.frame_load = loadpct_to_demand(render_demand[1]);

        render_load_.push_back(r);
    }

    if (j["windowedLoad"].size() == 0) {
        using namespace std;
        cout << "windowedLoad is empty: " << workload_file << endl;
        throw runtime_error("windowedLoad is empty");
    }

    auto has_render = [&](int idx) {
        for (const auto &r : render_load_) {
            if (r.window_idxs[0] == idx || r.window_idxs[1] == idx || r.window_idxs[2] == idx)
                return true;
        }
        return false;
    };

    windowed_load_.reserve(j["windowedLoad"].size());
    for (const auto &slice : j["windowedLoad"]) {
        LoadSlice l;
        memset(&l, 0, sizeof(LoadSlice));

        l.max_load = loadpct_to_demand(slice[0]);
        for (int idx = 0; idx < core_num_; ++idx) {
            l.load[idx] = loadpct_to_demand(slice[idx + 1]);
        }
        l.has_input_event = slice[core_num_ + 1];
        l.has_render      = has_render(windowed_load_.size());

        windowed_load_.push_back(l);
    }
}
