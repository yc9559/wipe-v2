# 本文件夹负载序列原始数据说明

## 采集systrace

1. 配置手机的HMP和CPU调速器
   1. `sched_boost` = 1
   2. 大核心的CPU调速器 = `performance`
2. 开启设备的ADB
3. 点击[这里](https://developer.android.com/studio/releases/platform-tools)下载`platform-tools`，配置完成后执行

```bash
python systrace.py -t 30 input idle view -o xxxxx.html
```
## 采集设备

坚果 Pro 2  
sdm660 A73 x 4 @2208mhz  
同频性能：1638(A53为1024)  

## 本文件夹采集的负载情景

每段大约30s

- 微信 朋友圈
- 微信 打字
- 微信 选择图片
- 微信 公众号
- 微信 小程序
- QQ 打字 滑动
- QQ 空间动态
- bili 信息流
- bili 1080+弹幕
- share 信息流
- 贴吧 wp7吧
- 酷安 首页
- 美团 搜索饭店
- via 浏览apple官网
- 七日之都 1-1
- 七日之都 小boss
- 淘宝 选择小米移动电源
- 闲鱼 搜索游戏主机
- 百度地图 搜索世纪大道
- Twitter 信息流
- 频繁多任务切换
