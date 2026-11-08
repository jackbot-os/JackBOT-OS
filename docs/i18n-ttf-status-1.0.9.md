# JackBOT OS 1.0.9 — i18n / TTF 真实进度（2026-11-07 23:5x）

这份文件是给作者的"做到哪了"清单，只写实测结果，不写计划性措辞。

## 1. 语言：21 / 205 是"完整翻译"，其余 186 门被构建期剔除

`tools/gen_i18n.py` 现在的规则是硬的：除 `en` 外，任何语言只要缺任意一个 key，就把整门从生成的
`build/i18n_data.h` 里剔除，并在 stderr 打 `i18n: dropping N incomplete languages`。
所以 **ISO 里出现的语言数 = 真翻译完整的语言数**，不存在"英文占位"。

本次构建实际输出：`i18n: dropping 186 incomplete languages (first: aa, missing 363 keys)` /
`i18n: shipping 19 complete languages`。

21 门齐全（430 / 430 key）：

| # | code | 语言 | 实测取证 |
|---|------|------|----------|
| 1 | en | English | 基准 |
| 2 | zh-CN | 简体中文 | 桌面实拍 |
| 3 | zh-TW | 繁體中文 | — |
| 4 | zh-HK | 繁體（香港） | — |
| 5 | ja | 日本語 | `ja_labels.png` |
| 6 | ko | 한국어 | `ko_fix.png`（시작/배경 화면/설정/파일 탐색기） |
| 7 | de | Deutsch | — |
| 8 | fr | Français | `fr_desktop.png`（Démarrer/Fond d'écran/Paramètres） |
| 9 | es | Español | — |
| 10 | pt | Português | — |
| 11 | it | Italiano | — |
| 12 | ru | Русский | — |
| 13 | nl | Nederlands | — |
| 14 | pl | Polski | ASCII 转写（ą/ę/ł 未补） |
| 15 | tr | Türkçe | ASCII 转写（ş/ğ/ı 未补） |
| 16 | uk | Українська | `uk_desktop.png`（Пуск/Шпалери/Налаштування/Провідник） |
| 17 | cs | Čeština | — |
| 18 | sv | Svenska | `sv_desktop.png`（Start/Bakgrundsbild/Inställningar/Utforskaren） |
| 19 | fi | Suomi | — |
| 20 | da | Dansk | da_desktop.png（Start/Baggrund/Indstillinger/Stifinder） |
| 21 | id | Bahasa Indonesia | — |

尚未完整的 184 门各缺 363 条（账户/登录等 14 条已补，仍缺主体 363 条），合计约 6.7 万条。

## 2. TTF：库里 29 个，内核仍只加载 8 个，不是 205 个

`assets/fonts` 与 `iso/boot/fonts` 各 8 个真 `.ttf`（内核用 stb_truetype 运行时光栅化）：

| 字体 | 字节 | 覆盖 |
|------|------|------|
| SANS.ttf | 759,720 | 拉丁（含扩展）、西里尔、希腊、阿拉伯、希伯来 |
| MONO.ttf | 319,624 | 拉丁、西里尔、希腊、希伯来 |
| MONOB.ttf | 308,068 | 拉丁（粗体） |
| CJK.ttf | 4,033,420 | 仅 CJK（西里尔/希腊/阿拉伯/希伯来/天城文/泰文一个都不覆盖） |
| KR.ttf | 4,691,820 | 韩文、西里尔、希腊 |
| AR.ttf | 200,416 | 阿拉伯 |
| HI.ttf | 155,012 | 天城文 |
| TH.ttf | 77,100 | 泰文 |

用 fontTools 逐字核对（该门全部字符 vs `getBestCmap()`）：
ko → KR.ttf 缺 0；de/fr/es/pt/it/ru/nl/sv/fi → SANS.ttf 缺 0；
ja → CJK.ttf 缺 75、zh-CN/TW/HK → CJK.ttf 各缺 72，缺的全是拉丁字母/数字/括号，靠 SANS 回退，
截图里 `JackBOT OS`、`Jbot`、`Python`、`Browser` 正常显示即为回退生效的证据。

**缺口（明确没有）**：格鲁吉亚、亚美尼亚、埃塞俄比亚、孟加拉、泰米尔、泰卢固、古吉拉特、
奥里亚、僧伽罗、缅甸、高棉、老挝、藏、蒙古、彝、切罗基、加拿大原住民音节、Thaana/迪维希、
N'Ko、叙利亚、它加罗等文字，一个字体都没加。所以"205 个 TTF"这条**没有达成**，不粉饰。

## 3. 语言为什么能记住（本轮三个根因）

- 根因 A：`kernel/config.c` 原来按**序号**存语言（`L=<idx>`），语言表从 205 缩到 9 后序号全错位。
  已改为按代码存：新增 `lcode[12]`、`parse()` 认 `Q=<code>`、`config_save()` 写 `Q=`，并加
  `config_language_code()` / `config_set_language_code()`。
- 根因 B：Live 无盘启动跳过整块 `config_init()`，配置不加载。曾临时放开验证，串口打出
  `[I18N] code=ko` 但界面仍英文 → 说明还有一层，此条证伪。
- 根因 C（真凶）：`kernel/main.c` 语言自检块结束时无条件执行 `i18n_set_lang(saved_lang)`，
  用旧序号把刚设好的语言覆盖回 0（英文）。改为
  `if (saved_code[0]) i18n_set_code(saved_code); else i18n_set_lang(saved_lang);`。
  修完真机立刻出韩文桌面。

## 4. 黑格子（已修，已实拍）

`kernel/ttf.c` 的 `ttf_draw_cp()` 里把字形"覆盖度 0"的像素刷成面板底色的 `else` 分支已删除，
同时删掉任务栏开头的 `gfx_set_transparent(0)`。玻璃面板下不再出现深色方块。

## 5. 本版产物

- ISO：`_work/users1.iso`，32,061,440 B（15,655 sectors），
  sha256 `90B9B8DEF5C3FBF0516510D86276C4C76C001529F273BE8A61775C7C3AC224FC`
- 构建：`make iso` rc=0，0 error（Debian VM，远端仓库 `/home/jackbot/JackBOT-OS`）
- 串口：`[I18N] langs=0x...` / `[I18N] code=en` / `[I18N] test PASS`
- 预览：`_work/uk_desktop.png`（乌克兰语）、`_work/sv_desktop.png`（瑞典语）、
  `_work/ko_fix.png`（韩语）、`_work/fr_desktop.png`（法语），均为 VirtualBox 真机截图
- GitHub：`master` 已推到 `ca00d95`（前一提交 `493ba8e` = 17 门）

## 6. 还没做的（如实列出）

- 186 门语言未翻译；PL/TR 缺变音符号。
- 205 个 TTF 未达成，只有 8 个。
- `images/` 里 8 张官网截图仍是修复前版本，未重拍。
- 381 条硬编码英文字面量未清（清单 `_work/hardcoded_strings.json`）。
- 液态玻璃窗口体仍不透出桌面（`draw_glass_rect()` 只从壁纸缓冲混色）。

## 7. 本轮补充（23:2x–23:4x 追加）

- 新下载 21 个 OFL（Noto）字体到 `assets/fonts`：格鲁吉亚、亚美尼亚、埃塞俄比亚、孟加拉、泰米尔、
  泰卢固、古吉拉特、奥里亚、僧伽罗、缅甸、高棉、老挝、藏、蒙古、彝、切罗基、加拿大音节、Thaana、
  N'Ko、叙利亚、Tifinagh（另有约 15 个脚本在下载队列未被计入本版）。
- **诚实说明**：这 21 个字体只是"文件入库"，内核 `ttf.c` 的 `fb_disk/fb_pay` 表仍只挂
  CJK/KR/AR/TH/HI 五个回退面 + SANS/MONO/MONOB 三个主面，共 8 个真正参与渲染。
  把新字体接入渲染链需要改 payload 表并重生成内核内嵌 payload，本轮**未做**。
- 语言再 +2：丹麦语 `da`、印尼语 `id`，构建输出 `shipping 21 complete languages`，
  剩余 184 门各缺 363 条。
- 最终 ISO：32,061,440 B（15,655 sectors），sha256 `90B9B8DE...`（`make iso` 0 error）。
## 8. 205 个按语言 TTF（本轮）

**做法**：`_work/gen_lang_fonts2.py` 对 `tools/i18n_data.json` 的 205 门语言逐个生成字体：
按该语言母语名称 + 已翻译文本判定所用 Unicode 文字（汉字/谚文/西里尔/阿拉伯/天城文…），
在本地 OFL 源字体（`assets/fonts` + `_work/fonts_all` 共 40 余个 Noto/DejaVu/Droid）里选覆盖该文字最好的一个，
用 fontTools 按"该文字在本字体中的全部码位 + ASCII + 扩展拉丁"裁剪，输出 `assets/fonts/lang/<code>.ttf`。
汉字/谚文类超过 4000 字形时截断到 3000 + 实际用到的字符，避免单个字体几 MB。

**结果**：205 / 205 生成成功，合计 16.6 MB。抽证：
`uk` 379 字形 62 KB（Cyrl）、`ar` 1298 字形 155 KB（Arab）、`ja` 3481 字形 606 KB（Hani）、
`zh-CN` 3591 字形 629 KB、`am` 525 字形 282 KB（Ethi，来自 NotoSansEthiopic）、
`bo`/`dz` 来自 NotoSerifTibetan、`ko` 661 KB（Hang）、`ka` 175 字形 46 KB（Geor）。

**已复制到真正随 ISO 发布的字体目录**：`tools/prep_grub.sh` 末尾新增循环，把 205 个 TTF 以大写名
（`AA.TTF`…`ZU.TTF`）复制进 `iso/boot/fonts/`。构建日志 `lang fonts copied: 205`，
该目录现有 **210 个 TTF / 25 MB**（8 个原字体 + 205 个语言字体，有 3 个同名覆盖）。
ISO 从 32,061,440 B 涨到 **48,519,168 B（23,691 sectors）**，`make iso` 0 error。

**内核侧（已改，尚未在安装态验证）**：`kernel/ttf.c` 新增 `dyn_load()`——按当前语言代码
拼 `<CODE>.TTF`，走 `load_file()` 从 FAT 盘读入并 `open_face()`，挂在 `src_face()` 回退链最后一级
（keyfont 30），首次命中会往串口打 `[TTF] lang font XX.TTF loaded`。
**边界**：Live 模式从光盘启动、没有 FAT 盘，这条路径不会触发；装到硬盘后，installer 目前只把
8 个字体写进数据盘，**205 个语言字体还没有被 installer 写到用户盘**——所以"内核真按语言加载字体"
这一条目前只在代码层完成，没到可用状态。下一步是让 installer 从 ISO 的 /boot/fonts 把 205 个 TTF
一并写入数据盘分区。

**已知缺口**：4 门曾因缺源字体只有 1 个字形（am/ti/bo/dz），补下 NotoSerifTibetan 与
NotoSansEthiopic 后已重生成（am 525 字形）；报告 `docs/fonts-per-language.json` 记录了每门的
src/scripts/glyphs/size。