#!/usr/bin/env python3
# One-off 1.0.4 i18n expansion: add user-visible strings with real
# English / Simplified Chinese / Traditional Chinese translations.
# Other 202 languages fall back to English inside gen_i18n.py.
import json, sys

JSON_PATH = sys.argv[1]

# key: (English, Simplified Chinese, Traditional Chinese)
TRANS = {
    # ---- splash ----
    "splash_subtitle": (
        "Welcome to JackBOT OS",
        "欢迎使用 JackBOT OS",
        "歡迎使用 JackBOT OS"),

    # ---- settings read-only card ----
    "set_lang_hint": (
        "Set by the installer; cannot be changed here.",
        "由安装程序设置，无法在此更改。",
        "由安裝程式設定，無法在此變更。"),

    # ---- about ----
    "about_subtitle": (
        "A self-built 64-bit operating system - x86_64 architecture",
        "自研 64 位操作系统  x86_64 架构",
        "自研 64 位作業系統  x86_64 架構"),
    "about_feat1": (
        "· Localized GUI with anti-aliased vector fonts",
        "· 中文图形界面，真矢量字体抗锯齿",
        "· 中文圖形介面，真向量字型抗鋸齒"),
    "about_feat2": (
        "· Graphical setup wizard: real partitioning and formatting",
        "· 图形化安装向导：真实分区与格式化",
        "· 圖形化安裝精靈：真實分割與格式化"),
    "about_feat3": (
        "· Drive letters; recognizes NTFS / Linux partitions",
        "· 盘符系统，可识别 NTFS / Linux 等分区",
        "· 磁碟機代號，可辨識 NTFS / Linux 等分割"),
    "about_feat4": (
        "· Mouse, windows, multitasking and process isolation",
        "· 鼠标、窗口、多任务与进程隔离",
        "· 滑鼠、視窗、多工與程序隔離"),
    "about_feat5": (
        "· Network stack and a built-in browser",
        "· 网络协议栈与内置浏览器",
        "· 網路協定堆疊與內建瀏覽器"),
    "about_feat6": (
        "· App store, screenshots, wallpapers and themes",
        "· 应用商店、截图、壁纸与主题",
        "· 應用程式商店、截圖、桌布與主題"),
    "about_third": (
        "Third-party components",
        "第三方组件",
        "第三方元件"),
    "about_footer": (
        "JackBOT (c) 2026 - For learning and VM testing only",
        "JackBOT (c) 2026  仅用于学习与虚拟机测试",
        "JackBOT (c) 2026  僅用於學習與虛擬機測試"),

    # ---- monitor ----
    "mon_total": (
        "Total memory ",
        "总内存 ",
        "總記憶體 "),
    "mon_used": (
        "Used ",
        "已用 ",
        "已用 "),
    "mon_free": (
        "Available ",
        "可用 ",
        "可用 "),
    "mon_usage": (
        "Usage ",
        "使用率 ",
        "使用率 "),
    "mon_uptime": (
        "Uptime ",
        "运行时间 ",
        "執行時間 "),
    "mon_resolution": (
        "Resolution ",
        "分辨率 ",
        "解析度 "),

    # ---- recycle bin ----
    "rec_emptied": (
        "Recycle Bin emptied",
        "回收站已清空",
        "資源回收筒已清空"),
    "rec_empty_fail": (
        "Failed to empty (disk not ready)",
        "清空失败（磁盘未就绪）",
        "清空失敗（磁碟未就緒）"),
    "rec_items": (
        "Recycle Bin items ",
        "回收站项目 ",
        "資源回收筒項目 "),
    "rec_using": (
        " items, using ",
        " 个，占用 ",
        " 個，佔用 "),
    "rec_empty": (
        "The Recycle Bin is empty",
        "回收站是空的",
        "資源回收筒是空的"),
    "rec_hint": (
        "Files deleted in File Explorer are kept here and can be restored anytime.",
        "在文件管理器里删除的文件会先放到这里，可随时还原。",
        "在檔案總管裡刪除的檔案會先放到這裡，可隨時還原。"),
    "rec_restored": (
        "File restored",
        "已还原文件",
        "已還原檔案"),
    "rec_restore_fail": (
        "Restore failed (target may already exist, or disk problem)",
        "还原失败（目标可能已存在或磁盘问题）",
        "還原失敗（目標可能已存在或磁碟問題）"),

    # ---- file explorer ----
    "file_read_fail": (
        "Unable to read this file",
        "无法读取该文件",
        "無法讀取此檔案"),
    "file_unsupported": (
        "This file type cannot be opened yet (text/source/script files can be opened)",
        "该文件类型暂不支持打开（可打开文本/源码/脚本）",
        "此檔案類型暫不支援開啟（可開啟文字/原始碼/指令碼）"),
    "file_no_folder": (
        "The Recycle Bin handles files only; folders cannot be deleted yet",
        "回收站只处理文件，暂不支持删除文件夹",
        "資源回收筒只處理檔案，暫不支援刪除資料夾"),
    "file_moved": (
        "Moved to the Recycle Bin",
        "已移入回收站",
        "已移入資源回收筒"),
    "file_too_big": (
        "File is over 8MB or protected; cannot be sent to the Recycle Bin",
        "文件超过 8MB 或受保护，无法送入回收站",
        "檔案超過 8MB 或受保護，無法送入資源回收筒"),
    "file_del_fail": (
        "Delete failed (protected or disk problem)",
        "删除失败（受保护或磁盘问题）",
        "刪除失敗（受保護或磁碟問題）"),
    "file_location": (
        "Location: ",
        "位置：",
        "位置："),
    "file_root": (
        "\\ Root directory",
        "\\ 根目录",
        "\\ 根目錄"),
    "file_empty": (
        "(empty directory)",
        "（空目录）",
        "（空目錄）"),
    "file_count": (
        "Items ",
        "项目数 ",
        "項目數 "),

    # ---- battery & power ----
    "bat_detected": (
        "Battery device detected (control-method battery PNP0C0A)",
        "检测到电池设备（控制法电池 PNP0C0A）",
        "偵測到電池裝置（控制法電池 PNP0C0A）"),
    "bat_aml": (
        "Charge percentage/status needs an AML interpreter; under development",
        "电量百分比/充电状态需要 AML 解释器，开发中",
        "電量百分比/充電狀態需要 AML 解譯器，開發中"),
    "bat_no_battery": (
        "No battery detected: desktop / VM, running on external power",
        "未检测到电池：台式机 / 虚拟机，使用外接电源",
        "未偵測到電池：桌上型電腦 / 虛擬機，使用外接電源"),
    "bat_laptop_hint": (
        "On a real laptop, battery and charging status will appear here",
        "笔记本实机上会在此显示电池与充电状态",
        "筆記型電腦實機上會在此顯示電池與充電狀態"),
    "bat_no_acpi": (
        "No ACPI tables found (power information unavailable)",
        "未找到 ACPI 表（电源信息不可用）",
        "找不到 ACPI 表（電源資訊無法使用）"),
    "bat_rev": (
        "ACPI revision ",
        "ACPI 修订 ",
        "ACPI 修訂 "),
    "bat_tables": (
        ", tables ",
        "，表数量 ",
        "，表數量 "),
    "bat_sci": (
        ", SCI interrupt ",
        "，SCI 中断 ",
        "，SCI 中斷 "),
    "bat_ec": (
        "Embedded controller (EC): ",
        "嵌入式控制器(EC)：",
        "內嵌控制器(EC)："),
    "bat_present": (
        "Detected",
        "检测到",
        "偵測到"),
    "bat_none": (
        "None",
        "无",
        "無"),
    "bat_ac": (
        "    AC power: ",
        "    交流电源：",
        "    交流電源："),
    "bat_pm1": (
        "PM1 control block: ",
        "PM1 电源控制块：",
        "PM1 電源控制區塊："),
    "bat_unavailable": (
        "Not available",
        "未取得",
        "未取得"),
    "bat_way": (
        "Soft power-off method: ",
        "软关机方式：",
        "軟關機方式："),
    "bat_s5": (
        "ACPI S5 sleep register (real)",
        "ACPI S5 睡眠寄存器（真实）",
        "ACPI S5 睡眠暫存器（真實）"),
    "bat_enum": (
        "ACPI enumerated SLP_TYP (compatibility)",
        "ACPI 枚举 SLP_TYP（兼容）",
        "ACPI 列舉 SLP_TYP（相容）"),
    "bat_footer": (
        "Shutdown writes SLP_TYP|SLP_EN to the FADT PM1 control block first; works on both VMs and real machines.",
        "关机优先向 FADT 的 PM1 控制块写 SLP_TYP|SLP_EN，兼容虚拟机与真机。",
        "關機優先向 FADT 的 PM1 控制區塊寫入 SLP_TYP|SLP_EN，相容虛擬機與實機。"),

    # ---- USB app ----
    "usb_ctrl": (
        "Controllers ",
        "控制器 ",
        "控制器 "),
    "usb_conn": (
        ", connected devices ",
        " 个，已连接设备 ",
        " 個，已連接裝置 "),
    "usb_units": (
        "",
        " 台",
        " 台"),
    "usb_no_hc": (
        "No USB host controller detected (enable a USB controller in the VM settings)",
        "未检测到 USB 主控制器（可在虚拟机设置里启用 USB 控制器）",
        "未偵測到 USB 主控制器（可在虛擬機設定裡啟用 USB 控制器）"),
    "usb_bus": (
        "Bus ",
        "总线 ",
        "匯流排 "),
    "usb_port": (
        "  Port ",
        "  端口 ",
        "  連接埠 "),
    "usb_port_conn": (
        " connected ",
        " ● 已连接  ",
        " ● 已連接  "),
    "usb_port_empty": (
        " empty",
        " ○ 空",
        " ○ 空"),
    "usb_overcurrent": (
        "Overcurrent!",
        "过流!",
        "過電流!"),
    "usb_no_power": (
        "No power",
        "未供电",
        "未供電"),
    "usb_other": (
        "  other ",
        "  其余 ",
        "  其餘 "),
    "usb_skip": (
        " ports omitted",
        " 个端口略",
        " 個連接埠略"),
    "usb_footer": (
        "Note: port status is read directly from the host controller root hub registers; full device enumeration (HID/USB drives) comes in a later stage.",
        "说明：端口状态直接读取主控根集线器寄存器；完整设备枚举（HID/U盘）在后续阶段。",
        "說明：連接埠狀態直接讀取主控根集線器暫存器；完整裝置列舉（HID/USB 隨身碟）在後續階段。"),

    # ---- device manager PCI class names ----
    "pc_ide": (
        "IDE storage controller",
        "IDE 存储控制器",
        "IDE 儲存控制器"),
    "pc_sata": (
        "SATA/AHCI controller",
        "SATA/AHCI 控制器",
        "SATA/AHCI 控制器"),
    "pc_nvme": (
        "NVMe storage controller",
        "NVMe 存储控制器",
        "NVMe 儲存控制器"),
    "pc_storage": (
        "Storage controller",
        "存储控制器",
        "儲存控制器"),
    "pc_ethernet": (
        "Ethernet controller",
        "以太网控制器",
        "乙太網路控制器"),
    "pc_network": (
        "Network controller",
        "网络控制器",
        "網路控制器"),
    "pc_vga": (
        "VGA display controller",
        "VGA 显示控制器",
        "VGA 顯示控制器"),
    "pc_3d": (
        "3D display controller",
        "3D 显示控制器",
        "3D 顯示控制器"),
    "pc_display": (
        "Display controller",
        "显示控制器",
        "顯示控制器"),
    "pc_audio": (
        "Audio/multimedia controller",
        "音频/多媒体控制器",
        "音訊/多媒體控制器"),
    "pc_memory": (
        "Memory controller",
        "内存控制器",
        "記憶體控制器"),
    "pc_host_bridge": (
        "Host bridge",
        "主机桥（Host Bridge）",
        "主機橋（Host Bridge）"),
    "pc_isa": (
        "ISA bridge",
        "ISA 桥",
        "ISA 橋"),
    "pc_pci_bridge": (
        "PCI-PCI bridge",
        "PCI-PCI 桥",
        "PCI-PCI 橋"),
    "pc_bridge": (
        "Bridge device",
        "桥设备",
        "橋接裝置"),
    "pc_comm": (
        "Communication controller",
        "通信控制器",
        "通訊控制器"),
    "pc_sys_periph": (
        "Generic system peripheral",
        "通用系统外设",
        "通用系統週邊"),
    "pc_input": (
        "Input controller",
        "输入控制器",
        "輸入控制器"),
    "pc_usb": (
        "USB controller",
        "USB 控制器",
        "USB 控制器"),
    "pc_smbus": (
        "SMBus",
        "SMBus 总线",
        "SMBus 匯流排"),
    "pc_serial_bus": (
        "Serial bus controller",
        "串行总线控制器",
        "序列匯流排控制器"),
    "pc_wireless": (
        "Wireless controller",
        "无线控制器",
        "無線控制器"),
    "pc_crypto": (
        "Encryption controller",
        "加密控制器",
        "加密控制器"),
    "pc_signal": (
        "Signal processing controller",
        "信号处理控制器",
        "訊號處理控制器"),
    "pc_other": (
        "Other device",
        "其他设备",
        "其他裝置"),
    "dev_pci": (
        "PCI devices ",
        "PCI 设备 ",
        "PCI 裝置 "),
    "dev_click": (
        " (click to view resources)",
        " 个（点击查看资源）",
        " 個（點擊查看資源）"),
    "dev_no_res": (
        "No I/O / memory resources",
        "无 I/O / 内存资源",
        "無 I/O / 記憶體資源"),

    # ---- this PC / disk manager ----
    "pt_fat": (
        "FAT partition",
        "FAT 分区",
        "FAT 分割"),
    "pt_ntfs": (
        "NTFS / exFAT",
        "NTFS / exFAT",
        "NTFS / exFAT"),
    "pt_extended": (
        "Extended partition",
        "扩展分区",
        "延伸分割"),
    "pt_linux": (
        "Linux partition",
        "Linux 分区",
        "Linux 分割"),
    "pt_swap": (
        "Linux swap",
        "Linux 交换",
        "Linux 交換"),
    "pt_other": (
        "Other system",
        "其他系统",
        "其他系統"),
    "disk_header": (
        "Drive     Type               Capacity     Boot   Status",
        "盘符     类型                容量        启动   状态",
        "磁碟機   類型                容量        啟動   狀態"),
    "disk_yes": (
        "Yes",
        "是",
        "是"),
    "disk_no": (
        "No",
        "否",
        "否"),
    "disk_access": (
        "Accessible",
        "可访问",
        "可存取"),
    "disk_visible": (
        "Visible only",
        "仅可见",
        "僅可見"),
    "disk_no_part": (
        "No usable partitions detected (no hard disk may be installed)",
        "未检测到可用分区（可能没有安装硬盘）",
        "未偵測到可用分割（可能沒有安裝硬碟）"),
    "disk_note1": (
        "Note: drive letters start at C; other systems such as Windows/NTFS, Linux and extended partitions are all listed.",
        "说明：盘符从 C 开始分配；Windows/NTFS、Linux、扩展分区等其他系统分区都会列出。",
        "說明：磁碟機代號從 C 開始；Windows/NTFS、Linux、延伸分割等其他系統分割都會列出。"),
    "disk_note2": (
        "FAT partitions can be read and written; other formats are visible only for now; write support is under development.",
        "FAT 分区可读写；其他格式当前仅可见，写入支持在开发中。",
        "FAT 分割可讀寫；其他格式目前僅可見，寫入支援開發中。"),

    # ---- USB controller kind / speed ----
    "uk_uhci": (
        "UHCI  USB 1.1 host controller",
        "UHCI  USB 1.1 主控",
        "UHCI  USB 1.1 主控"),
    "uk_ohci": (
        "OHCI  USB 1.1 host controller",
        "OHCI  USB 1.1 主控",
        "OHCI  USB 1.1 主控"),
    "uk_ehci": (
        "EHCI  USB 2.0 host controller",
        "EHCI  USB 2.0 主控",
        "EHCI  USB 2.0 主控"),
    "uk_xhci": (
        "xHCI  USB 3.x host controller",
        "xHCI  USB 3.x 主控",
        "xHCI  USB 3.x 主控"),
    "uk_unknown": (
        "USB host controller (unknown interface)",
        "USB 主控（接口未知）",
        "USB 主控（介面未知）"),
    "sp_low": (
        "Low speed 1.5M",
        "低速 1.5M",
        "低速 1.5M"),
    "sp_full": (
        "Full speed 12M",
        "全速 12M",
        "全速 12M"),
    "sp_high": (
        "High speed 480M",
        "高速 480M",
        "高速 480M"),
    "sp_super": (
        "Super speed 5G",
        "超高速 5G",
        "超高速 5G"),
    "sp_empty": (
        "Empty",
        "空",
        "空"),

    # ---- desktop shell functional words ----
    "ctx_wallpaper": (
        "Wallpaper",
        "壁纸",
        "桌布"),
    "ctx_settings": (
        "Settings",
        "设置",
        "設定"),
    "ctx_refresh": (
        "Refresh",
        "刷新",
        "重新整理"),
    "ctx_about": (
        "About",
        "关于",
        "關於"),
    "start_button": (
        "Start",
        "开始",
        "開始"),
    "send_button": (
        "Send",
        "发送",
        "傳送"),
    "search_hint": (
        "Type to search...",
        "输入以搜索...",
        "輸入以搜尋..."),
    "no_matches": (
        "No matches",
        "无匹配",
        "無符合項目"),

    # ---- Start menu app names ----
    "an_power": ("Power", "电源", "電源"),
    "an_store": ("App Store", "应用商店", "應用程式商店"),
    "an_firewall": ("Firewall", "防火墙", "防火牆"),
    "an_backup": ("Backup", "备份", "備份"),
    "an_capture": ("Screenshot", "截图", "截圖"),
    "an_calendar": ("Calendar", "日历", "日曆"),
    "an_fontmgr": ("Font Manager", "字体管理器", "字型管理員"),
    "an_region": ("Time Zone", "时区", "時區"),
    "an_network": ("Network", "网络", "網路"),
    "an_audio": ("Audio", "音频", "音訊"),
    "an_update": ("System Update", "系统更新", "系統更新"),
    "an_browser": ("JBrowser", "JBrowser", "JBrowser"),
    "an_3dstudio": ("3D Studio", "3D 工作室", "3D 工作室"),
    "an_diskmgr": ("This PC", "此电脑", "此電腦"),
    "an_about": ("About", "关于", "關於"),
    "an_calc": ("Calculator", "计算器", "計算機"),
    "an_notepad": ("Notepad", "记事本", "記事本"),
    "an_monitor": ("System Monitor", "系统监视器", "系統監視器"),
    "an_python": ("Python", "Python", "Python"),
    "an_jca": ("JCA Studio", "JCA 工作室", "JCA 工作室"),
    "an_usb": ("USB Devices", "USB 设备", "USB 裝置"),
    "an_battery": ("Battery & Power", "电池与电源", "電池與電源"),
    "an_recycle": ("Recycle Bin", "回收站", "資源回收筒"),
    "an_files": ("File Explorer", "文件资源管理器", "檔案總管"),
    "an_devices": ("Device Manager", "设备管理器", "裝置管理員"),
    "an_voice": ("Jbot Voice", "Jbot 语音", "Jbot 語音"),
    "an_bluetooth": ("Bluetooth", "蓝牙", "藍牙"),
    "an_snake": ("Snake", "贪吃蛇", "貪食蛇"),
    "an_g2048": ("2048", "2048", "2048"),
}

with open(JSON_PATH, encoding="utf-8") as f:
    data = json.load(f)

existing = set(data["keys"])
added = 0
for key, (en, cn, tw) in TRANS.items():
    if key in existing:
        continue
    data["keys"].append(key)
    added += 1

# langs[0]=en, langs[1]=zh-CN, langs[2]=zh-TW
targets = {"en": 0, "zh-CN": 1, "zh-TW": 2}
for code, idx in targets.items():
    lg = data["langs"][idx]
    assert lg["code"] == code, (code, lg["code"])
    for key, vals in TRANS.items():
        lg["strings"].setdefault(key, vals[idx])

with open(JSON_PATH, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")

print("new keys added:", added)
print("total keys now:", len(data["keys"]))
