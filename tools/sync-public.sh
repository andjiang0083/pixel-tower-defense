#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
# sync-public.sh — 把开发树同步到公开仓的工作副本（单向，别反向手改公开树）
#
# 用法：在开发树里执行
#   tools/sync-public.sh [公开树路径]
# 默认公开树：~/esp32/pixel-tower-defense-public
#
# 纪律：公开仓里唯一的产出方式是「本脚本」，禁止在公开树里直接编辑——
#       两份副本一旦各自演化，就再也说不清哪份是真的。
set -eu

DEV="$(cd "$(dirname "$0")/.." && pwd)"
PUB="${1:-$HOME/esp32/pixel-tower-defense-public}"

[ -d "$PUB/.git" ] || { echo "公开树不存在或不是 git 仓库：$PUB" >&2; exit 1; }

rsync -a --delete \
  --exclude='.git/' \
  --exclude='.pio/' \
  --exclude='sim/sim' \
  --exclude='*.dSYM/' \
  --exclude='*.o' \
  --exclude='*.elf' \
  --exclude='*.map' \
  --exclude='*.ppm' \
  --exclude='.DS_Store' \
  --exclude='.vscode/' \
  --exclude='__pycache__/' \
  --exclude='releases/*.bin' \
  "$DEV/" "$PUB/"

echo "synced → $PUB"
cd "$PUB" && git status --short
