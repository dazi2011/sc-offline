# 简体中文支持

本分支在 `scubamount/sc-offline@60a4ef6d` 的源码上加入了简体中文：

- **菜单汉化**：菜单、按钮、提示和状态栏都是简体中文。
- **中文字体**：依次尝试 `data\font_zh.ttf` / `data\font_zh.ttc`、macOS 的 `Hiragino Sans GB.ttc`
  （在 Wine/CrossOver 里经 `Z:` 盘读取）、Windows 的微软雅黑 / 黑体。想换字体，把字体文件放成
  `data\font_zh.ttf` 即可。
- **中文船名**：“载具”页按 `data\ship_names_zh.txt`（每行 `代号|中文名`）显示，鼠标悬停显示原始代号，
  搜索时中文名和代号都能匹配。刷船仍然使用原始代号。
- **中文地名**：“传送”页按 `data\place_names_zh.txt` 显示星系、行星、卫星、拉格朗日点和跳跃点的中文名。

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

## 编译

除了原有的 Visual Studio 工程，也可以不用微软 SDK，在 macOS/Linux 上用 clang 的 MinGW 模式交叉编译：
`clang++ --target=x86_64-w64-mingw32 --sysroot=<mingw-w64 sysroot> -std=c++20 -O2 -fms-extensions -mcrc32 ...`，
再用 LLD 链接成 `dinput8.dll`。
