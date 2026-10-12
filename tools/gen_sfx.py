#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
# gen_sfx.py — 程序化合成全部音效 PCM，生成 src/sfx_data.h
# 哲学同 pixel_font：零音频文件资源，构建期合成、编译进 Flash。
# Tab5 喇叭频响只有 ~200Hz–6kHz：音色全部避开低音，靠中高频+包络做冲击力。
import math, struct

SR = 48000                      # codec 原生率：Tab5 I2S 时钟在 begin 时钉死
                                # （P4 driver-managed clk，运行时不改速率）→ 直接按原生合成
def env_ad(n, a=0.02):         # 快起音+指数衰减包络
    return [min(1.0, i / max(1, n * a)) * math.exp(-3.5 * i / n) for i in range(n)]

def env_hit(n):                # 硬起音+线性衰减（打击感）
    return [1.0 - i / n for i in range(n)]

# xorshift LFSR：确定性伪随机噪声（每次构建产物一致，可复算校验）
class Noise:
    def __init__(self, seed=0xACE1): self.s = seed & 0xFFFFFFFF or 1
    def f(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF; s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF;  self.s = s
        return ((s >> 16) / 32768.0) * 2 - 1

def mix(*ws):                  # 加权混合归一：[(w, wave), ...]
    n = max(len(w) for _, w in ws)
    out = [0.0] * n
    for wt, w in ws:
        for i, v in enumerate(w): out[i] += wt * v
    peak = max(1e-6, max(abs(v) for v in out))
    return [v / peak for v in out]

def tone(f0, f1, n, saw=False):        # 扫频音（f0→f1 Hz）
    out, ph = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * i / max(1, n - 1)
        ph = (ph + f / SR) % 1.0
        out.append(2 * ph - 1 if saw else math.sin(2 * math.pi * ph))
    return out

def burst(n, lp=0.35):                 # 噪声+一阶低通（lp越小越闷）
    nz, x = Noise(), 0.0
    out = []
    for _ in range(n):
        x += lp * (nz.f() - x)          # 一阶IIR：无FFT、构建期也够快
        out.append(x)
    return out

def sq(f, n):                        # 方波（警报/号角用，喇叭上穿透力强）
    out, ph = [], 0.0
    for i in range(n):
        ph = (ph + f / SR) % 1.0
        out.append(1.0 if ph < 0.5 else -1.0)
    return out

def seg_concat(segs):              # [(wave, gain), ...] 串接
    out = []
    for w, g in segs: out += [v * g for v in w]
    return out

SFX = {}
# 每音效烘焙响度（"不能太吵但不能没有"的落实处）：常驻音压到最低档，
# 事件大音效才给满幅。混在一起时自然分层，不需要运行时音量旋钮。
GAIN = {"GUN":.45, "GUN2":.45, "GUN3":.45,   # v0.4.2：机枪三音色（同响度，不同基频）
        "CANNON":.70, "BOOM":1.00, "LASER":.60, "FROST":.35,
        "COIN":.80, "HIT":.25, "LEAK":.70, "CLEAR":.90, "ROTOR":.40,
        "HORN":.85, "EMP":.75}     # v0.4：技能音——比爆炸轻、比tick响（事件感）
def add(name, wave, note):
    n = len(wave)
    peak = max(1e-9, max(abs(v) for v in wave)) or 1
    g = GAIN[name] / peak * 0.92
    pcm = [max(-32768, min(32767, int(v * 32767 * g))) for v in wave]
    SFX[name] = (pcm, note)

# ── 音色表（全部避开 <200Hz；短、脆、不糊）──────────────────────
# 机枪"哒"：25ms 中高频噪声+1.8k点击
w = [a * b for a, b in zip(burst(int(.025*SR), .5), env_ad(int(.025*SR), .1))]
add("GUN", mix((.7, w), (.6, tone(1800, 900, int(.02*SR)))), "机枪：超短亮噪哒")
# v0.4.2：机枪音色变体（同结构、基频 ×1.25 / ×1.5）——满场十二座枪
# 不再是一个音高的节拍器。零新素材：只是 tone() 的频率参数。
add("GUN2", mix((.7, w), (.6, tone(2250, 1125, int(.02*SR)))), "机枪变体：×1.25 基频")
add("GUN3", mix((.7, w), (.6, tone(2700, 1350, int(.02*SR)))), "机枪变体：×1.5 基频")

# 加农开火"咚"：60ms 中频腔体感（250Hz方波+噪声）
n1 = int(.06 * SR)
add("CANNON", mix((.8, [a*b for a,b in zip(sq(240,n1), env_ad(n1,.08))]),
                  (.5, [a*b for a,b in zip(burst(n1,.4), env_ad(n1,.08))])),
    "加农：中频闷咚")

# 爆炸"轰+碎"：250ms 噪声滚降 + 碎裂尾
n2 = int(.25 * SR)
body   = [a*b for a,b in zip(burst(n2, .18), env_hit(n2))]
n3 = int(.12 * SR)
debris = [a*b for a,b in zip(burst(n3, .55), env_ad(n3, .05))]
add("BOOM", mix((1.0, body), (.45, seg_concat([(debris,.8)]))), "爆炸：轰隆+亮碎片尾")

# 激光"咻"：120ms 锯齿扫频 3500→800
n4 = int(.12 * SR)
add("LASER", [a*b for a,b in zip(tone(3500, 800, n4, saw=True), env_ad(n4,.15))],
    "激光：下滑锯齿咻")

# 冰霜"叮铃"：150ms 双高音稀疏
n5 = int(.15 * SR)
add("FROST", mix((.6, [a*b for a,b in zip(tone(2400,2100,n5), env_ad(n5,.3))]),
                 (.4, [a*b for a,b in zip(tone(3600,3200,n5), env_ad(n5,.4))])),
    "冰霜：高音叮铃")

# 能量产金"叮咚♪"：两音铃声（最讨喜，必须有）
a1 = [v*e for v,e in zip(tone(1318,1318,int(.09*SR)), env_ad(int(.09*SR),.2))]
a2 = [v*e for v,e in zip(tone(1760,1760,int(.14*SR)), env_ad(int(.14*SR),.2))]
add("COIN", seg_concat([(a1,1.0),(a2,.9)]), "能量塔：叮咚两连音")

# 击中 tick：12ms 窄点击（常驻最低音量位）
n6 = int(.012 * SR)
add("HIT", [a*b for a,b in zip(tone(1200,700,n6), env_hit(n6))], "击中：轻tick")

# 基地挨打警报：440/330 方波双音
r1 = [v*e for v,e in zip(sq(440,int(.12*SR)), env_ad(int(.12*SR),.1))]
r2 = [v*e for v,e in zip(sq(330,int(.18*SR)), env_ad(int(.18*SR),.1))]
add("LEAK", seg_concat([(r1,1.0),(r2,.9)]), "漏怪：低沉双音警报")

# 通关号角：523/659/784 上行三音
h = lambda f,d: [v*e for v,e in zip(sq(f,int(d*SR)), env_ad(int(d*SR),.15))]
add("CLEAR", seg_concat([(h(523,.14),.9),(h(659,.14),.9),(h(784,.30),1.0)]),
    "通关：上行三音符")

# 直升机 rotor："突"短脉冲串（游戏侧节流~400ms触发一次，防吵）
segs = []
for k in range(4):                    # 4 个脉冲 ≈160ms，间隔微渐弱
    p = [v*e for v,e in zip(burst(int(.035*SR), .25), env_hit(int(.035*SR)))]
    segs.append((p, 1.0 - k*0.12))
add("ROTOR", seg_concat(segs), "直升机：突突脉冲串")

# ── v0.4 BOSS 技能音 ─────────────────────────────────────────────
# 📢 HORN 集结号角：双音上行方波（392→523），喇叭穿透力强=危险信号
c1 = [v*e for v,e in zip(sq(392,int(.10*SR)), env_ad(int(.10*SR),.1))]
c2 = [v*e for v,e in zip(sq(523,int(.16*SR)), env_ad(int(.16*SR),.1))]
add("HORN", seg_concat([(c1,1.0),(c2,.95)]), "号角：上行两音警报")

# 🔇 EMP 塔静默：下滑锯齿+噪声刺啦（高频塌落=断电感）
n7 = int(.20 * SR)
add("EMP", mix((.8, [a*b for a,b in zip(tone(4200,300,n7,saw=True), env_hit(n7))]),
               (.5, [a*b for a,b in zip(burst(n7,.6), env_ad(n7,.05))])),
    "EMP：高频塌落刺啦")

# ── 输出 C 头文件 ────────────────────────────────────────────────
lines = ["// SPDX-License-Identifier: MIT",
         "// Copyright (c) 2026 andjiang0083",
         "// sfx_data.h — GENERATED by tools/gen_sfx.py. DO NOT EDIT BY HAND.",
         "// 程序化合成 PCM（16kHz int16 mono）。音色设计边界：Tab5 喇叭 ~200Hz-6kHz。",
         "#pragma once", "#include <stdint.h>", "", "#define SFX_SR 16000", ""]
order = ["GUN","GUN2","GUN3","CANNON","BOOM","LASER","FROST","COIN","HIT","LEAK","CLEAR","ROTOR","HORN","EMP"]
for name in order:
    pcm, note = SFX[name]
    lines.append(f"// {name}: {note} — {len(pcm)/SR*1000:.0f}ms")
    lines.append(f"static const int16_t SFX_{name}[] = {{")
    for i in range(0, len(pcm), 12):
        lines.append("  " + ",".join(str(v) for v in pcm[i:i+12]) + ",")
    lines.append("};")
    lines.append(f"#define SFX_{name}_LEN ((uint32_t)(sizeof(SFX_{name})/sizeof(int16_t)))")
    lines.append("")
total = sum(len(SFX[n][0]) for n in order) * 2
lines.append(f"#define SFX_TOTAL_BYTES {total}   // Flash 常驻 ≈{total/1024:.0f}KB")
open("src/sfx_data.h", "w").write("\n".join(lines) + "\n")
print("sfx_data.h written:", ", ".join(f"{n}({len(SFX[n][0])/SR*1000:.0f}ms)" for n in order))
print(f"total ≈ {total/1024:.1f}KB flash")
