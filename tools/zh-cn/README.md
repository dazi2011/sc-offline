# 简体中文支持

本分支（`zh-cn`）在 `scubamount/sc-offline` 0.7.0 的源码上加入了简体中文：

- **菜单汉化**：菜单、按钮、提示和状态栏都是简体中文（`mod.log`、配置键、文件名、类名不翻译）。
- **中文字体**：依次尝试 `data\font_zh.ttf` / `data\font_zh.ttc`、macOS 的 `Hiragino Sans GB.ttc` /
  `STHeiti Medium.ttc`（在 Wine/CrossOver 里经 `Z:` 盘读取）、Windows 的微软雅黑 / 黑体；都找不到时只用英文字体。
  想换字体，把字体文件放成 `data\font_zh.ttf` 即可。
- **中文船名**：“载具”页按 `data\ship_names_zh.txt`（每行 `代号|中文名`）显示，鼠标悬停显示原始代号，
  搜索时中文名和代号都能匹配。刷船仍然使用原始代号。
- **中文地名**：“传送”页按 `data\place_names_zh.txt` 显示星系、行星、卫星、拉格朗日点、跳跃点以及扫描到的
  小地点的中文名。传送仍然使用原始实体名。
- **跨星系传送**：列表里的地点可以跨星系传送（实验功能，需用 `PU_All` 启动）；只有找不到目标所在星系时才报错。

## 文件

| 文件 | 作用 |
| --- | --- |
| `p4k-extract.py` | 只读地从 `Data.p4k`（zip64 + zstd）里解出指定文件 |
| `gen-sc-offline-zh-names.py` | 用游戏语言包生成 `ship_names_zh.txt` 和 `place_names_zh.txt` |
| `sc-offline-zh-strings.py` | 菜单文字的中英对照表；套到上游英文 `src/` 的副本上即得到本分支的文字替换，可用来核对漏翻 |
| `build-macos.sh` | 在 macOS 上不用 MSVC 编译 `dinput8.dll` |

## 生成中文名文件

中文名来自游戏自带的简体中文语言包，所以不随本仓库分发，需要自己从游戏里生成（只读，不修改游戏文件）。
需要 Python 3.14 或更新版本（用到标准库的 `compression.zstd`）。

```bash
# 1. 从 Data.p4k 只读解出中英文语言包
python3 tools/zh-cn/p4k-extract.py "<游戏目录>/LIVE/Data.p4k" loc \
    'data/localization/chinese_(simplified)/global.ini' 'data/localization/english/global.ini'
# 2. 生成 ship_names_zh.txt 和 place_names_zh.txt 到 mod 的 data 目录（它会读取其中的 ships.txt）
python3 tools/zh-cn/gen-sc-offline-zh-names.py loc/Data/Localization <mod 的 data 目录>
```

命名规则：官方语言包里有中文名的直接用；没有的按官方“厂商 + 型号”的风格补译；同一艘船的不同变体
在后面用括号说明，例如 `神盾伊德里斯-M（AI·UEE·无内饰）`、`神盾伊德里斯-P（维克洛战争特别版）`。
游戏更新后重新生成一次即可跟上官方译名。

## 核对菜单文字

上游加了新文字后，可以把对照表套到上游源码的副本上，再和本分支比较，剩下的差异应当只有船名/地名/字体/传送这些代码改动：

```bash
mkdir -p /tmp/up && git archive upstream/main src | tar -x -C /tmp/up
python3 tools/zh-cn/sc-offline-zh-strings.py /tmp/up/src   # 找不到的字面量会全部列出
diff -ru -x third_party /tmp/up/src src
```

翻译时注意：MinGW 的 printf 不支持位置参数（`%2$s`），中文需要调换语序时改代码里实参的顺序。

## 编译（macOS）

```bash
tools/zh-cn/build-macos.sh
```

用 Homebrew 的 clang（MinGW 模式）+ mingw-w64 sysroot + Rust 工具链自带的 LLD，产物在 `build/zh-cn/dinput8.dll`，
脚本会检查它是 PE32+ x86-64 DLL、导出了转发到系统 `dinput8.DirectInput8Create` 的 `DirectInput8Create`、
导入表里没有 MinGW 运行库。源文件列表取自 `src/sc-offline-dll.vcxproj`。工具链路径可以用
`SC_OFFLINE_CXX`、`SC_OFFLINE_MINGW`、`SC_OFFLINE_RUST_TOOLCHAIN` 覆盖，输出目录用 `SC_OFFLINE_OUT`。

## 与 0.2.0-rc2 相比（给外部启动脚本）

- DLL 仍然叫 `dinput8.dll`，放在 `Bin64`，加载方式不变（Wine 下 `WINEDLLOVERRIDES=dinput8=n,b`）。
- DLL 读取的环境变量不变：`SC_OFFLINE_SHIPS_FILE`（它所在的目录就是 data 目录，`ship_names_zh.txt`、
  `place_names_zh.txt`、`font_zh.ttf` 都放这里）、`SC_OFFLINE_MOD_LOG`、`SC_OFFLINE_SPAWN_FILE`、
  `SC_OFFLINE_START`、`SC_OFFLINE_START_SHIP`、`SC_OFFLINE_BOOT_MAP`。
- `data/`：上游 0.2.0-rc5 出于版权原因删除了 `data/scripts/`（229 个 CIG 任务脚本），`contract_scripts.txt` 只剩 796 个
  不需要脚本的合同。本 fork 保留 0.2.0-rc2 的 `data/scripts/` 和完整的 2153 条 `contract_scripts.txt`（新列表是它的子集），
  除非收到权利人的删除要求。`missions.txt` 已删除（从来没有代码读它）。其余随附数据与 0.2.0-rc2 相同。
