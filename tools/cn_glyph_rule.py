#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# cn_glyph_rule.py — 【单一真源】"哪些字符必须有 16x16 点阵"
# ----------------------------------------------------------------------------
# 为什么需要一份共享规则：v0.4.3 之前，gen_cnfont.py（生成字库）和
# check_cnfont.py（CI 校验覆盖）各自写了一遍 U+4E00..9FFF 的判定。
# 两份判定一致时没事，但只要有一处改动就会漂移；而它们的共同漏洞——
# **全角标点不在汉字区**——让 STR_CN 里的 "已击败：" 的冒号、
# "拦住所有敌人！" 的感叹号从未进字库，渲染时被静默跳过。
#
# 正确的语义不变量其实很简单：
#   px_text 遇 (>=0x80) 字节切中文点阵 → **STR_CN 表里每一个非 ASCII
#   字符都必须有字形**（不管它是汉字、全角标点还是别的什么）。
# 生成器和检查器都调用本文件，任何一边都无法单独漂移。

import re

_STR_TABLE_RE = r"STR_CN\[STR_COUNT\]\s*=\s*\{(.*?)\};"


def needs_glyph(ch):
    """字符是否需要 16x16 点阵字形。

    px_text 以首字节 >= 0x80 判定 UTF-8 多字节序列；因此任一非 ASCII
    码点（汉字 / 全角标点 / CJK 符号）都必须有字形，ASCII 走 5x7 点阵。
    """
    return ord(ch) >= 0x80


def collect_from_i18n(root):
    """从 src/i18n.h 的 STR_CN 表收集所有需要字形的字符（去重、排序）。"""
    text = (root / "src" / "i18n.h").read_text(encoding="utf-8")
    m = re.search(_STR_TABLE_RE, text, re.S)
    if not m:
        raise SystemExit("FATAL: STR_CN table not found in src/i18n.h")
    # 只取字符串字面量内部，避免把中文注释里的字也收进来
    body = "".join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group(1), re.S))
    chars = {ch for ch in body if needs_glyph(ch)}
    return sorted(chars)
