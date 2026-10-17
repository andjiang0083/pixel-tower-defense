#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# claw4_isolation.py — Claw4 零位移门禁（共享层改动的机器可判证明）
# ---------------------------------------------------------------------------
# 为什么需要它：tab5 与 claw4 共用同一份 src/*.h（只差 build/<profile>/config_gen.h）。
# 为 Tab5 修东西时"顺手"改到共享层，会让 Claw4 固件跟着位移 —— 那是另一台机器、
# 另一批用户的固件，位移意味着"谁来验它"。v0.5.0 的教训更贵：为了 Claw4 的 720
# 窄屏在共享层删掉中文链路，Tab5 的中文能力被连带干掉。
#
# 原理：把共享文件按 **claw4 profile** 预处理一遍，逐字节比对基线哈希。
#   预处理输出相同 ⇒ 编译器看到的源码相同 ⇒ 目标码必然相同（比反汇编更早、更硬）。
#   所以任何"泄漏到 Claw4"的改动都会立刻红，作者只有两条路：
#     ① 把改动挪进能力宏（PTD_HAS_CJK / PTD_BG_TILE_BLIT …）—— 推荐
#     ② 有意改 Claw4 行为时，用 --update 重新落基线并在 PR 里说明原因
#
# 用法：python3 tools/claw4_isolation.py            # 校验（CI）
#       python3 tools/claw4_isolation.py --update   # 有意改动后重新落基线
import hashlib, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = ROOT / 'docs' / 'claw4-isolation.sha256'
# ⚠️ 文件清单**自动发现**，不许写死：写死时新增一个共享文件就悄悄漏出覆盖范围
#   （v0.5.4 复查发现原来的 10 个是硬编码的 —— 那正是"门禁看着在、其实有洞"的形态）。
#   · 自动发现 src/ 下全部 .h/.cpp（新增文件自动进覆盖）
#   · 例外：sdkconfig.h 是 ESP-IDF 的设备侧 shim（`#include_next`），本机预处理不了，
#     也不含共享逻辑 ⇒ 显式排除，并把排除项打印出来（覆盖范围必须可见，不许静默）
EXCLUDE = {'src/sdkconfig.h'}
FILES = [str(p.relative_to(ROOT)) for p in
         sorted((ROOT / 'src').glob('*.h')) + sorted((ROOT / 'src').glob('*.cpp'))
         if str(p.relative_to(ROOT)) not in EXCLUDE]
CMD = ['g++', '-E', '-std=c++17', '-DPTD_FAKE_TOUCH',
       '-I', 'build/claw4', '-I', 'sim', '-I', 'sim/compat', '-I', 'src', '-P']


def preprocess(rel):
    r = subprocess.run(CMD + [rel], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'❌ 预处理失败：{rel}\n{r.stderr[:800]}')
    return r.stdout


def digest():
    h = hashlib.sha256()
    for rel in FILES:
        h.update(f'== {rel} ==\n'.encode())
        h.update(preprocess(rel).encode())
    return h.hexdigest()


def main():
    d = digest()
    if '--update' in sys.argv:
        BASE.parent.mkdir(parents=True, exist_ok=True)
        BASE.write_text(f'{d}  claw4 共享层预处理基线（改 Claw4 行为时用 --update 重落）\n')
        print(f'✅ 基线已更新：{d}')
        return 0
    if not BASE.exists():
        sys.exit(f'❌ 缺基线 {BASE.relative_to(ROOT)}；先跑 --update 落一次')
    want = BASE.read_text().split()[0]
    if d != want:
        sys.exit(f'❌ Claw4 位移了：当前 {d[:16]} ≠ 基线 {want[:16]}\n'
                 '   共享层改动泄漏到 Claw4 了。请挪进能力宏（见 tools/layout.py），'
                 '或有意改动时 --update 并说明原因。')
    print(f'✅ 零位移：Claw4 共享层预处理哈希与基线一致（{d[:16]}）')
    print(f'   覆盖 {len(FILES)} 个共享文件；排除 {sorted(EXCLUDE)}（设备侧 shim；改动它要先问）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
