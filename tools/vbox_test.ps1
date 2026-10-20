# JackBOT OS headless test harness - VirtualBox ONLY (no qemu).
# Usage:
#   powershell -ExecutionPolicy Bypass -File vbox_test.ps1 \
#     -Iso <path.iso> [-Seconds 25] [-VmName JackBOT-test] [-Keep]
param(
    [Parameter(Mandatory=$true)][string]$Iso,
    [int]$Seconds = 25,
    [string]$VmName = 'JackBOT-test',
    [string]$HdMb = '2048',
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
$VBM = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$proj = 'E:\JackBOT-OS\new-chat\JackBOT-OS'
$art = Join-Path $proj 'build_assets\vbox'
New-Item -ItemType Directory -Force -Path $art | Out-Null
$serial = Join-Path $art 'serial.log'
$shot   = Join-Path $art 'shot.png'
$disk   = Join-Path $art ("disk_" + $VmName + ".vdi")

if (-not (Test-Path $Iso)) { throw "ISO not found: $Iso" }
foreach ($f in @($serial, $shot, $disk)) { if (Test-Path $f) { Remove-Item $f -Force } }

try { & $VBM unregistervm $VmName --delete 2>&1 | Out-Null } catch {}

& $VBM createvm --name $VmName --ostype Other_64 --register | Out-Null
& $VBM modifyvm $VmName --memory 2048 --vram 128 --firmware bios `
    --boot1 dvd --boot2 disk --boot3 none --boot4 none `
    --graphicscontroller vmsvga --accelerate3d on `
    --audio none --usb-xhci off --uart1 0x3F8 4 --uartmode1 file $serial
& $VBM createhd --filename $disk --size $HdMb --format VDI | Out-Null
& $VBM storagectl $VmName --name IDE --add ide --controller PIIX4
& $VBM storageattach $VmName --storagectl IDE --port 0 --device 0 `
    --type hdd --medium $disk
& $VBM storageattach $VmName --storagectl IDE --port 1 --device 0 `
    --type dvddrive --medium $Iso

Write-Host "starting $VmName headless for ${Seconds}s ..."
& $VBM startvm $VmName --type headless
Start-Sleep -Seconds $Seconds
& $VBM controlvm $VmName screenshotpng $shot
Start-Sleep -Seconds 1
$log = ''
if (Test-Path $serial) { $log = Get-Content $serial -Raw }
$log | Out-File -Encoding utf8 (Join-Path $art 'serial_dump.txt')

if (-not $Keep) {
    try { & $VBM controlvm $VmName poweroff 2>&1 | Out-Null } catch {}
    Start-Sleep -Seconds 2
    try { & $VBM unregistervm $VmName --delete 2>&1 | Out-Null } catch {}
}

Write-Host "---- serial (last 60 lines) ----"
if ($log) { $log -split "`n" | Select-Object -Last 60 }
Write-Host "screenshot: $shot"
Write-Host ("serial lines: " + ($log -split "`n").Count)
