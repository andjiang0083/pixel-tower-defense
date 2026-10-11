#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
"""map_design.py — 关卡地图审计器（读 layout.py 唯一真源，出指标表 + 热力图）。

设计哲学（讨论定稿，见 SPEC-v0.4.md）：
  · 路是绳子，塔是环。难度 = 削减"好位置"，不是堆数值。
  · 关卡是【预算题】不是【拼图】：多条活路、失败可渐进挽救（防"围棋化"）。
  · 长路径不该白送时间：可建区压成贴路窄带/行间位，长度就变周长负担。

指标怎么读：
  最少塔数 = 贪心集合覆盖"罩住全路至少几座这种塔"（≤ MAX_TOWERS=20 才通）。
    该数越接近 20 = 空间考题越狠（预算题）；三种塔都能通 = 至少三条活路。
  宝地     = 一格罩 ≥8 路格的可建位（蛇形的行间位天生就是宝地）。
  死角     = 只有 ≤2 个可建位打得到的路格（唯一解风险区，必须少）。

历史教训（别重蹈）：
  v1 用"潜在覆盖"涂色 → 可建格两百多个，全图恒绿，指标失效（=空间免费）。
  v2 手画白名单 → L6/L8 只剩 7~8 位，90% 路打不到 = 不可玩。
  v3 贪心起点写成"被覆盖格的并集" → 打不到的格被静默忽略，坏图伪装 PASS。
  ⇒ 现在的规矩：贪心起点=全部路格；可建区由 band(D) 统一雕刻，不手画。
"""
import os, sys, struct, zlib

sys.path.insert(0, os.path.dirname(__file__))
import layout as L

MAX_TOWERS = 20
GOLD_MIN_PATH = 8
W, H = L.SCREEN_W, L.MAP_ROWS * L.CELL


def audit(lv):
    pathc = L.path_cells(lv["routes"])
    blocked = L.blocked_cells(lv)
    idx = {p: i for i, p in enumerate(sorted(pathc))}
    cells = [(c, r) for c in range(L.COLS) for r in range(L.MAP_ROWS)
             if (c, r) not in pathc and (c, r) not in blocked]

    # 每个可建位 → [(路格下标, 距离²)]；算一次，各射程尺子直接过滤
    seen = []
    for b in cells:
        hits = []
        for p, i in idx.items():
            dx = (b[0] - p[0]) * L.CELL; dy = (b[1] - p[1]) * L.CELL
            d2 = dx * dx + dy * dy
            if d2 <= 322 * 322: hits.append((i, d2))
        seen.append((b, hits))

    def greedy(rng):
        r2 = rng * rng
        masks = [sum(1 << i for i, d2 in h if d2 <= r2) for _b, h in seen]
        masks = [m for m in masks if m]
        todo, picks = (1 << len(idx)) - 1, 0
        while todo:
            best, bn = 0, 0
            for m in masks:
                n = bin(m & todo).count("1")
                if n > bn: best, bn = m, n
            if bn == 0: break
            todo &= ~best; picks += 1
        return picks, bin(todo).count("1")          # ⚠️ 起点=全部路格

    m1 = [sum(1 << i for i, d2 in h if d2 <= 150 * 150) for _b, h in seen]
    m1 = [m for m in m1 if m]
    cov = [0] * len(idx)
    for m in m1:
        for i in range(len(idx)):
            if (m >> i) & 1: cov[i] += 1

    n_gat, g_gat = greedy(150.0)                    # GAT LV1（可达性地板）
    n_gat3, _ = greedy(150 * 1.24)                  # GAT LV3（射程 +24%）
    n_las, g_las = greedy(260 * 1.24)               # LASER LV3（狙击链）
    return dict(pathc=pathc, blocked=blocked, cells=cells, idx=idx, cov=cov,
                golden=sum(1 for m in m1 if bin(m).count("1") >= GOLD_MIN_PATH),
                thin=sum(1 for v in cov if v <= 2), gat=n_gat, gat3=n_gat3,
                las=n_las, gap=max(g_gat, g_las), seen=dict(seen))


# ── 纯 stdlib PNG（零依赖，同 layout.py）──────────────────────────
def png(path, w, h, buf):
    raw = b"".join(b"\x00" + bytes(buf[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def unmap(n):
    return (((n >> 11) & 0x1F) << 3, ((n >> 5) & 0x3F) << 2, (n & 0x1F) << 3)


def rgb565(rgb):
    r, g, b = rgb; return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def mix(a, b, t):
    return tuple(int(x + (y - x) * t) for x, y in zip(a, b))


def render(lv, a):
    gA, gB, pA, pB = L.THEMES[lv["theme"]]
    gA, gB = unmap(rgb565(gA)), unmap(rgb565(gB))
    pA, pB = unmap(rgb565(pA)), unmap(rgb565(pB))
    ROCK, ROCKHI, ROCKLO = (38, 40, 46), (74, 78, 90), (22, 24, 28)
    buf = bytearray(W * H * 3)

    def rect(x, y, w, h, rgb):
        x0, x1 = max(0, int(x)), min(W, int(x + w))
        y0, y1 = max(0, int(y)), min(H, int(y + h))
        blob = bytes(rgb)
        for yy in range(y0, y1):
            o = yy * W * 3 + x0 * 3
            for _ in range(x1 - x0): buf[o:o+3] = blob; o += 3

    for r in range(L.MAP_ROWS):
        for c in range(L.COLS):
            x, y = c * L.CELL, r * L.CELL
            cell = (c, r)
            if cell in a["pathc"]:                  # 路：按"几个可建位打得到"涂色
                cv = a["cov"][a["idx"][cell]]
                col = (mix(pA, (232, 66, 44), .52) if cv <= 1 else
                       mix(pA, (238, 196, 52), .42) if cv <= 5 else
                       mix(pA, (54, 204, 96), .34))
                rect(x, y, L.CELL, L.CELL, col)
                for i2 in range(0, L.CELL, 8):
                    for j in range((i2 // 8) % 2 * 4, L.CELL, 8):
                        rect(x + i2, y + j, 4, 4, pB)
            elif cell in a["blocked"]:              # 禁建岩层
                rect(x + 1, y + 2, L.CELL - 2, L.CELL - 4, ROCK)
                rect(x + 4, y + 5, 12, 5, ROCKHI)
                rect(x + L.CELL - 18, y + L.CELL - 13, 9, 4, ROCKHI)
                rect(x + 2, y + L.CELL - 6, L.CELL - 4, 3, ROCKLO)
            else:                                   # 可建草地
                rect(x, y, L.CELL, L.CELL, gA if (c + r) % 2 == 0 else gB)
                rect(x, y, L.CELL, 1, (14, 34, 20)); rect(x, y, 1, L.CELL, (14, 34, 20))

    top = sorted(a["seen"].items(),
                 key=lambda kv: -sum(1 for _i, d2 in kv[1] if d2 <= 150 * 150))[:6]
    for (b, hits) in top:                           # 宝地金菱（每关只标前 6 个，否则满屏金星没法看）
        if sum(1 for _i, d2 in hits if d2 <= 150 * 150) < GOLD_MIN_PATH: continue
        cx, cy = b[0] * L.CELL + L.CELL // 2, b[1] * L.CELL + L.CELL // 2
        rect(cx - 3, cy - 3, 6, 6, (255, 214, 80))
        rect(cx - 9, cy - 1, 18, 2, (255, 214, 80))
        rect(cx - 1, cy - 9, 2, 18, (255, 214, 80))
    for rt in lv["routes"]:                         # 出生(红) / 出口(金)
        rect(rt[0][0] * L.CELL + 8, rt[0][1] * L.CELL + 12, 24, 16, (196, 48, 40))
        rect(rt[-1][0] * L.CELL + 8, rt[-1][1] * L.CELL + 12, 24, 16, (255, 206, 74))
    return buf


def downscale(buf, w, h, k):
    nw, nh = w // k, h // k
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        sy = y * k * w * 3
        o = y * nw * 3
        for x in range(nw):
            s = sy + x * k * 3
            out[o:o+3] = buf[s:s+3]; o += 3
    return out, nw, nh


def main():
    os.makedirs("shots", exist_ok=True)
    print("%-3s %-15s %6s %7s %7s %6s %6s %7s %6s %7s  %s" %
          ("Lv", "name", "路格", "可建格", "岩格", "宝地", "死角",
           "GAT塔", "L3塔", "LAS塔", "验收"))
    tiles, bad = [], []
    for lv in L.LEVELS:
        a = audit(lv)
        ok = (a["gat"] <= MAX_TOWERS and a["las"] <= MAX_TOWERS and a["gap"] == 0
              and (lv["id"] <= L.LEGACY_LEVELS or a["golden"] >= 3)
              and a["thin"] * 100.0 / max(1, len(a["pathc"])) <= 25)
        if not ok: bad.append(lv["id"])
        print("%-3d %-15s %6d %7d %7d %6d %6d %7d %6d %7d  %s" %
              (lv["id"], lv["name"], len(a["pathc"]), len(a["cells"]),
               len(a["blocked"]), a["golden"], a["thin"],
               a["gat"], a["gat3"], a["las"], "PASS" if ok else "**FAIL**"))
        buf = render(lv, a)
        png("shots/preview-map-lv%02d.png" % lv["id"], W, H, buf)
        tiles.append(downscale(buf, W, H, 4))

    cols, gap = 4, 8                                 # 拼图 4×5：一眼看完 20 关
    tw, th = tiles[0][1], tiles[0][2]
    rows = (len(tiles) + cols - 1) // cols
    sw, sh = cols * tw + (cols + 1) * gap, rows * th + (rows + 1) * gap
    sheet = bytearray(bytes((18, 20, 24)) * (sw * sh))
    for i, (t, nw, nh) in enumerate(tiles):
        cx, cy = (i % cols) * (tw + gap) + gap, (i // cols) * (th + gap) + gap
        for y in range(nh):
            o = ((cy + y) * sw + cx) * 3
            sheet[o:o + nw * 3] = t[y * nw * 3:(y + 1) * nw * 3]
    png("shots/preview-maps-sheet.png", sw, sh, sheet)
    print("\n不合格：%s   拼图：shots/preview-maps-sheet.png" % (bad or "无"))


if __name__ == "__main__":
    main()
