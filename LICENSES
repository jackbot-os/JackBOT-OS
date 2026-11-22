# 第三方组件与许可证

本文件随扫描结果更新，权威明细见 `licenses/scan-report.md`（由 `tools/scan_licenses.py` 生成）与 `licenses/sbom.json`。

## 使用方式

每次拉取或升级 Chromium 后执行：

```powershell
python tools/scan_licenses.py src/chromium-main licenses
```

报告分四段：组件级统计、许可证文件统计、强 GPL/AGPL 组件（需剔除）、LGPL 组件（可保留但需履行义务）。

## 已确认的上游主体

| 组件 | 许可证 | 说明 |
|---|---|---|
| Chromium | BSD-3-Clause（主体） | Google 版权，保留 LICENSE 与版权头 |
| V8 | BSD-3-Clause | JavaScript 引擎，随 DEPS 拉取 |
| ICU | Unicode License | 国际化数据与算法，随 DEPS 拉取 |
| Skia | BSD-3-Clause | 2D 图形库，随 DEPS 拉取 |
| ANGLE | BSD-3-Clause | 图形接口转换层，随 DEPS 拉取 |
| Abseil | Apache-2.0 | C++ 基础库 |
| BoringSSL | OpenSSL/ISC 混合 | TLS 实现 |
| zlib / libpng / libjpeg-turbo | Zlib / libpng / IJG | 压缩与图像解码 |

## 待办

- [ ] 主仓库解压后跑首次扫描，填入真实统计
- [ ] 按 DEPS 补齐依赖后二次扫描
- [ ] 剔除清单确认后，把对应组件从构建配置里移除并记录到 `docs/`
- [ ] 发行物内附带本文件与 `licenses/` 报告
