#ifndef __WORKLOAD_H
#define __WORKLOAD_H

#include <stdint.h>
#include <string>
#include <vector>

class Workload {
public:
    typedef struct _LoadSlice {
        int max_load;
        int load[4];
        int has_input_event;
    } LoadSlice;

    typedef struct _RenderSlice {
        int window_idxs[3];
        int window_quantums[3];
        int frame_load;
    } RenderSlice;

    Workload(const std::string &workload_file);

    std::vector<LoadSlice>   windowed_load_;
    std::vector<RenderSlice> render_load_;
    std::vector<std::string> src_;
    float                    quantum_sec_;
    int                      window_quantum_;
    int                      frame_quantum_;
    int                      efficiency_;
    int                      freq_;
    int                      load_scale_;
    int                      core_num_;

private:
    Workload();
};

#endif