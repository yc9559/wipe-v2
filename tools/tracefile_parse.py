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

# 提取cpu c-state信息
r_idle = r'^ {10}<.{20}\[(\d{3})\].{6}(\d+.\d{6}): cpu_idle: state=(\d{1,10}) '
re_idle = re.compile(r_idle, re.MULTILINE)

def parse_cstate(systrace, start_time_sec):
    t = re_idle.findall(systrace)
    idle_trace = [(int(cpuid), float(time), int(state)) for cpuid, time, state in t]

    # https://www.kernel.org/doc/Documentation/trace/events-power.txt
    # cpu_idle		"state=%lu cpu_id=%lu"
    # Note: the value of "-1" or "4294967295" for state means an exit from the current state,
    CSTATE_EXIT     = 4294967295
    # 准备进入cstate的犹豫时间，40us来自/sys/devices/system/cpu/cpu4/cpuidle/state0/latency
    WAIT_TO_SEE_SEC = 0.000040
    # cstate = 0 -> C1, cstate = 2 -> C3
    is_busy         = lambda cstate: cstate == CSTATE_EXIT
    cstates         = [999] * n_cpu
    cstates_start   = [start_time_sec] * n_cpu
    busy_durations  = [0.0] * n_cpu
    busy_seq        = []
    window_start    = start_time_sec

    for cpuid, time, state in idle_trace:
        # quantum时间截止，回写cpu busy时间
        while time - window_start > quantum_sec:
            window_start += quantum_sec
            for i in range(n_cpu):
                if is_busy(cstates[i]):
                    busy_durations[i] = busy_durations[i] + window_start - cstates_start[i]
                cstates_start[i] = window_start
            busy_seq.append(tuple(busy_durations))
            for i in range(n_cpu):
                busy_durations[i] = 0.0

        if is_busy(cstates[cpuid]):
            delta = time - cstates_start[cpuid] - WAIT_TO_SEE_SEC
            busy_durations[cpuid] = max(0.0, busy_durations[cpuid] + delta)
        cstates[cpuid] = state
        cstates_start[cpuid] = time

    # 按照window_sec长度聚集负载
    n_quantum = int(window_sec / quantum_sec)
    windowed_load_seq = []
    sum_busy = [0.0] * n_cpu
    for idx in range(len(busy_seq)):
        for cpuid in range(n_cpu):
            sum_busy[cpuid] += busy_seq[idx][cpuid]
        # 如果在循环结束时有不完整的windowed_load，丢弃
        if idx % n_quantum == n_quantum - 1:
            windowed_loads = [busy_ratio_to_load(busy / window_sec) for busy in sum_busy]
            windowed_load_seq.append(windowed_loads)
            sum_busy = [0.0] * n_cpu

    # 传递给interactive调速器的会是集群中负载最大的数字
    max_load_seq = [max(loads[trace_cpuid_low: trace_cpuid_high+1]) for loads in windowed_load_seq]

    return max_load_seq, windowed_load_seq, busy_seq


# 提取触摸响应，需要quantum时间进行聚集
r_input = r'^.{20,45} (\d{1,10}.\d{6}):.+?\|pokeUserActivity'
re_input = re.compile(r_input, re.MULTILINE)

def parse_input(systrace, start_time_sec):
    t = re_input.findall(systrace)
    input_trace = [(float(x) - start_time_sec) for x in t]
    ret = set([int(time / window_sec) * window_sec for time in input_trace])
    ret = sorted(list(ret))
    return ret


# 提取渲染帧起始时间，ui_thread为帧事件起始时间，按照input事件响应的app进行跟踪
# 后台不交互的也会有ui渲染线程在活动，频率甚至可以很高(16ms)，我们只关注前台用户正在使用的进程
r_ui_thread = r'^.{20,45} (\d{1,10}.\d{6}).{23}\|(\d{3,6})\|Choreographer#doFrame'
re_ui_thread = re.compile(r_ui_thread, re.MULTILINE)
r_input_ui = r'^.{20,45} (\d{1,10}.\d{6}).{23}\|(\d{3,6})\|deliverInputEvent'
re_input_ui = re.compile(r_input_ui, re.MULTILINE)

def parse_render(systrace, start_time_sec):
    t = re_ui_thread.findall(systrace)
    ui_trace = [(float(time) - start_time_sec, int(pid)) for time, pid in t]
    t = re_input_ui.findall(systrace)
    ui_input_trace = [(float(time) - start_time_sec, int(pid)) for time, pid in t]

    # 提取当前交互在哪个进程发生，记录开始时间和进程PID
    input_log = []
    if len(ui_input_trace) > 0:
        input_log.append((0.0, ui_input_trace[0][1]))
        for time, pid in ui_input_trace:
            t = time - input_log[-1][0]
            # 至少等待16ms再切换当前交互的进程
            if not pid == input_log[-1][1] and t > frame_sec:
                input_log.append((time, pid))
        input_log.append((999999, 0))

    # 提取帧渲染开始的时间，用于辅助流畅度评分
    ret = []
    idx_input_log = 0
    prev_time = 0.0
    for time, pid in ui_trace:
        # 按照之前的结果，跳过并非前台交互程序的UI起始标志
        if input_log:
            if time > input_log[idx_input_log + 1][0]:
                idx_input_log += 1
            if not pid == input_log[idx_input_log][1]:
                continue
        delta = time - prev_time
        # 避免帧渲染请求太过重叠，进行没必要的卡顿评测
        # 跳过断断续续渲染请求的第一帧，通常这个负载相比之前的变化很大，不算在卡顿评测内
        if delta > 0.6 * frame_sec and delta < 5 * frame_sec:
            ret.append(int(round(time / quantum_sec)) * quantum_sec)
        prev_time = time
    return ret


# 按照DOM标签提取原始trace信息
r_tracedata = r'^  <script class=\"trace-data\" type=\"application/text\">(.+?)  </script>'
re_tracedata = re.compile(r_tracedata, re.MULTILINE | re.DOTALL)

# 提取起始时间戳
r_starttime = r'^.{42}(\d{1,10}.\d{6}):'
re_starttime = re.compile(r_starttime, re.MULTILINE)

def parse_trace(input_file):
    print('processing', input_file)
    trace_datas = None
    with open(input_file, 'r') as f:
        content = f.read()
        trace_datas = re_tracedata.findall(content)

    if not trace_datas:
        print('load err')
        return

    # trace-data 标签数据块共有3个，第一个是进程信息，第二个是systrace，第三个是录制trace的命令行
    systrace = trace_datas[1]
    start_time_sec = float(re_starttime.findall(systrace, 0, 2000)[0])

    # 原始数据预处理
    max_load_seq, windowed_load_seq, busy_seq = parse_cstate(systrace, start_time_sec)
    input_seq = parse_input(systrace, start_time_sec)
    render_seq = parse_render(systrace, start_time_sec)

    # CPU4-7的最大负载，CPU4负载，CPU5负载，CPU6负载，CPU7负载，是否有触摸事件
    packed_loads_seq = []
    input_quantum_set = set([int(ftime / quantum_sec) for ftime in input_seq])
    n_window_quantum = int(window_sec / quantum_sec)
    time_quantum = 0.0
    for max_load, loads in zip(max_load_seq, windowed_load_seq):
        has_input_event = int(time_quantum in input_quantum_set)
        time_quantum += n_window_quantum 
        packed_loads = [max_load, ]
        packed_loads.extend(loads[trace_cpuid_low: trace_cpuid_high + 1])
        packed_loads.append(has_input_event)
        packed_loads_seq.append(packed_loads)

    # 注意：16ms的帧可以横跨三个10ms的窗口
    # 帧在第x个ms开始，帧内16ms的在各个核心的负载的最大值
    packed_render_loads_seq = []
    len_busy_seq = len(busy_seq)
    n_frame_quantum = int(frame_sec / quantum_sec)
    for render_start_time in render_seq:
        idx_start = int(render_start_time / quantum_sec)
        idx_end = idx_start + n_frame_quantum
        if idx_end > len_busy_seq:
            break
        period_sum_busy = [0.0] * n_cpu
        for idx_busy in range(idx_start, idx_end):
            for cpuid in range(trace_cpuid_low, trace_cpuid_high + 1):
                period_sum_busy[cpuid] += busy_seq[idx_busy][cpuid]
        period_max_load = busy_ratio_to_load(max(period_sum_busy) / frame_sec)
        packed_render_loads_seq.append((idx_start, period_max_load))

    return packed_loads_seq, packed_render_loads_seq


def merge_packed_seq(packed_loads_seq_arr, packed_render_loads_seq_arr):
    merged_loads = list()
    merged_renders = list()
    idx_quantum_base = 0
    for packed_loads_seq, packed_render_loads_seq in zip(packed_loads_seq_arr, packed_render_loads_seq_arr):
        merged_loads.extend(packed_loads_seq)
        merged_renders.extend( [ (idx_start + idx_quantum_base, period_max_load) for idx_start, period_max_load in packed_render_loads_seq] )
        # 由于负载序列生成时，最后不满10ms也就是一个window窗长的负载会被丢弃
        # 负载序列在尾部拼接完毕后，根据当前负载序列结束时的quantum(ms)数，来设置下一个渲染需求序列的quantum(ms)的序号偏移
        idx_quantum_base += len(packed_loads_seq) * int(window_sec / quantum_sec)
    return merged_loads, merged_renders

def parse_load_set(set_path, out_path, sector_key):
    info_filename = 'info.json'
    info = None
    with open(set_path + info_filename, 'r') as filename:
        info = json.load(filename)
    sector = info[sector_key]
    
    if len(sector['loadSeq']):
        todos = sector['loadSeq']
    else:
        print("ERROR: " + sector_key + ".loadSeq is empty")
        exit(-1)

    name_arr = list()
    packed_loads_seq_arr = list()
    packed_render_loads_seq_arr = list()

    for filename in todos:
        packed_loads_seq, packed_render_loads_seq = parse_trace(set_path + filename)
        name_arr.append(filename)
        packed_loads_seq_arr.append(packed_loads_seq)
        packed_render_loads_seq_arr.append(packed_render_loads_seq)
        packed_data = {
            'src':              [filename,],
            'ver':              1,
            'quantumSec':       quantum_sec,
            'windowQuantum':    int(window_sec / quantum_sec),
            'frameQuantum':     int(frame_sec / quantum_sec),
            'efficiencyA53':    1024,
            'efficiency':       info['efficiency'],
            'freq':             info['freq'],
            'loadScale':        load_scale,
            'coreNum':          trace_cpuid_high - trace_cpuid_low + 1,
            'windowedLoadLen':  len(packed_loads_seq),
            'windowedLoad':     packed_loads_seq,
            'renderLoad':       packed_render_loads_seq
        }
        with open(out_path + filename[:-5] + '.json', 'w') as f:
            json.dump(packed_data, f, indent=None, separators=(',', ':'))

    if need_merged_file:
        merged_loads, merged_renders = merge_packed_seq(packed_loads_seq_arr, packed_render_loads_seq_arr)
        packed_data = {
            'src':              name_arr,
            'ver':              1,
            'quantumSec':       quantum_sec,
            'windowQuantum':    int(window_sec / quantum_sec),
            'frameQuantum':     int(frame_sec / quantum_sec),
            'efficiencyA53':    1024,
            'efficiency':       info['efficiency'],
            'freq':             info['freq'],
            'loadScale':        load_scale,
            'coreNum':          trace_cpuid_high - trace_cpuid_low + 1,
            'windowedLoadLen':  len(merged_loads),
            'windowedLoad':     merged_loads,
            'renderLoad':       merged_renders
        }
        with open(out_path + sector_key + '-merged' + '.json', 'w') as f:
            json.dump(packed_data, f, indent=None, separators=(',', ':'))
    return


# todos = [
#     # raw_path + 'bili-danmu.html', 
#     'gflops.html', 
#     # 'trace.html'
# ]

parse_load_set(raw_path, out_path, "onscreen")
parse_load_set(raw_path, out_path, "offscreen")
# parse_trace(todos[0])
