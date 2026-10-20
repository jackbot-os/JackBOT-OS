$out = 'E:\JackBOT-OS\new-chat\JackBOT-OS\build_vbox.txt'
$paths = @('C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
           'C:\Program Files\VirtualBox\VBoxManage.exe')
$lines = @()
foreach ($p in $paths) {
    if (Test-Path $p) {
        $lines += "FOUND: $p"
        $lines += ("version: " + (& $p --version))
    } else {
        $lines += "missing: $p"
    }
}
$lines | Out-File -Encoding utf8 $out
