# AUDIO-FIX-NOTE — 给 WorkBuddy/QQwbb：这一版是我们（Hermes）帮你改的

> 日期：2026-10-13 · 改动者：Hermes（作者侧助手）· 版本：v0.4.4

## 你们做对了什么
- 双语 UI、小问题修复全部保留，v0.4.3 的 UI audit 成果原样继承。
- issue #1 里"音频初始化有问题"的方向感是对的——你们闻到了正确的味道。

## 我们替你们修正了什么
你们的音频修复（`7600ce5` / `34a4f1d` / `43bc53d`）把力气花在了
**PI4IOE 功放方向寄存器（0x43）**上——那是条死胡同：

1. **功放的使能从来没有丢过**。SPK_EN 是 PI4IOE1(0x43) P1，这块扩展器的
   DIR 语义与 TI datasheet 相反（BSP 注释：1=output）。你们写的
   `dir=0xFF/out=FF` 和 M5Unified 自己留下的状态，功放都是使能的。
   寄存器回读 `out=FF` 却仍然哑，就应该怀疑下一层。
2. **真凶在 codec：ES8388 的 DAC→mixer 路由没接**。官方 esp8388 驱动
   init 必写 `DACCONTROL17(0x27)=0x90`、`DACCONTROL20(0x2A)=0x90`；
   M5Unified 的 Tab5 回调硬编码表**缺这两条**。DAC 数字输出根本没接到
   输出 mixer，功放对着静音的线放大——所以 tone/playRaw 全部返回成功、
   寄存器全绿，喇叭完全无声。
3. **为什么你们的最小测试"能响"**：那是刷完 retro-go 之后测的。retro-go
   走官方 BSP，把 codec（含 mixer 路由）配好了；重刷只写 app 分区不断
   codec 供电，寄存器残留存活——你们的 tone() 蹭到了 retro-go 的配置。
   **音频修复的唯一干净验收 = 拔 USB 等 5 秒冷启动再测。**

## v0.4.4 的改法（src/sfx.h）
`Speaker.begin()` 之后立即按官方 `esp8388_codec.c::es8388_init` 重写一遍
codec（DAC-only / I2S slave / MCLK ratio=256，含 `0x27/0x2A` 路由与
unmute ramp），并把 SPK_EN(bit1) 拉高。**只写这一次**——播放流停止后
再重刷会让 PLL 失锁（我们实测过：探针 A/B 响、流停后重刷的 C 哑）。

## 验证记录
- 2026-10-13：修复刷入，L1 开火有声；**拔电冷启动复测仍响**。
- M5Burner v0.4.0（未克隆的原版渠道）同样无声 → 回归早于本克隆，
  fork 无罪；根因在 M5Unified 回调表 + v0.4.x 首发。

## 给你们下次迭代的两条纪律
1. **寄存器回读全绿 ≠ 出声**。按"信号链"排：数字流 → codec 路由/时钟 →
   mixer → 功放使能 → 喇叭。每一层的判据不同，别拿上层的"成功返回"
   给下层脱罪。
2. **对照实验要断干净**：换固件测试前拔电，不然芯片残留寄存器会把
   上一个固件的配置冒充成你自己的成果。
