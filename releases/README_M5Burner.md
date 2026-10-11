# PIXEL TOWER DEFENSE — M5Stack Tab5

**A pixel tower defence written by a local LLM. 20 levels, 5 towers, 8 boss
abilities — and not a single image or audio file in the firmware: every pixel is
drawn with `fillRect`, every sound is synthesised from arithmetic.**

**本机大模型写就的像素塔防。20 关、5 种塔、8 种 BOSS 技能 —— 固件里没有一个
图片或音频文件：每一颗像素都是 `fillRect` 画的，每一声都是算出来的。**

---

## Flash it / 刷机

1. Open **M5Burner** (desktop app, or <https://burner.m5stack.com>) and search for
   **PIXEL TOWER DEFENSE**, then flash.
   打开 **M5Burner**（桌面版或网页版）搜 **PIXEL TOWER DEFENSE**，直接刷。
2. Or with esptool, writing the merged image at 0x0 / 或用 esptool 把合并镜像写到 0x0：

```bash
esptool --chip esp32p4 write-flash -z 0x0 pixel-tower-defense-v0.4.0-tab5-merged.bin
```

⚠️ Use the **merged** image (this file's sibling `.bin`), not an app-only
`firmware.bin` — M5Burner always writes at offset 0x0, so an app-only image
overwrites the bootloader and the screen stays black.
⚠️ 必须用**合并镜像**（不是只有 app 的 `firmware.bin`）：M5Burner 永远写到 0x0，
只写 app 会盖掉 bootloader，刷完黑屏。

**Device / 设备：** M5Stack Tab5 (ESP32-P4, 1280×720 touch screen)
**Version / 版本：** v0.4.0 · merged image 928,816 B · sha256 in the `.sha256` file

---

## How to play / 怎么玩

Touch only — this device has no buttons. / 纯触摸，这台设备没有实体按键。

1. **Tap a tower** in the bottom bar. / 点底部建造栏选塔。
2. **Tap green ground** to build. Path, occupied and **rock** cells are refused —
   rock is drawn as grey rubble so you can see it before you tap.
   点绿色草地建塔；路径格、已占用格、**岩石格**会被拒 —— 岩石格画成深灰乱石，
   点之前就能看出来。
3. **Tap a built tower** to upgrade to LV2/LV3 or sell for 70 % back.
   点已建的塔升级或按 70 % 卖出。
4. **NEXT WAVE** (top right) sends the next wave early. / 右上角提前开下一波。
5. You have **20 lives**; lose them all and the level restarts when you tap.
   20 条命，掉光后点屏幕重开本关。

| Tower | Cost | What it does |
|---|---|---|
| M.GUN | 60 | Fast single target. Struggles against armour. |
| CANNON | 100 | Splash — the armour answer. Slow. |
| FROST | 80 | Slows enemies to 45 %, deals no damage. Support only. |
| ENERGY | 80 | Pays out gold. Occupies a tower slot to earn money. |
| LASER | 140 | Beam, full damage vs shields, longest range. |

| Tower / 塔 | 造价 / Cost | 说明 |
|---|---|---|
| M.GUN | 60 | 便宜速射，怕护甲 |
| CANNON | 100 | 溅射，专治护甲 |
| FROST | 80 | 减速到 45%，零伤害的辅助 |
| ENERGY | 80 | 产金，用塔位换经济 |
| LASER | 140 | 激光，破盾，射程最长 |

**Watch out for / 小心这些敌人：** ARMOR (flat −6 per bullet hit) · SHIELD (half
bullet damage) · CHOPPER (flies straight to your base, ignoring your maze) ·
HEALER (heals the group) · BOSS (8 abilities: heal, brood, death-split, horn, EMP,
greed, molt, ghost — up to two bosses per level).
护甲兵 / 护盾兵 / 直升机（无视迷宫直线飞）/ 治疗兵 / BOSS（8 种技能，一关最多两只）。

---

## Why it is unusual / 有什么不一样

1. **Written by a local LLM.** Architecture and most of the code came from a 4-bit
   model running on the author's own Mac. Slow, but it shipped.
   **本机大模型写就。** 架构和大部分代码出自作者 Mac 上的 4-bit 模型，慢，但做完了。
2. **Sound effects are maths.** Machine-gun ticks, cannon thuds, explosions: all
   synthesised at build time, zero audio files.
   **魔性音效是算出来的。** 没有一个音频文件。
3. **Small, retry-until-you-win challenges.** 20 maps, minutes each, multiple
   viable build orders — never a single-solution puzzle.
   **小挑战的难度。** 一关几分钟、失败可挽救，每张图都有多条活路，不搞唯一解。
4. **Play-tested by a primary-school kid.** The wave sizes, level length and boss
   difficulty were tuned from his notes.
   **小学生测试员亲手测过。** 波量、关卡长度、BOSS 手感都按他的反馈改过。

**Source, build instructions and the full honest status list /
源码、构建说明与完整现状清单：** <https://github.com/andjiang0083/pixel-tower-defense>
