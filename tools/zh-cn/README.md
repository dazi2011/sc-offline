# 简体中文支持

本分支（`zh-cn`，也是 fork 的 `main`）在 `scubamount/sc-offline` 0.7.7（sco-core SDK 架构，`external/sco-core`
子模块钉在 `sdk-v2.0.0`）的源码上加入了简体中文：

- **界面汉化**：菜单外壳、各内置插件的标签页和标签标题、状态栏和它显示的出错原因、“多人”页、creative 插件的回复、
  Discord 状态都是简体中文。`mod.log`、命令的标题/帮助/回复、配置键、文件名、类名和写到磁盘的数据不翻译。
  Windows 启动器（`launcher/`）没有翻译：它会改 hosts、改名 EAC、加防火墙规则，在 CrossOver 里不要用它。
- **完整状态栏**：sco-core 的 `sco::Status` 只保留 255 字节（中文约 85 个字）；状态栏显示最近一次状态的完整内容，
  交给 sco-core 的那份在字符边界截断。sco-core 插件加载器自己的三条状态（插件崩溃/出错/无法释放）也显示为中文。
- **中文字体**：依次尝试 `data\font_zh.ttf` / `data\font_zh.ttc`、macOS 的 `Hiragino Sans GB.ttc` /
  `STHeiti Medium.ttc`（在 Wine/CrossOver 里经 `Z:` 盘读取）、Windows 的微软雅黑 / 黑体；都找不到时只用英文字体。
  只改宿主（`dinput8.dll`）的字体图集，用 ImGui 画界面的插件共用它。
- **中文船名**：“载具”页按 `data\ship_names_zh.txt`（每行 `类名|中文名`）显示，鼠标悬停显示类名，
  搜索时中文名和类名都能匹配。刷船仍然使用类名。
- **中文地名**：“传送”页按 `data\place_names_zh.txt` 显示星系、行星、卫星、拉格朗日点、跳跃点以及扫描到的
  小地点的中文名。传送仍然使用实体名。
- **跨星系传送**：“传送”页列表里的地点可以跨星系传送（实验功能，需用 `PU_All` 启动）；只有找不到目标所在星系时才报错，
  `mod.log` 记 `[tp] crossing star systems`。F8 和已保存的位置仍只能在当前星系内使用。上游 0.7.7 仍然拒绝跨星系。
- **任务脚本与完整合同列表**：保留 `data/scripts/`（229 个 CIG 任务脚本）和 2153 条的 `contract_scripts.txt`
  （上游的 796 条是它的子集），除非权利人要求删除。
- **creative 默认开启**：穿墙飞行、无敌模式、无限弹药（`data\plugins\creative`）不再带 `disabled` 标记，上游默认关闭。
  “玩家”页的提示相应改写。在该文件夹里放一个空的 `disabled` 文件即可关闭。
- **派罗/尼克斯合同**：用 `PU_All` 启动且启动补丁生效时，mobiGlas 也列出派罗和尼克斯的合同；`PU` 启动时照旧不列，
  `mod.log` 的 `[contracts]` 行写明是哪种情况。

## 文件

| 文件 | 作用 |
| --- | --- |
| `build-macos.sh` | 在 macOS 上不用 MSVC 编译 `dinput8.dll` 和四个插件（creative、discord、npc、inspector），并检查产物 |
| `mingw/toolchain.cmake`、`mingw/CMakeLists.txt` | clang MinGW 的 CMake 工具链和工程（`build-macos.sh` 调用它们） |
| `sc-offline-zh-strings.py` | 界面文字的中英对照表；套到上游英文源码的副本上即得到本分支的文字替换，可用来核对漏翻 |
| `p4k-extract.py` | 只读地从 `Data.p4k`（zip64 + zstd）里解出指定文件 |
| `gen-sc-offline-zh-names.py` | 用游戏语言包生成 `ship_names_zh.txt` 和 `place_names_zh.txt` |

## 编译（macOS）

```bash
git submodule update --init      # sco-core 源码；只从源码编译，不用任何预编译产物
tools/zh-cn/build-macos.sh
```

需要 Homebrew 的 `llvm`（clang）和 `mingw-w64`、Rust 工具链（用它自带的 LLD）、CMake 3.20 以上
（`/Applications/CMake.app` 自带的也行）。脚本用 `tools/zh-cn/mingw` 这个 CMake 工程编译，
dinput8 和 ImGui 的源文件列表从顶层 `CMakeLists.txt` 读取（上游加了文件这里自动跟上），sco-core 用它自己的
`CMakeLists.txt`。顶层 `CMakeLists.txt` 只支持 MSVC，本分支没有改它。

产物在 `build/zh-cn/`：

| 产物 | 放到哪里 |
| --- | --- |
| `dinput8.dll` | 游戏的 `Bin64` |
| `data/plugins/creative/`（`creative.dll`、`plugin.ini`） | mod 的 `data\plugins\creative\` |
| `data/plugins/discord/`、`data/plugins/npc/`、`data/plugins/inspector/`（各自的 dll 和 `plugin.ini`） | mod 的 `data\plugins\<id>\` |
| `BUILD-INFO.txt` | 提交号、sco-core 提交号、工具链版本、每个 dll 的 SHA-256 |

脚本会检查：每个 dll 都是 PE32+ x86-64 DLL、导入表里没有 MinGW 运行库（`libstdc++`/`libgcc`/`libwinpthread`）；
`dinput8.dll` 导出转发到 `C:\Windows\System32\dinput8.DirectInput8Create` 的 `DirectInput8Create`、
内嵌 `SegmentHeap` 清单（资源 2）和版本信息、不含可选桥（titanlink/voxel）和 npc/inspector 的命令；
插件导出 `sco_plugin_query/load/unload`。工具链路径可以用 `SC_OFFLINE_LLVM`（或 `SC_OFFLINE_CXX`）、
`SC_OFFLINE_MINGW`、`SC_OFFLINE_RUST_TOOLCHAIN`、`SC_OFFLINE_CMAKE` 覆盖，输出目录用 `SC_OFFLINE_OUT`。

和 MSVC 构建的区别：clang 需要 `-fms-extensions`（`__try`）、`-mcrc32`、`-femulated-tls`（与 mingw-w64 的
libstdc++ 一致）；运行库静态链接，导入的是系统的 UCRT（`api-ms-win-crt-*`），Wine 自带。
npc 和 inspector 在上游用发布的 SDK 压缩包编译，这里直接用 sco-core 里同样的公共头文件编译。

提交前再跑上游的检查：

```bash
tools/check.sh                 # clang 语法检查 + C2712 筛查（本分支基线为空）
tools/no-scans.sh              # src/ 下不能有自己的特征码扫描
python3 tools/sdk-headers.py   # src/builtins 只能包含 SDK 公共头
```

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
在后面用括号说明，例如 `神盾伊德里斯-M（AI·UEE·无内饰）`。游戏更新后重新生成一次即可跟上官方译名。

## 核对界面文字

上游加了新文字后，把对照表套到上游源码的副本上，再和本分支比较。剩下的差异应当只有代码改动：
状态栏、字体、船名、地名、跨星系传送、派罗/尼克斯合同和 MinGW 编译兼容。

```bash
mkdir -p /tmp/up && git archive upstream/main src plugins | tar -x -C /tmp/up
python3 tools/zh-cn/sc-offline-zh-strings.py /tmp/up   # 找不到的字面量会全部列出
git diff --no-index /tmp/up/src src; git diff --no-index /tmp/up/plugins plugins
```

翻译时注意：MinGW 的 printf 不支持位置参数（`%2$s`），中文需要调换语序时改代码里实参的顺序（对照表的 `EXTRA`）。
标签标题（sco.ui）限 1–63 字节。命令的参数名（如 creative 的 `on`）不能翻。

## 给外部启动脚本（与 0.7.0 相比）

- **DLL**：仍然叫 `dinput8.dll`，放在 `Bin64`，加载方式不变（Wine 下 `WINEDLLOVERRIDES=dinput8=n,b`）。
- **data 目录**：仍然是 `SC_OFFLINE_SHIPS_FILE` 所在的目录。新增的子目录：
  - `data\plugins\<id>\`：插件（`<id>.dll` + `plugin.ini`）。本 fork 的 creative 默认开启（上游带一个名为 `disabled` 的
    空文件，默认关闭）；在它的文件夹里放一个空的 `disabled` 文件即可关闭；
  - `data\builtin\quantum\datacore\quantum_drive.toml`：新量子引擎的数据包（取代 0.7.0 的 `dcb_patch.h` 字节补丁），
    **每次升级都要覆盖**，不能用 `rsync --ignore-existing` 留旧版；
  - `data\storage\*.db`：存档（sco.storage/SQLite），游戏运行时创建，首次运行时导入旧的 `wallet.txt`、`spawn.txt`、
    `bookmarks.txt`；之后手改 `wallet.txt` 仍然有效。升级时不要覆盖或删除。
  - `ship_names_zh.txt`、`place_names_zh.txt`、`font_zh.ttf` 仍然放在 data 目录。
- **环境变量**（DLL 读取的；sc-offline.exe 会按 `sc-offline.ini` 设置，CrossOver 里由启动脚本设置）：

  | 变量 | 作用 | 未设置时 | 启动器默认 |
  | --- | --- | --- | --- |
  | `SC_OFFLINE_SHIPS_FILE` | `ships.txt` 的路径，它所在的目录就是 data 目录 | 没有 data 目录：不加载插件、没有存档 | `data\ships.txt` |
  | `SC_OFFLINE_MOD_LOG` | `mod.log` 路径 | 不写日志 | `data\mod.log` |
  | `SC_OFFLINE_SPAWN_FILE` | 旧的 F7 位置文件：首次运行导入 `data\storage`，存储不可用时作后备 | — | `data\spawn.txt` |
  | `SC_USER` | 游戏的 `USER\Client\0` 目录（装备/服装 XML、玩家数据） | 装备菜单不可用 | 自动 |
  | `SC_OFFLINE_BOOT_MAP` | `PU` 或 `PU_All` | 不改启动地图 | `PU_All` |
  | `SC_OFFLINE_START`、`SC_OFFLINE_START_SHIP` | `Daymar` 时在戴玛上空出生 | — | 空 |
  | `SC_OFFLINE_PLUGINS` | `on` 才加载 `data\plugins` 里的插件（**新增，必须设**） | **off** | on |
  | `SC_OFFLINE_MULTIPLAYER` | `off` 关闭“多人”页 | on | on |
  | `SC_OFFLINE_MULTIPLAYER_ALLOW` | 局域网之外允许的网段（VPN） | 只限局域网 | 空 |
  | `SC_OFFLINE_ASOP` | ASOP 舰船终端、个人机库、机库电梯、ATC | on | on |
  | `SC_OFFLINE_ASOP_FLEET_LIST` | 终端列出 `ships`（`data\ships.txt`）还是 `game`（离线为空） | **game** | ships |
  | `SC_OFFLINE_MINING`、`SC_OFFLINE_MINING_DEBUG` | 自然采矿（实验） | off | off |

  0.7.0 的 `SC_OFFLINE_DCB_PATCH` 已经没有了（上游删掉了字节补丁）。
