#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# gh_preflight.py — 开工/收工门禁：远端同步 + 商店台账对账（零登录）
# ---------------------------------------------------------------------------
# 为什么需要它（真实事故，不是假想）：
#   2026-10-17 这一轮，会话从头到尾**一次 `git fetch` 都没做**，也**没跑商店巡检**；
#   结果 ① 用"未推送的提交"构建并发了一个商店版本（镜像在商店、提交只在本地 ⇒
#   别人无法复现）；② 仓库台账（publish.yaml 的 readback）还停在 v0.4.3 PENDING，
#   而线上 10-13 就 PUBLISHED 了 v0.4.4 —— 台账落后线上一个版本，本可以第一天就
#   看到（工具早就有：~/esp32/tools/store-audit.py 零登录巡检），是习惯问题。
#
# 门禁而非常识：**开工前跑一次、收工后跑一次**，红灯就停下对账，别"先改再拉"。
#
# 用法：
#   python3 tools/gh_preflight.py          # 全查（开工/收工都跑）
#   python3 tools/gh_preflight.py --pre    # 开工三件：远端 / 落后 / 台账对账
#   python3 tools/gh_preflight.py --post   # 收工：额外要求"无未推送提交"（发行前置条件）
# 退出码：0 = 全绿；1 = 有 ✗（阻断）
import json, pathlib, re, subprocess, sys, urllib.parse, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = 'https://burner.m5stack.com/api/v1/firmwares?keyword='


def sh(*args):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True)


def read_publish():
    p = ROOT / 'publish.yaml'
    if not p.exists():
        return None
    txt = p.read_text()
    name = re.search(r'name:\s*(.+)', txt)
    ver = re.search(r'version:\s*"?([\d.]+)"?', txt)
    rb = re.search(r'readback:\s*(.+)', txt)
    binm = re.search(r'bin:\s*(\S+)', txt)
    return {
        'name': (name.group(1).strip() if name else ''),
        'version': (ver.group(1) if ver else ''),
        'readback': (rb.group(1).strip() if rb else ''),
        'readback_vid': (re.search(r'vid=(\d+)', rb.group(1)).group(1) if rb and re.search(r'vid=(\d+)', rb.group(1)) else ''),
        'bin': (binm.group(1) if binm else ''),
    }


def check_remote():
    """① 远端：拉一次、看落后/领先。落后 > 0 必须停下对账（别先改再拉）。"""
    sh('git', 'fetch', '--prune', '--quiet')
    up = sh('git', 'rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{u}')
    if up.returncode != 0:
        print('⚠️  远端：无上游分支（CI 的 detached HEAD 属正常）→ 跳过落后检查')
        return True, 0
    nm = up.stdout.strip()
    lr = sh('git', 'rev-list', '--left-right', '--count', f'{nm}...HEAD')
    behind, ahead = (int(x) for x in lr.stdout.split()) if lr.returncode == 0 else (0, 0)
    ok = behind == 0
    print(f"{'✓' if ok else '✗'} 远端：落后 {nm} {behind} 个 / 领先 {ahead} 个"
          + ('' if ok else '  ← 先 fetch+对账再动手，禁止"先改再拉"'))
    return ok, ahead


def check_ledger():
    """② 台账 vs 线上商店（零登录）：线上出现过的版本，仓库里必须留有记录。"""
    pub = read_publish()
    if not pub or not pub['name']:
        print('⚠️  台账：publish.yaml 无 name，跳过')
        return True
    try:
        with urllib.request.urlopen(API + urllib.parse.quote(pub['name']), timeout=20) as r:
            rows = json.load(r).get('rows', [])
    except Exception as e:
        print(f'⚠️  台账：商店接口不可达（{type(e).__name__}）→ 跳过（离线时不算错）')
        return True
    hit = next((x for x in rows if (x.get('firmwareName') or '').strip() == pub['name']), None)
    if not hit:
        print(f"✗ 台账：线上查不到条目「{pub['name']}」")
        return False
    cv = hit.get('currentVersion') or {}
    live_ver, live_vid = cv.get('versionName', ''), str(cv.get('versionId', ''))
    rb_ver = ''
    m = re.search(r'v?(\d+\.\d+\.\d+)', pub['readback'])
    if m:
        rb_ver = m.group(1)
    else:  # 老式 readback 没写版本号 → 从台账指的 bin 名兜底（加 [0-9] 边界，别把日期当年份）
        m2 = re.search(r'v(\d+\.\d+\.\d+)', pub['bin'] or '')
        if m2:
            rb_ver = m2.group(1)

    def vkey(v):
        return tuple(int(x) for x in re.findall(r'\d+', v or '0')[:3]) or (0,)

    # ⚠️ 判据是**版本先后**，不是"仓库里随便有痕迹"（CHANGELOG 提到过 0.4.4 也会让
    #    "有痕迹"型判据假绿 —— 2026-10-17 事故正是台账停在 0.4.3 PENDING、线上已 0.4.4）。
    if live_vid and live_vid == pub['readback_vid']:
        print(f"✓ 台账：线上版本 {live_ver}（vid={live_vid}）= 台账 readback，一致")
        return True
    if rb_ver and vkey(live_ver) < vkey(rb_ver):
        print(f"✓ 台账：在途 —— 线上已发布 {live_ver}，台账记的是在审的 {rb_ver}"
              f"（{pub['readback'][:48]}…）")
        return True
    print(f"✗ 台账漂移：线上已发布 {live_ver}（vid={live_vid}），而台账最后记录是 "
          f"{rb_ver or '（解析不出）'}（vid={pub['readback_vid'] or '-'}）—— "
          f"商店发过一版台账从未回灌。先回灌再动手。")
    return False


def check_bin(post):
    """③ 发行件：台账指的 bin + sha256 是否在位；收工还要求"无未推送提交"。"""
    pub = read_publish()
    ok = True
    if pub and pub['bin']:
        b = ROOT / pub['bin']
        sha = ROOT / (pub['bin'] + '.sha256')
        if b.exists():
            print(f"✓ 发行件：{pub['bin']} 在位（{b.stat().st_size:,} B）")
        else:
            print(f"⚠️  发行件：{pub['bin']} 不在本地（bin_external / 走 Release 附件时正常）")
        if sha.exists():
            print(f"✓ 发行件：{sha.name} 已入库（校验值随仓走）")
        else:
            print(f"✗ 发行件：缺 {sha.name} —— bin 可以不入库，校验值必须入库")
            ok = False
    return ok


def main():
    args = set(sys.argv[1:])
    post = '--post' in args
    print('== gh_preflight：远端同步 + 商店台账对账（零登录）==')
    ok_remote, ahead = check_remote()
    ok_ledger = check_ledger()
    ok_bin = check_bin(post)
    ok = ok_remote and ok_ledger and ok_bin
    if post:
        if ahead > 0:
            print(f"✗ 收工：有 {ahead} 个未推送提交 —— 发行件必须来自**已推送**的提交，"
                  f"否则商店里的镜像无法复现。先 push 再发布。")
            ok = False
        else:
            print('✓ 收工：本地与远端一致（无未推送提交）')
    elif ahead > 0:
        print(f'ℹ️  当前领先远端 {ahead} 个提交（开工时正常；收工前要 push）')
    print('== 结论：', '全绿，可以继续' if ok else '有 ✗，先对账再动手', '==')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
