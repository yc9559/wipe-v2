#ifndef __CPU_MODEL_H
#define __CPU_MODEL_H

#include <stdint.h>
#include <string>
#include <vector>

class Cluster {
public:
    typedef struct _Pwr {
        int16_t freq;
        int16_t core_power;
        int16_t cluster_power;
    } Pwr;

    typedef struct _ClusterModel {
        int              min_freq;
        int              max_freq;
        int              efficiency;
        int              core_num;
        std::vector<Pwr> opp_model;
    } Model;

    Cluster(Model model);
    int  FindIdxWithFreqFloor(int freq, int start_idx) const;
    int  freq_floor_to_idx(int freq) const;
    int  freq_ceiling_to_idx(int freq) const;
    int  freq_floor_to_opp(int freq) const;
    int  freq_ceiling_to_opp(int freq) const;
    int  CalcPower(const int *load_pcts) const;
    int  CalcCapacity(void) const;
    void SetMinfreq(int freq);
    void SetCurfreq(int freq);

    const Model model_;
    int         cur_freq_;
    int         busy_pct_;

private:
    Cluster();
    int min_opp_idx_;
    int cur_opp_idx_;
};

// 从start_idx开始，找到 >=@freq的最低频点对应的opp频点序号
inline int Cluster::FindIdxWithFreqFloor(int freq, int start_idx) const {
    uint32_t i       = start_idx;
    uint32_t uplimit = model_.opp_model.size() - 1;
    // 第1-n个频点，到达第n或者当前频点>=要寻找的即可跳出
    for (; i < uplimit && model_.opp_model[i].freq < freq; ++i)
        ;
    return i;
}

// 从设定的最低频开始，找到 >=@freq的最低频点对应的opp频点序号
inline int Cluster::freq_floor_to_idx(int freq) const {
    return FindIdxWithFreqFloor(freq, min_opp_idx_);
}

// 从设定的最低频开始，找到 <=@freq的最大频点对应的opp频点序号
inline int Cluster::freq_ceiling_to_idx(int freq) const {
    uint32_t i = min_opp_idx_ + 1;
    // 第2-n个频点，到达第n+1或者当前频点>要寻找的即可跳出
    for (; i < model_.opp_model.size() && model_.opp_model[i].freq <= freq; ++i)
        ;
    // 取该频点左边那个，使得频点范围落在第1-n，左边频点<=要寻找的
    return (i - 1);
}

// 从设定的最低频开始，找到 >=@freq的最低频点
inline int Cluster::freq_floor_to_opp(int freq) const {
    return model_.opp_model[freq_floor_to_idx(freq)].freq;
}

// 从设定的最低频开始，找到 <=@freq的最大频点
inline int Cluster::freq_ceiling_to_opp(int freq) const {
    return model_.opp_model[freq_ceiling_to_idx(freq)].freq;
}

inline void Cluster::SetMinfreq(int freq) {
    min_opp_idx_ = FindIdxWithFreqFloor(freq, 0);
    if (cur_freq_ < freq) {
        SetCurfreq(freq);
    }
    return;
}

inline void Cluster::SetCurfreq(int freq) {
    cur_opp_idx_ = freq_floor_to_idx(freq);
    cur_freq_    = model_.opp_model[cur_opp_idx_].freq;
    return;
}

// 耗电量 = 功耗(mw) * 占用率(最大100)
inline int Cluster::CalcPower(const int *load_pcts) const {
    int pwr      = model_.opp_model[cur_opp_idx_].cluster_power * 100;
    int core_pwr = model_.opp_model[cur_opp_idx_].core_power;
    for (int i = 0; i < model_.core_num; ++i) {
        pwr += core_pwr * load_pcts[i];
    }
    return pwr;
}

inline int Cluster::CalcCapacity() const {
    return (cur_freq_ * model_.efficiency * 100);
}

class Soc {
public:
    // 多核心模式
    typedef enum _IntraType { kSMP = 0, kASMP } IntraType;
    // 使用的调度器类型
    typedef enum _SchedType { kLegacy = 0, kWalt, kPelt } SchedType;

    Soc(const std::string &model_file);
    ~Soc(){};
    IntraType GetIntraType(void) const { return intra_type_; }
    SchedType GetSchedType(void) const { return sched_type_; }
    bool      GetInputBoostFeature(void) const { return input_boost_; }
    int       GetEnoughCapacity(void) const {
        return (clusters_.back().model_.max_freq * clusters_.back().model_.efficiency * enough_capacity_pct_);
    }

    std::string          name_;
    std::vector<Cluster> clusters_;

private:
    Soc();

    IntraType intra_type_;
    SchedType sched_type_;
    bool      input_boost_;
    int       enough_capacity_pct_;  // 提供的容量大于SOC最大容量xx%的跳过卡顿判断
};

#endif
