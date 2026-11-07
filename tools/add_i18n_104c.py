#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""JackBOT OS 1.0.4 i18n batch C: window titles and missed strings.

Adds real English / Simplified Chinese / Traditional Chinese translations
for every built-in window title and a few strings missed in earlier batches.
Idempotent: existing keys and translations are left untouched.

Usage: python3 add_i18n_104c.py <path-to-i18n_data.json>
"""
import json
import sys

TRANS = {
    # ---- window titles ----
    "wt_bluetooth": ("Bluetooth & Lamps", "蓝牙与灯", "藍牙與燈"),
    "wt_3d": ("3D Studio - Software GPU", "3D 工作室 - 软件 GPU", "3D 工作室 - 軟體 GPU"),
    "wt_backup": ("Backup & Restore", "备份与还原", "備份與還原"),
    "wt_audio": ("Audio", "音频", "音訊"),
    "wt_about": ("About JackBOT", "关于 JackBOT", "關於 JackBOT"),
    "wt_battery": ("Battery & Power", "电池与电源", "電池與電源"),
    "wt_calc": ("Calculator", "计算器", "計算機"),
    "wt_capture": ("Screenshot", "截图", "截圖"),
    "wt_clock": ("Calendar & Clock", "日历与时钟", "日曆與時鐘"),
    "wt_devices": ("Device Manager", "设备管理器", "裝置管理員"),
    "wt_diskmgr": ("This PC", "此电脑", "此電腦"),
    "wt_files": ("File Explorer", "文件资源管理器", "檔案總管"),
    "wt_firewall": ("Firewall & Ports", "防火墙与端口", "防火牆與連接埠"),
    "wt_fontmgr": ("Font Manager", "字体管理器", "字型管理員"),
    "wt_jca": ("JCA Studio", "JCA 工作室", "JCA 工作室"),
    "wt_monitor": ("System Monitor", "系统监视器", "系統監視器"),
    "wt_network": ("Network", "网络", "網路"),
    "wt_notepad": ("Notepad", "记事本", "記事本"),
    "wt_power": ("Power", "电源", "電源"),
    "wt_pyconsole": ("Python Console", "Python 控制台", "Python 主控台"),
    "wt_recycle": ("Recycle Bin", "回收站", "資源回收筒"),
    "wt_region": ("Time Zone & Region", "时区与地区", "時區與地區"),
    "wt_store": ("App Store", "应用商店", "應用程式商店"),
    "wt_update": ("System Update", "系统更新", "系統更新"),
    "wt_usb": ("USB Devices", "USB 设备", "USB 裝置"),
    "wt_voice": ("Jbot Voice", "Jbot 语音", "Jbot 語音"),

    # ---- missed strings ----
    "3d_oom": ("3D: out of memory", "3D：内存不足", "3D：記憶體不足"),
}


def main():
    if len(sys.argv) < 2:
        print("usage: python3 add_i18n_104c.py <i18n_data.json>")
        sys.exit(1)

    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    keys = data["keys"]
    langs = data["langs"]
    en, zhcn, zhtw = langs[0], langs[1], langs[2]

    added = 0
    for k, (en_v, cn_v, tw_v) in TRANS.items():
        if k not in keys:
            keys.append(k)
            added += 1
        en["strings"].setdefault(k, en_v)
        zhcn["strings"].setdefault(k, cn_v)
        zhtw["strings"].setdefault(k, tw_v)

    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print("new keys added:", added)
    print("total keys now:", len(keys))


if __name__ == "__main__":
    main()
