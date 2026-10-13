# AUDIO 排障交接（已完结 · v0.4.4）

## 根因（已定案，真机验证）
ES8388 的 **DAC→mixer 路由寄存器**（0x27=0x90 / 0x2A=0x90）在
M5Unified Tab5 speaker 回调的硬编码表里**缺失**。芯片数据手册上这两个
寄存器默认关闭——DAC 数字输出根本没有接到输出 mixer，功放自然无声。

- v0.4.x（含 M5Burner 0.4.0）冷启动必静音；
- "有时能响"是假象：刷 retro-go / UserDemo 等官方 BSP 固件后 codec
  寄存器被正确写入，重刷不断电则残留存活——我们的固件恰好蹭到了；
- 一切"寄存器回读全绿"都正常，因为 I2C 写读通路本来就是好的。

## 修复（v0.4.4，src/sfx.h）
`Speaker.begin()` 之后立即按官方 esp8388_codec init 序列重写一遍
codec（DAC-only / I2S slave / ratio=256），含 0x27/0x2A 路由与
unmute ramp，并把 PI4IOE SPK_EN(bit1) 拉高。只写这一次：
播放流停止后不要重刷（曾致 PLL 状态异常，探针 C 段实测）。

## 走过的弯路（引以为戒）
- workbuddy v1/v2：误判 PI4IOE 功放方向（0x3F vs 0xFF）——扩展器
  OUT bit1 一直是高的，功放使能从未丢；
- 逐发 playRaw 日志 + force=true：保留（防齐射丢帧），但与根因无关；
- 回退"v0.4.x 原始路径"：无效——因为 v0.4.x 本来就是哑的
  （M5Burner 版同样无声，是本案最强证据）。

## 验证记录
- 2026-10-13：官方序列刷入后用户开火有声（"有声音了！哈哈！"）；
- 冷启动拔电复测：见 CHANGELOG v0.4.4 条目（通过后方算关单）。
