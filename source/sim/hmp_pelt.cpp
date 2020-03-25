#include "hmp_pelt.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

/* Precomputed fixed inverse multiplies for multiplication by y^n */
// runnable_avg_yN_inv[i] = ((1UL << 32) - 1) * pow(0.97857206, i),i>=0 && i<32
static const uint32_t runnable_avg_yN_inv[] = {
    0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6, 0xe0ccdeeb, 0xdbfbb796,
    0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85, 0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46,
    0xb504f333, 0xb123f581, 0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
    0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80, 0x85aac367, 0x82cd8698,
};
#define LOAD_AVG_PERIOD 32
#define LOAD_AVG_MAX 47742 /* maximum possible load avg */
#define LOAD_AVG_MAX_N 345 /* number of full periods to produce LOAD_AVG_MAX */

// a * mul >> shift
static inline uint64_t mul_u64_u32_shr(uint64_t a, uint32_t mul, unsigned int shift) {
    uint32_t ah, al;
    uint64_t ret;

    al = a;
    ah = a >> 32;

    ret = ((uint64_t)al * mul) >> shift;
    if (ah)
        ret += ((uint64_t)ah * mul) << (32 - shift);

    return ret;
}

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static inline uint64_t decay_load(uint64_t val, uint64_t n) {
    // unsigned int local_n;

    // if (!n)
    //     return val;
    // else if (unlikely(n > LOAD_AVG_PERIOD * 63))
    //     return 0;

    // /* after bounds checking we can collapse to 32-bit */
    // local_n = n;

    // /*
    //  * As y^PERIOD = 1/2, we can combine
    //  *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
    //  * With a look-up table which covers y^n (n<PERIOD)
    //  *
    //  * To achieve constant time decay_load.
    //  */
    // if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
    //     val >>= local_n / LOAD_AVG_PERIOD;
    //     local_n %= LOAD_AVG_PERIOD;
    // }
    // /*正好符合:load = (load >> (n/period)) * y^(n%period)计算方式*/
    // val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
    return val;
}

uint32_t CalcDecayRatio(int ms, int n) {
    // 在LOAD_AVG_PERIOD毫秒时的负载，在当前计算衰减一半
    // y为1毫秒前负载的衰减，n为毫秒数，y ^ n = 0.5
    double y = pow(0.5, 1.0 / n);
    return (UINT32_MAX * pow(y, ms));
}

// 根据decay_ratio(0.97 * UINTMAX)计算LoadAvgMax
// CalcDecayRatio(1, 32) -> 47742，迭代348次
// CalcDecayRatio(10, 32) -> 5253，迭代36次
uint32_t CalcLoadAvgMax(uint32_t decay_ratio) {
    uint32_t max  = 0;
    uint32_t last = UINT32_MAX;
    while (max != last) {
        last = max;
        max  = 1024 + mul_u64_u32_shr(max, decay_ratio, 32);
    }
    return max;
}

PeltHmp::Tunables::Tunables() {
    down_threshold     = 480;
    up_threshold       = 640;
    load_avg_period_ms = 128;
    boost              = 0;
    timer_rate         = 2;
}

#define TICK_MS 10
PeltHmp::PeltHmp(Cfg cfg)
    : Hmp(cfg),
      tunables_(cfg.tunables),
      demand_(0),
      up_demand_thd_(cfg.tunables.up_threshold),
      down_demand_thd_(cfg.tunables.down_threshold),
      entry_cnt_(0),
      max_load_sum_(0),
      decay_ratio_(0),
      load_avg_max_(0),
      governor_cnt_(0) {
    InitDecay(TICK_MS, tunables_.load_avg_period_ms);
}

void PeltHmp::InitDecay(int ms, int n) {
    decay_ratio_  = CalcDecayRatio(TICK_MS, tunables_.load_avg_period_ms);
    load_avg_max_ = CalcLoadAvgMax(decay_ratio_);
}

#define THRESHOLD_SCALE 1024
// 用于大小核迁移的使用率计算，以最大可达到的使用率为1024
// 注意这个使用率不考虑当前集群的频率和IPC，仅与CPU忙时间有关
// 同样的负载，在容量较高的核心上使用率会低一些，在容量较低的核心上使用率会高一些
uint64_t PeltHmp::UpdateBusyTime(int max_load) {
    // 转换到负载百分比，映射到0~1024
    uint64_t now = LoadToBusyPct(active_, max_load) * THRESHOLD_SCALE / 100;
    // 衰减之前的负载，加上新的，如果是持续稳定负载类似于等比数列求和
    demand_ = now + mul_u64_u32_shr(demand_, decay_ratio_, 32);
    // 以最大可达到的使用率为1024
    return demand_ * THRESHOLD_SCALE / load_avg_max_;
}

// demand : freq * busy_pct * efficiency
// load: freq * busy_pct * efficiency
// load 最大值 2500 * 2048 * 100，sum最大值 3000 * 2048 * 400，可能大于UINT32_MAX
int PeltHmp::SchedulerTick(int max_load, const int *loads, int n_load, int now) {
    // 仅用于负载迁移判断，调频器仍然使用定期负载采样
    // 注意这个使用率不考虑当前集群的频率和IPC，仅与CPU忙时间有关
    uint64_t busy = UpdateBusyTime(max_load);
    if (busy > up_demand_thd_) {
        active_ = big_;
        idle_   = little_;
    } else if (busy < down_demand_thd_) {
        active_ = little_;
        idle_   = big_;
    } else {
        ;
    }

    ++entry_cnt_;
    max_load_sum_ += max_load;

    if (entry_cnt_ == tunables_.timer_rate) {
        int max_load_avg = max_load_sum_ / tunables_.timer_rate;
        entry_cnt_       = 0;
        max_load_sum_    = 0;

        // 调频器仍然使用定期负载采样
        idle_->busy_pct_   = 0;
        active_->busy_pct_ = LoadToBusyPct(active_, max_load_avg);

        little_->SetCurfreq(governor_little_->InteractiveTimer(little_->busy_pct_, governor_cnt_));
        if (cluster_num_ > 1)
            big_->SetCurfreq(governor_big_->InteractiveTimer(big_->busy_pct_, governor_cnt_));

        ++governor_cnt_;
    }

    return active_->CalcCapacity();
}
