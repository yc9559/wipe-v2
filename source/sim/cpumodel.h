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
    int  FindFreqIdx(int freq, int left, int right) const;
    int  freq_floor_to_idx(int freq) const;
    int  freq_ceiling_to_idx(int freq) const;
    int  freq_floor_to_opp(int freq) const;
    int  freq_ceiling_to_opp(int freq) const;
    int  CalcPower(const int *load_pcts) const;
    int  CalcCapacity(void) const;
    void SetMinfreq(int freq);
    void SetMaxfreq(int freq);
    void SetCurfreq(int freq);

    const Model model_;
    int         cur_freq_;
    int         busy_pct_;

private:
    Cluster();
    int GetOpp(int idx) const;
    int min_opp_idx_;
    int max_opp_idx_;
    int cur_opp_idx_;
};

inline int Cluster::GetOpp(int idx) const {
    return model_.opp_model[idx].freq;
}

// 在给定下标闭区间内，找到 >=@freq的最低频点对应的opp频点序号
inline int Cluster::FindFreqIdx(int freq, int left, int right) const {
    left  = (left == -1) ? 0 : left;
    right = (right == -1) ? (model_.opp_model.size() - 1) : right;
    int i = left;
    // 第1-n个频点，到达第n或者当前频点>=要寻找的即可跳出
    for (; i < right && GetOpp(i) < freq; ++i)
        ;
    return i;
}

// 在最低最高频率范围内，找到 >=@freq的最低频点对应的opp频点序号
inline int Cluster::freq_floor_to_idx(int freq) const {
    return FindFreqIdx(freq, min_opp_idx_, max_opp_idx_);
}

// 在最低最高频率范围内，找到 <=@freq的最大频点对应的opp频点序号
inline int Cluster::freq_ceiling_to_idx(int freq) const {
    int i = FindFreqIdx(freq, min_opp_idx_, max_opp_idx_);
    return (i > 0 && GetOpp(i) > freq) ? (i - 1) : i;
}

// 在最低最高频率范围内，找到 >=@freq的最低频点
inline int Cluster::freq_floor_to_opp(int freq) const {
    return GetOpp(freq_floor_to_idx(freq));
}

// 在最低最高频率范围内，找到 <=@freq的最大频点
inline int Cluster::freq_ceiling_to_opp(int freq) const {
    return GetOpp(freq_ceiling_to_idx(freq));
}

inline void Cluster::SetMinfreq(int freq) {
    min_opp_idx_ = FindFreqIdx(freq, -1, -1);
    if (cur_freq_ < freq)
        SetCurfreq(freq);
}

inline void Cluster::SetMaxfreq(int freq) {
    max_opp_idx_ = FindFreqIdx(freq, -1, -1);
    if (cur_freq_ > freq)
        SetCurfreq(freq);
}

inline void Cluster::SetCurfreq(int freq) {
    cur_opp_idx_ = freq_floor_to_idx(freq);
    cur_freq_    = GetOpp(cur_opp_idx_);
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

    int GetLittleClusterIdx(void) const { return 0; }
    int GetBigClusterIdx(void) const { return clusters_.size() - 1; }

    int GetEnoughCapacity(void) const {
        return (clusters_.back().model_.max_freq * clusters_.back().model_.efficiency * enough_capacity_pct_);
    }

    int GetMaxCapacity(void) const {
        return (clusters_.back().model_.max_freq * clusters_.back().model_.efficiency * 98);
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
