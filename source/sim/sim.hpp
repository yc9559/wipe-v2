#ifndef __SIM_H
#define __SIM_H

#include <stdint.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "cpumodel.h"
#include "sim_types.h"
#include "workload.h"

// 仿真运行
template <typename GovernorType, typename SchedType, typename BoostType>
class Sim {
public:
#define POWER_SHIFT 4

    typedef struct _Tunables {
        typename GovernorType::Tunables governor[2];
        typename SchedType::Tunables    sched;
        typename BoostType::Tunables    boost;
    } Tunables;

    typedef struct _MiscConst {
        int working_base_mw;
        int idle_base_mw;
    } MiscConst;

    Sim() = delete;
    Sim(const Tunables &tunables, const MiscConst &misc) : tunables_(tunables), misc_(misc){};

    // 仿真运行，得到亮屏考察每一时间片的性能输出和功耗，以及灭屏的总耗电
    void Run(const Workload &workload, const Workload &idleload, Soc soc, SimResultPack *rp) {
        // 常量计算
        const int cl_big_idx    = soc.clusters_.size() - 1;
        const int base_pwr      = misc_.working_base_mw * 100;
        const int idle_base_pwr = misc_.idle_base_mw * 100;

        // 使用参数实例化CPU调速器仿真
        auto little_governor = GovernorType(tunables_.governor[0], &soc.clusters_[0]);
        auto big_governor    = GovernorType(tunables_.governor[cl_big_idx], &soc.clusters_[cl_big_idx]);

        // 使用参数实例化调度器仿真
        typename SchedType::Cfg sched_cfg;
        sched_cfg.tunables        = tunables_.sched;
        sched_cfg.little          = &soc.clusters_[0];
        sched_cfg.big             = &soc.clusters_[cl_big_idx];
        sched_cfg.governor_little = &little_governor;
        sched_cfg.governor_big    = &big_governor;
        SchedType sched(sched_cfg);

        // 使用参数实例化输入升频
        auto boost = BoostType(tunables_.boost);
        if (soc.GetInputBoostFeature() == false)
            boost = BoostType();

        int quantum_cnt = 0;
        int capacity    = soc.clusters_[0].CalcCapacity();

        // 亮屏考察每一时间片的性能输出和功耗
        auto &capacity_log = rp->onscreen.capacity;
        auto &power_log    = rp->onscreen.power;
        for (Workload::LoadSlice w : workload.windowed_load_) {
            AdaptLoad(w.max_load, capacity);
            AdaptLoad(w.load, workload.core_num_, capacity);
            capacity_log.push_back(capacity);
            power_log.push_back(base_pwr + sched.CalcPower(w.load));

            boost.HandleInput(soc, w.has_input_event, quantum_cnt);
            capacity = sched.SchedulerTick(w.max_load, w.load, workload.core_num_, quantum_cnt);
            quantum_cnt++;
        }

        // 灭屏只计算耗电总和，不考察是否卡顿
        rp->offscreen_pwr = idle_base_pwr * idleload.windowed_load_.size();
        for (Workload::LoadSlice w : idleload.windowed_load_) {
            AdaptLoad(w.max_load, capacity);
            AdaptLoad(w.load, idleload.core_num_, capacity);
            rp->offscreen_pwr += sched.CalcPowerForIdle(w.load);

            boost.HandleInput(soc, w.has_input_event, quantum_cnt);
            capacity = sched.SchedulerTick(w.max_load, w.load, idleload.core_num_, quantum_cnt);
            quantum_cnt++;
        }

        return;
    }

private:
    // 根据当前性能输出限幅输入的性能需求，不可能输入高于100%的负载
    void AdaptLoad(int &load, int capacity) const { load = std::min(load, capacity); }
    // 根据当前性能输出限幅输入的性能需求，不可能输入高于100%的负载
    void AdaptLoad(int *loads, int n_loads, int capacity) const {
        for (int i = 0; i < n_loads; ++i) {
            loads[i] = std::min(loads[i], capacity);
        }
    }

    Tunables  tunables_;
    MiscConst misc_;
};

#endif
