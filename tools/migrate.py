import re
import os
import json
import argparse

need_merged_file = True
raw_path = '../dataset/workload/osborn/raw/'
out_path = '../dataset/workload/osborn/'
n_cpu = 8
trace_cpuid_low = 4
trace_cpuid_high = 7
# window_sec = 0.02 # 20ms timer_rate
window_sec = 0.01   # 负载聚合到10ms，以适配10ms，20ms，30ms...的timer_rate
quantum_sec = 0.001 # 解析序列的最小时间单位，我们不在意事件在1.2ms还是1.4ms发生的，1ms
frame_sec = 0.016   # 1帧的时长，16.7ms归到16ms

merged_load_seq = list()
load_shift = 7      
load_scale = 100    # 满负载是100
busy_ratio_to_load = lambda busy_ratio: int(round(busy_ratio * load_scale))

info_filename = 'info.json'
info = None
with open(raw_path + info_filename, 'r') as filename:
    info = json.load(filename)

# 采集设备是 nexus 9
idle_efficiency = 1638
idle_freq = 2014

idle_load_seq = list()
with open('standby_load_20180308_from_171023.csv', 'r') as f:
    for line in f:
        idle_load_seq.append(min(100, int(line) + 10))

# 全长1000，取前800
windowed_idle_load_seq = list()
for demand in idle_load_seq[:800]:
    windowed_idle_load_seq.append([demand, demand, demand, demand, demand, 0])

# 加一个触摸事件
windowed_idle_load_seq[400][-1] = 1

packed_data = {
    'src':              'standby_load_20180308_from_171023.csv',
    'ver':              1,
    'quantumSec':       quantum_sec,
    'windowQuantum':    int(window_sec / quantum_sec),
    'frameQuantum':     int(frame_sec / quantum_sec),
    'efficiencyA53':    1024,
    'efficiency':       info['efficiency'],
    'freq':             info['freq'],
    'loadScale':        load_scale,
    'coreNum':          trace_cpuid_high - trace_cpuid_low + 1,
    'windowedLoadLen':  len(windowed_idle_load_seq),
    'windowedLoad':     windowed_idle_load_seq,
    'renderLoad':       []
}
with open(out_path + 'idle' + '.json', 'w') as f:
    json.dump(packed_data, f, indent=None, separators=(',', ':'))

