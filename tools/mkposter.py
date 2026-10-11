#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
"""mkposter.py — 生成 PIXEL TOWER DEFENSE 宣传图（中英双语）。

零外部素材原则延伸到宣传图：画面来自**游戏真机渲染帧**（仿真的 PPM 截图），
标题用**游戏自带的 5x7 点阵字库**（tools/gen_font.py），音效波形来自
**程序化合成的 PCM**（tools/gen_sfx.py）。所以海报本身就是"零素材"的证明。

用法：
  # 1) 先抓一张战场帧（真渲染，有塔有兵）
  g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim
  SIM_SHOT=13 SIM_SHOT_WAVE=9 SIM_SHOT_OUT=/tmp/ptd/frame.ppm ./sim/sim

  # 2) 出图
  python3 tools/mkposter.py --frame /tmp/ptd/frame.ppm --out releases/
    → releases/cover-1280x720-pixel-tower-defense-tab5.png   (M5Burner 封面, 1280x720)
    → releases/poster-2560x1440-pixel-tower-defense.png      (GitHub/README 首图, 2x)
"""
import argparse, os, sys, importlib.util
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
CJK_FONT = "/System/Library/Fonts/STHeiti Medium.ttc"

# ── 调色板（与游戏内一致：暗底 + 琥珀/青/白）─────────────────────
BG        = (7, 10, 14)
BG2       = (13, 17, 23)
WHITE     = (232, 236, 240)
AMBER     = (255, 176, 46)
AMBER_DIM = (150, 104, 30)
CYAN      = (86, 214, 232)
GREEN     = (126, 214, 122)
RED       = (231, 92, 84)
GREY      = (128, 138, 148)

TITLE_CN   = "像素塔防"
KICKER     = "M5STACK TAB5 - ESP32-P4 - 20 LEVELS - ZERO ASSET FILES"
CN_SUB     = "20 关小挑战 - 魔性音效 - 本机大模型写就"
BULLETS = [
    ("本机大模型写就",  "WRITTEN BY A LOCAL LLM",            AMBER),
    ("魔性音效",        "PROCEDURAL SFX, ZERO AUDIO FILES",  CYAN),
    ("小挑战的难度",    "BITE-SIZED, RETRY-UNTIL-YOU-WIN",   GREEN),
    ("小学生测试员",    "PLAYTESTED BY A KID",               RED),
]
URL   = "github.com/andjiang0083/pixel-tower-defense"
DEVICE = "M5STACK TAB5 / ESP32-P4 / 1280x720"


def load_glyphs():
    """复用游戏字库（唯一真源：tools/gen_font.py），不在海报里另画一套字。"""
    spec = importlib.util.spec_from_file_location("gen_font", os.path.join(HERE, "gen_font.py"))
    mod = importlib.util.module_from_spec(spec)
    _stdout = sys.stdout
    sys.stdout = open(os.devnull, "w")          # gen_font 会打印字形预览
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.stdout.close(); sys.stdout = _stdout
    return {k.upper(): v for k, v in mod.GLYPHS.items()}


def txt(text, scale, color, glyphs, spacing=1):
    """游戏点阵字（5x7）→ 位图文字，逐像素画方块，和游戏里同一种笔画。"""
    img = Image.new("RGBA", (len(text) * (5 + spacing) * scale, 7 * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, ch in enumerate(text.upper()):
        g = glyphs.get(ch)
        if g is None:
            continue
        for r, row in enumerate(g.split()):
            for c, bit in enumerate(row):
                if bit == "1":
                    x = i * (5 + spacing) * scale + c * scale
                    y = r * scale
                    d.rectangle([x, y, x + scale - 1, y + scale - 1], fill=color)
    return img


def cjk(text, px, k, color):
    """中文：系统字体小字号渲染 → 二值化 → 整数倍放大 = 像素字（与点阵字同质感）。"""
    f = ImageFont.truetype(CJK_FONT, px)
    tmp = Image.new("L", (px * len(text) + px * 2, px * 3), 0)
    ImageDraw.Draw(tmp).text((px // 2, px // 2), text, font=f, fill=255)
    tmp = tmp.crop(tmp.getbbox())
    tmp = tmp.point(lambda v: 255 if v >= 110 else 0)          # 去抗锯齿 → 硬边
    tmp = tmp.resize((tmp.width * k, tmp.height * k), Image.NEAREST)
    out = Image.new("RGBA", tmp.size, color)
    out.putalpha(tmp)
    return out


def waveform(kind, w, h, color, glyphs):
    """程序化音效的真实 PCM 包络（gen_sfx.py 的唯一真源），画成条形谱。"""
    spec = importlib.util.spec_from_file_location("gen_sfx", os.path.join(HERE, "gen_sfx.py"))
    mod = importlib.util.module_from_spec(spec)
    _stdout = sys.stdout
    sys.stdout = open(os.devnull, "w")
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.stdout.close(); sys.stdout = _stdout
    pcm = mod.SFX[kind][0]
    n = 64
    step = max(1, len(pcm) // n)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    bw = max(1, w // n - 1)
    for i in range(n):
        chunk = pcm[i * step:(i + 1) * step] or [0]
        peak = max(abs(v) for v in chunk) / 32767.0
        bh = max(1, int(peak * (h - 2)))
        x = i * (bw + 1)
        d.rectangle([x, (h - bh) // 2, x + bw - 1, (h - bh) // 2 + bh - 1], fill=color)
    return img


def busy_crop(sc, cw, ch):
    """自动挑"最热闹"的裁切窗：以帧内最常见三种颜色（草地/路/暗底）为背景，
    滑动窗口里"背景之外"的像素最多者胜 → 塔和兵最密的那一块。"""
    small = sc.copy()
    q = small.quantize(colors=12).convert("RGB")
    from collections import Counter
    cnt = Counter(q.getdata())
    bg = [c for c, _ in cnt.most_common(3)]
    px = q.load()
    W, H = q.size
    best, bx, by = -1, 0, 62
    for y in range(0, min(H - ch, 212) + 1, 14):
        for x in range(0, max(1, W - cw) + 1, 14):
            s = 0
            for yy in range(y, y + ch, 6):
                for xx in range(x, x + cw, 6):
                    c = px[xx, yy]
                    if all(abs(c[0]-b[0]) + abs(c[1]-b[1]) + abs(c[2]-b[2]) > 60 for b in bg):
                        s += 1
            if s > best:
                best, bx, by = s, x, y
    return (bx, by, bx + cw, by + ch)


def build(frame_path, S, out_path, label):
    W, H = 1280 * S, 720 * S
    glyphs = load_glyphs()
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # 背景：极淡网格（同游戏草格的网格语言）+ 左上冷光 + 右侧暖光
    for x in range(0, W, 40 * S):
        d.line([(x, 0), (x, H)], fill=BG2, width=S)
    for y in range(0, H, 40 * S):
        d.line([(0, y), (W, y)], fill=BG2, width=S)
    for cx, cy, col, rad in ((0, 0, (10, 13, 18), 620), (W, H, (18, 12, 6), 720)):
        ov = Image.new("RGB", (W, H), (0, 0, 0))
        od = ImageDraw.Draw(ov)
        for i in range(28):
            r = int(rad * S / 4 * (28 - i) / 28)
            od.ellipse([cx // 4 - r, cy // 4 - r, cx // 4 + r, cy // 4 + r],
                       fill=tuple(int(v * (28 - i) / 28) for v in col))
        img = Image.blend(img, ov, 0.55)
    d = ImageDraw.Draw(img)

    # ── 左栏：文字 ─────────────────────────────────────────────
    x0 = 46 * S
    y = 54 * S
    k = txt(KICKER, S, AMBER_DIM, glyphs)
    img.paste(k, (x0, y), k)

    y += 26 * S
    for word, col in (("PIXEL", WHITE), ("TOWER", WHITE), ("DEFENSE", AMBER)):
        t = txt(word, 7 * S, col, glyphs)
        sh = txt(word, 7 * S, (0, 0, 0), glyphs)
        img.paste(sh, (x0 + 3 * S, y + 3 * S), sh)
        img.paste(t, (x0, y), t)
        y += 8 * 7 * S + 6 * S

    d.rectangle([x0, y + 6 * S, x0 + 300 * S, y + 6 * S + 3 * S - 1], fill=AMBER)
    y += 20 * S
    cn = cjk(TITLE_CN, 30, S, WHITE)
    img.paste(cn, (x0, y), cn)
    sub = cjk(CN_SUB, 13, S, CYAN)
    img.paste(sub, (x0, y + cn.height + 10 * S), sub)

    # ── 左栏下：四条卖点（中 + 英）────────────────────────────
    by = 434 * S
    for cn_txt, en_txt, col in BULLETS:
        d.rectangle([x0, by + 6 * S, x0 + 5 * S, by + 6 * S + 5 * S], fill=col)
        c = cjk(cn_txt, 17, S, col)
        img.paste(c, (x0 + 14 * S, by), c)
        e = txt(en_txt, S, GREY, glyphs)
        img.paste(e, (x0 + 14 * S + c.width + 12 * S, by + (c.height - 7 * S) // 2), e)
        by += 34 * S

    # 数值条（与版本无关的固定卖点）
    st = txt("20 LEVELS - 5 TOWERS - 8 BOSS SKILLS", S, AMBER_DIM, glyphs)
    img.paste(st, (x0, 606 * S), st)

    # 分隔竖线
    d.line([(440 * S, 54 * S), (440 * S, H - 60 * S)], fill=(28, 34, 42), width=S)

    # ── 右栏：真机战场帧（1:1 裁切，不缩放 → 像素不糊）──────────
    sc = Image.open(frame_path).convert("RGB")
    cw, ch = 760, 428
    cx, cy = W - cw * S - 44 * S, 92 * S
    crop = sc.crop(busy_crop(sc, cw, ch))
    if S != 1:
        crop = crop.resize((cw * S, ch * S), Image.NEAREST)
    pad = 5 * S
    bw, bh = cw * S + pad * 2, ch * S + pad * 2
    glow = Image.new("RGBA", (bw + 16 * S, bh + 16 * S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rectangle([0, 0, glow.width - 1, glow.height - 1],
                                   outline=(255, 176, 46, 46), width=3 * S)
    img.paste(Image.new("RGB", glow.size, BG), (cx - 8 * S, cy - 8 * S))
    img.paste(glow, (cx - 8 * S, cy - 8 * S), glow)
    bez = Image.new("RGB", (bw, bh), (22, 28, 36))
    bez.paste(crop, (pad, pad))
    ImageDraw.Draw(bez).rectangle([0, 0, bw - 1, bh - 1], outline=AMBER, width=2 * S)
    img.paste(bez, (cx, cy))

    cap = txt(label, S, GREY, glyphs)
    img.paste(cap, (cx + pad, cy + bh + 12 * S), cap)

    # 音效波形条（卖点 2 的视觉证据）
    wf = waveform("BOOM", 300 * S, 40 * S, CYAN, glyphs)
    wy = cy + bh + 34 * S
    img.paste(wf, (cx + pad, wy), wf)
    wl = txt("SFX = MATH, NOT FILES", S, AMBER_DIM, glyphs)
    img.paste(wl, (cx + pad + wf.width + 14 * S, wy + (wf.height - 7 * S) // 2), wl)

    # ── 底栏 ───────────────────────────────────────────────────
    line_y = H - 46 * S
    d.line([(x0, line_y), (W - x0, line_y)], fill=(30, 36, 44), width=S)
    u = txt(URL, S, WHITE, glyphs)
    img.paste(u, (x0, line_y + 14 * S), u)
    dv = txt(DEVICE, S, AMBER_DIM, glyphs)
    img.paste(dv, (W - x0 - dv.width, line_y + 14 * S), dv)

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    img.save(out_path)
    print("wrote", out_path, img.size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", required=True, help="战场帧 PPM（sim 截图模式产出）")
    ap.add_argument("--out", default="releases/")
    ap.add_argument("--label", default="L13 SAND COIL - 20 TOWERS - 43 ENEMIES ON SCREEN")
    a = ap.parse_args()
    base = a.out.rstrip("/")
    build(a.frame, 1, f"{base}/cover-1280x720-pixel-tower-defense-tab5.png", a.label)
    build(a.frame, 2, f"{base}/poster-2560x1440-pixel-tower-defense.png", a.label)


if __name__ == "__main__":
    main()
