#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""JackBOT OS 1.0.4 i18n batch B: remaining built-in window strings.

Adds real English / Simplified Chinese / Traditional Chinese translations
for the rest of the built-in app windows. Idempotent: existing keys and
translations are left untouched.

Usage: python3 add_i18n_104b.py <path-to-i18n_data.json>
"""
import json
import sys

TRANS = {
    # ---- 3D Studio ----
    "3d_pipeline": (
        "JackBOT 3D pipeline  Q16.16 fixed-point  z-buffer",
        "JackBOT 3D 管线  Q16.16 定点数  深度缓冲",
        "JackBOT 3D 管線  Q16.16 定點數  深度緩衝"),

    # ---- Audio ----
    "au_card": (
        "Sound card: Intel AC97 (48000 Hz, stereo 16-bit).",
        "声卡：Intel AC97（48000 Hz，立体声 16 位）。",
        "音效卡：Intel AC97（48000 Hz，立體聲 16 位元）。"),
    "au_nocard": (
        "No sound card: AC97/HD Audio driver not built yet.",
        "无声卡：AC97/HD Audio 驱动尚未构建。",
        "無音效卡：AC97/HD Audio 驅動尚未建構。"),
    "au_volume": ("Volume", "音量", "音量"),
    "au_outputs": ("Output devices:", "输出设备：", "輸出裝置："),
    "au_codec": (
        "AC97 codec (SigmaTel STAC9700)",
        "AC97 编解码器（SigmaTel STAC9700）",
        "AC97 編解碼器（SigmaTel STAC9700）"),
    "au_none": ("(none detected)", "（未检测到）", "（未偵測到）"),
    "au_play_boot": ("Play boot", "播放开机音", "播放開機音"),
    "au_boot_playing": ("Playing boot sound...", "正在播放开机音...", "正在播放開機音..."),
    "au_boot_done": ("Boot sound finished.", "开机音播放完毕。", "開機音播放完畢。"),
    "au_play_warn": ("Play warn", "播放警告音", "播放警告音"),
    "au_warn_playing": ("Playing warning sound...", "正在播放警告音...", "正在播放警告音..."),
    "au_warn_done": ("Warning sound finished.", "警告音播放完毕。", "警告音播放完畢。"),
    "au_test": ("Test sound", "测试声音", "測試聲音"),
    "au_test_nodev": ("No audio device to play a test tone.", "无音频设备可播放测试音。", "無音訊裝置可播放測試音。"),

    # ---- Backup & Restore ----
    "bk_now": ("Backup now", "立即备份", "立即備份"),
    "bk_created": ("Created ", "已创建 ", "已建立 "),
    "bk_fail": ("Backup failed (config missing or disk full).", "备份失败（配置缺失或磁盘已满）。", "備份失敗（設定遺失或磁碟已滿）。"),
    "bk_saved": ("Saved backups:", "已保存的备份：", "已儲存的備份："),
    "bk_none": ("(none yet)", "（暂无）", "（尚無）"),
    "bk_restore": ("Restore", "还原", "還原"),
    "bk_restore_fail": ("Restore failed.", "还原失败。", "還原失敗。"),
    "bk_restored": ("Restored ", "已还原 ", "已還原 "),

    # ---- Bluetooth & Lamps ----
    "bt_radio_on": ("Bluetooth radio: ON", "蓝牙无线电：开", "藍牙無線電：開"),
    "bt_radio_off": ("Bluetooth radio: OFF", "蓝牙无线电：关", "藍牙無線電：關"),
    "bt_adapter": ("Adapter: ", "适配器：", "介面卡："),
    "bt_no_adapter": ("No PCI Bluetooth adapter found.", "未找到 PCI 蓝牙适配器。", "找不到 PCI 藍牙介面卡。"),
    "bt_usb_dongle": ("USB BT dongles need the USB device stage (next).", "USB 蓝牙适配器需要 USB 设备阶段（下一步）。", "USB 藍牙介面卡需要 USB 裝置階段（下一步）。"),
    "bt_power_off": ("Power off", "关机", "關閉電源"),
    "bt_power_on": ("Power on", "开机", "開啟電源"),
    "bt_power_down_msg": ("Radio powered down.", "无线电已关闭。", "無線電已關閉。"),
    "bt_power_up_msg": ("Radio powered up.", "无线电已开启。", "無線電已開啟。"),
    "bt_scan": ("Scan devices", "扫描设备", "掃描裝置"),
    "bt_scan_started": ("Inquiry started (HCI transport pending).", "查询已开始（HCI 传输待处理）。", "查詢已開始（HCI 傳輸待處理）。"),
    "bt_scan_turnon": ("Turn the radio on before scanning.", "扫描前请先开启无线电。", "掃描前請先開啟無線電。"),
    "bt_lamp": ("Smart LED lamp", "智能 LED 灯", "智慧 LED 燈"),
    "bt_lamp_off": ("Lamp off.", "灯已关闭。", "燈已關閉。"),
    "bt_lamp_set": ("Lamp color set.", "灯颜色已设置。", "燈光顏色已設定。"),
    "bt_brightness": ("Brightness", "亮度", "亮度"),
    "bt_steady": ("Steady", "常亮", "恆亮"),
    "bt_blink": ("Blink", "闪烁", "閃爍"),
    "bt_breathe": ("Breathe", "呼吸", "呼吸"),
    "bt_usb_note": ("USB ports with devices are listed in USB Devices.", "有设备的 USB 端口列在 USB 设备中。", "有裝置的 USB 連接埠列在 USB 裝置中。"),

    # ---- Browser ----
    "br_reload": ("Reload", "重新加载", "重新載入"),
    "br_home": ("Home", "主页", "首頁"),
    "br_loading": ("Loading...", "正在加载...", "正在載入..."),
    "br_status_ok": ("OK", "正常", "正常"),
    "br_status_notfound": ("Not Found", "未找到", "找不到"),
    "br_status_forbidden": ("Forbidden", "禁止访问", "禁止存取"),
    "br_status_error": ("Server Error", "服务器错误", "伺服器錯誤"),
    "br_status_redirect": ("Redirected", "已重定向", "已重新導向"),
    "br_fail_status": ("Status: connection failed", "状态：连接失败", "狀態：連線失敗"),
    "br_unreachable": ("This site can't be reached", "无法访问此网站", "無法連線此網站"),
    "br_https_note": ("HTTPS pages are not supported yet (TLS stack planned).", "暂不支持 HTTPS 页面（TLS 栈规划中）。", "暫不支援 HTTPS 頁面（TLS 堆疊規劃中）。"),
    "br_try_http": ("Try an http:// address such as http://10.0.2.2/", "试试 http:// 地址，例如 http://10.0.2.2/", "試試 http:// 位址，例如 http://10.0.2.2/"),
    "br_check_net": ("Check network (DHCP), the URL and that the host is up.", "检查网络（DHCP）、网址以及主机是否在线。", "檢查網路（DHCP）、網址以及主機是否在線。"),
    "br_plain_http": ("JackBOT Browser supports plain HTTP sites.", "JackBOT 浏览器支持纯 HTTP 网站。", "JackBOT 瀏覽器支援純 HTTP 網站。"),

    # ---- Screenshot / Capture ----
    "cap_saved": ("Saved ", "已保存 ", "已儲存 "),
    "cap_take": ("Take screenshot", "截取屏幕", "擷取螢幕"),
    "cap_fail": ("Screenshot failed (disk missing or full).", "截图失败（磁盘缺失或已满）。", "截圖失敗（磁碟遺失或已滿）。"),
    "cap_record": ("Record screen", "录制屏幕", "錄製螢幕"),
    "cap_no_encoder": ("Recording needs a video encoder (not built yet).", "录制需要视频编码器（尚未构建）。", "錄製需要視訊編碼器（尚未建構）。"),

    # ---- Firewall ----
    "fw_on": ("Firewall: ON", "防火墙：开", "防火牆：開"),
    "fw_off": ("Firewall: OFF", "防火墙：关", "防火牆：關"),
    "fw_filter_note": ("Filtering needs the network stack; state saved.", "过滤需要网络栈；状态已保存。", "過濾需要網路堆疊；狀態已儲存。"),
    "hp_on": ("Honeypot: ON", "蜜罐：开", "蜜罐：開"),
    "hp_off": ("Honeypot: OFF", "蜜罐：关", "蜜罐：關"),
    "fw_passive": ("Passive observer only: it never answers or scans.", "仅被动观察：从不应答或扫描。", "僅被動觀察：從不應答或掃描。"),
    "fw_probes": ("Honeypot probes:", "蜜罐探测：", "蜜罐探測："),
    "fw_policy": ("Default policy:", "默认策略：", "預設原則："),

    # ---- Font Manager ----
    "fm_sample": ("Welcome 0123", "欢迎 0123", "歡迎 0123"),
    "fm_selected": ("Selected font ", "已选字体 ", "已選字型 "),

    # ---- Games ----
    "gm_score": ("Score: ", "得分：", "分數："),
    "gm_snake_hint": ("Arrows/WASD to move, Enter to restart", "方向键/WASD 移动，回车重开", "方向鍵/WASD 移動，Enter 重開"),
    "gm_over": ("Game Over", "游戏结束", "遊戲結束"),
    "gm_press_enter": ("Press Enter to restart", "按回车重开", "按 Enter 重開"),
    "gm_2048_hint": ("Arrows/WASD to slide, Enter to restart", "方向键/WASD 滑动，回车重开", "方向鍵/WASD 滑動，Enter 重開"),
    "gm_no_moves": ("No moves left", "无可移动", "無可移動"),

    # ---- JCA Studio ----
    "jca_compile_err": ("Compile error: ", "编译错误：", "編譯錯誤："),
    "jca_source_hint": ("JCA source (.jca) - fn/var/if/while/return, Tab indent:", "JCA 源码（.jca）- fn/var/if/while/return，Tab 缩进：", "JCA 原始碼（.jca）- fn/var/if/while/return，Tab 縮排："),
    "jca_compile": ("Compile", "编译", "編譯"),
    "jca_run": ("Run", "运行", "執行"),
    "jca_example": ("Example", "示例", "範例"),
    "jca_clear": ("Clear", "清空", "清除"),
    "jca_bytecode": ("Bytecode (compiled):", "字节码（已编译）：", "位元組碼（已編譯）："),
    "jca_output": ("Output:", "输出：", "輸出："),
    "jca_press": ("Press Compile, then Run", "先按编译，再按运行", "先按編譯，再按執行"),

    # ---- Network ----
    "nw_no_nic": ("No network interface: NIC driver + TCP/IP stack not built yet.", "无网络接口：网卡驱动 + TCP/IP 栈尚未构建。", "無網路介面：網卡驅動 + TCP/IP 堆疊尚未建構。"),
    "nw_wired": ("Wired  (not available)", "有线（不可用）", "有線（不可用）"),
    "nw_wifi": ("Wi-Fi  (not available)", "Wi-Fi（不可用）", "Wi-Fi（不可用）"),
    "nw_ip": ("IP: -     Gateway: -", "IP：-     网关：-", "IP：-     閘道：-"),
    "nw_refresh": ("Refresh", "刷新", "重新整理"),
    "nw_no_scan": ("No NIC driver to scan.", "无网卡驱动可扫描。", "無網卡驅動可掃描。"),

    # ---- Power ----
    "pw_shutdown": ("Shut down", "关机", "關機"),
    "pw_restart": ("Restart", "重启", "重新啟動"),
    "pw_updates": ("Check for updates", "检查更新", "檢查更新"),
    "pw_update_nonic": ("Updates need the network stack (no NIC driver yet).", "更新需要网络栈（暂无网卡驱动）。", "更新需要網路堆疊（暫無網卡驅動）。"),

    # ---- Python Console ----
    "py_intro": ("Type Python code, then Run.", "输入 Python 代码，然后运行。", "輸入 Python 程式碼，然後執行。"),
    "py_code_hint": ("Code (Enter wraps, Tab indent):", "代码（回车换行，Tab 缩进）：", "程式碼（Enter 換行，Tab 縮排）："),
    "py_output": ("Output:", "输出：", "輸出："),
    "py_run": ("Run", "运行", "執行"),
    "py_clear": ("Clear", "清空", "清除"),

    # ---- App Store ----
    "st_offline": ("Offline: downloads need the network stack.", "离线：下载需要网络栈。", "離線：下載需要網路堆疊。"),
    "st_installed": ("Installed (built-in):", "已安装（内置）：", "已安裝（內建）："),
    "st_installed_tag": ("installed", "已安装", "已安裝"),
    "st_browse": ("Browse / refresh catalog", "浏览/刷新目录", "瀏覽/重新整理目錄"),
    "st_no_catalog": ("Cannot reach catalog (network stack unavailable).", "无法连接目录（网络栈不可用）。", "無法連線目錄（網路堆疊不可用）。"),

    # ---- System Update ----
    "up_installed_ver": ("Installed version: ", "已安装版本：", "已安裝版本："),
    "up_last_fail": ("Last check failed. Check network and retry.", "上次检查失败。请检查网络后重试。", "上次檢查失敗。請檢查網路後重試。"),
    "up_click_hint": ("Click Check to contact the update server.", "点击检查以联系更新服务器。", "按一下檢查以聯絡更新伺服器。"),
    "up_checking": ("Checking for updates...", "正在检查更新...", "正在檢查更新..."),
    "up_uptodate": ("Your system is up to date.", "系统已是最新。", "系統已是最新。"),
    "up_available": ("Update available:", "有可用更新：", "有可用更新："),
    "up_version_label": ("  version ", "  版本 ", "  版本 "),
    "up_notes_label": ("  notes:", "  说明：", "  備註："),
    "up_downloading": ("Downloading update package...", "正在下载更新包...", "正在下載更新套件..."),
    "up_checksum_fail": ("Checksum mismatch - update aborted, system unchanged.", "校验和不符 - 更新中止，系统未改变。", "校驗碼不符 - 更新中止，系統未改變。"),
    "up_verified": ("Package verified. Staged on B slot; reboot to finish.", "包已验证。暂存于 B 槽；重启以完成。", "套件已驗證。暫存於 B 槽；重新啟動以完成。"),
    "up_download_fail": ("Download failed - update aborted, system unchanged.", "下载失败 - 更新中止，系统未改变。", "下載失敗 - 更新中止，系統未改變。"),
    "up_rollback_note": ("If the new build fails, it auto-rolls back to A slot.", "若新版本失败，会自动回滚到 A 槽。", "若新版本失敗，會自動回復到 A 槽。"),
    "up_check": ("Check for updates", "检查更新", "檢查更新"),
    "up_install": ("Download and install", "下载并安装", "下載並安裝"),
    "up_no_net": ("Network not ready yet (waiting for DHCP).", "网络尚未就绪（等待 DHCP）。", "網路尚未就緒（等待 DHCP）。"),

    # ---- Voice ----
    "vo_mic_ready": ("Mic: AC97 capture channel ready", "麦克风：AC97 采集通道就绪", "麥克風：AC97 擷取通道就緒"),
    "vo_mic_none": ("Mic: no AC97 device in this VM", "麦克风：此虚拟机无 AC97 设备", "麥克風：此虛擬機無 AC97 裝置"),
    "vo_listening": ("Listening... (2 s)", "正在聆听...（2 秒）", "正在聆聽...（2 秒）"),
    "vo_tap": ("Tap to talk", "点击说话", "按一下說話"),
    "vo_level": ("Input level", "输入电平", "輸入電平"),
    "vo_detected": ("Voice detected. Speech-to-text is the next stage;", "检测到语音。语音转文字是下一阶段；", "偵測到語音。語音轉文字是下一階段；"),
    "vo_type_now": ("type the command below for now.", "暂时请在下方输入命令。", "暫時請在下方輸入命令。"),
    "vo_no_speech": ("No speech picked up. Check mic / VM audio input.", "未拾取到语音。检查麦克风/虚拟机音频输入。", "未擷取到語音。檢查麥克風/虛擬機音訊輸入。"),
    "vo_command_hint": ("Command (then Enter):", "命令（然后回车）：", "命令（然後 Enter）："),
    "vo_jbot": ("Jbot:", "Jbot：", "Jbot："),
    "vo_try": ("Try: time / calc 8*9 / hello / help", "试试：time / calc 8*9 / hello / help", "試試：time / calc 8*9 / hello / help"),
}


def main():
    if len(sys.argv) < 2:
        print("usage: python3 add_i18n_104b.py <i18n_data.json>")
        sys.exit(1)

    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    keys = data["keys"]
    langs = data["langs"]
    en = langs[0]
    zhcn = langs[1]
    zhtw = langs[2]

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
