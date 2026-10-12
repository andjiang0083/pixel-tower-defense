#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# check_cnfont.py — 语义校验：src/cn_font.h 覆盖 src/i18n.h 的每一个汉字
# ----------------------------------------------------------------------------
# 为什么不是字节级漂移检查：点阵由 Pillow+FreeType 光栅化外部字体得到，
# 字体/FreeType/Pillow 任一不同（换平台、换字体、升级依赖）都会得到不同的
# 字节——实测同一台机器上 STHeiti 与 Hiragino 渲染结果即不同。所以 CI 只查
# 语义不变量（跨平台恒成立），字节一致性由维护机纪律保证：
#   "改 i18n.h 中文文案 → 用同一字体重跑 gen_cnfont.py → git diff 必须为空"
# 本脚本纯 stdlib，无需 Pillow，任何平台可跑。
#
# 校验内容：
#   1. cn_font.h 的收录字符集 == i18n.h STR_CN 表的汉字集（增删文案都会被发现）
#   2. CN_GLYPH_COUNT 宏 == 实际字形数 == 汉字数
#   3. 每个字形 16 行 x 2 字节，且非空（空字形 = 字体缺字，渲染即失败）
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

def fail(msg):
    sys.exit("FAIL: %s" % msg)

def needed_chars():
    text = (ROOT / "src" / "i18n.h").read_text(encoding="utf-8")
    m = re.search(r"STR_CN\[STR_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        fail("STR_CN table not found in src/i18n.h")
    return {ch for ch in m.group(1) if "\u4e00" <= ch <= "\u9fff"}

def main():
    # 文件是 UTF-8：注释含中文原文，字形键是 \xNN 转义（ASCII 安全部分）
    src = (ROOT / "src" / "cn_font.h").read_text(encoding="utf-8")

    need = needed_chars()

    # 形如：  { "\xE5\x95\x86", { 0x00,0x10, ... } },
    glyph_re = re.compile(
        r'\{ "((?:\\x[0-9A-Fa-f]{2})+)",\s*\{ ([0-9A-Fa-fx, ]+) \} \}')
    covered, empties = set(), []
    for esc, body in glyph_re.findall(src):
        b = bytes(int(x, 16) for x in re.findall(r"\\x([0-9A-Fa-f]{2})", esc))
        try:
            covered.add(b.decode("utf-8"))
        except UnicodeDecodeError:
            fail("bad UTF-8 escape in cn_font.h: %r" % esc)
        vals = [v.strip() for v in body.split(",") if v.strip()]
        if len(vals) != 32:
            fail("glyph %s has %d row bytes, expected 32" % (esc, len(vals)))
        if all(int(v, 16) == 0 for v in vals):
            empties.append(esc)

    n = len(covered)
    if empties:
        fail("%d empty glyphs (font missing these?): %s" % (len(empties), " ".join(empties)))
    if covered != need:
        missing = "".join(sorted(need - covered))
        extra = "".join(sorted(covered - need))
        fail("coverage mismatch: %d glyphs vs %d needed chars; missing=%r extra=%r"
             % (n, len(need), missing, extra))

    m = re.search(r"#define CN_GLYPH_COUNT\s+\(\(int\)\(sizeof\(CN_GLYPHS\)/sizeof\(CN_GLYPHS\[0\]\)\)\)", src)
    if not m:
        fail("CN_GLYPH_COUNT macro not found")
    if "CN_GLYPHS[] = {" not in src:
        fail("CN_GLYPHS table not found")

    print("check_cnfont: OK — %d glyphs, coverage exact, geometry valid" % n)

if __name__ == "__main__":
    main()
