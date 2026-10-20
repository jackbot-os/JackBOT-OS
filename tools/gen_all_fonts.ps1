Set-Location 'E:\JackBOT-OS\new-chat\JackBOT-OS'
$fb = 'assets/fonts/CJK.ttf','assets/fonts/KR.ttf','assets/fonts/AR.ttf','assets/fonts/TH.ttf','assets/fonts/HI.ttf'
$jobs = @(
  @('assets/fonts/SANS.ttf','build/font_sans.h','UI Sans','font_sans'),
  @('assets/fonts/MONO.ttf','build/font_mono.h','Mono','font_mono'),
  @('assets/fonts/MONOB.ttf','build/font_mono_bold.h','Mono Bold','font_mono_bold')
)
foreach ($j in $jobs) {
  $argList = @('tools/font_to_header.py',$j[0],$j[1],$j[2],$j[3],'build/extra_charset.txt') + $fb
  & python @argList
  Write-Host ("done {0} exit={1}" -f $j[1], $LASTEXITCODE)
}
Get-Item build/font_sans.h,build/font_mono.h,build/font_mono_bold.h | Select-Object Name,Length | Format-Table
