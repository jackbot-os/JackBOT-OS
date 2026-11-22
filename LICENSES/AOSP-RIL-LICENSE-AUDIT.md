# AOSP RIL 组件许可审计

日期：2026-11-21
目录：`E:\JackBOT-OS\arm\vendor\aosp\ril`

## 1. 取得方式

- 首选源 `https://android.googlesource.com/platform/hardware/ril` 在本机网络下连不通（curl 28，连接超时），已放弃。
- 实际取得：GitHub 上 AOSP 的官方镜像 `aosp-mirror/platform_hardware_ril`，分支 `main`，打为 `ril.tar.gz`（176,963 字节）后解包。

## 2. 内容清点

| 项目 | 数值 |
|---|---|
| 文件总数 | 53 |
| C/C++/构建源文件 | 41 |
| 含 Apache-2.0 头 | 38 |
| 含 GPL/GNU 字样 | **0** |

不含许可头的 3 个文件：`Android.bp`（2 个，Soong 构建脚本）、`sap-api.proto`（SAP 接口定义）。三者均为 AOSP 工程内文件，随工程整体按 Apache-2.0 分发。

顶层目录：`include/`、`libril/`、`librilutils/`、`reference-ril/`、`rild/`，另有 `Android.bp`、`CleanSpec.mk`、`METADATA`、`OWNERS`。

说明：归档里的 `reference-ril/ril.h` 是一条指向 `include/telephony/ril.h` 的符号链接（tar 类型 2，内容 0 字节）。Windows 无法建立该链接，已按链接语义把目标文件复制进去（237,000 字节），内容与 `include/telephony/ril.h` 一致。

## 3. 许可结论

- 本目录**没有 GPL 成分**。
- `libril/NOTICE` 为 Apache-2.0 全文，按 Apache-2.0 第 4 条要求保留；各源文件的版权头同样保留，这是许可的强制条件，不属于额外添加的来源说明，因此不做删改。
- 除上述强制保留内容外，未额外添加任何"搬运自某处"的说明文字。

## 4. 明确未引入的部分

| 组件 | 原因 |
|---|---|
| Binder 内核驱动（`drivers/android/binder.c`） | Linux 内核 GPL-2.0，按项目规则不取 |
| 厂商 RIL `.so`（移远/华为） | 专有许可，且需硬件配套 |
| Linux 内核其它电话相关代码 | GPL-2.0 |

## 5. 当前状态

仅完成下载、解包与审计，**尚未接入 `Makefile.arch64` 构建**，也未改动任何现有源文件。

## 6. 下一步的硬约束（不隐藏）

这套代码是 Android 的 RIL 接口层与参考实现，运行前提是 Binder IPC 与 Android 属性服务；本目录不提供这两者，且它们的内核部分属 GPL。因此在本目录之上，可以直接复用的是 `ril.h` 的接口定义与命令枚举，不能直接产出可运行的电话栈。真正拨号所需的 modem 硬件同样不在本目录范围内。
