# 検証用の OBS を、窓を閉じる操作で終了する。設定とドックの配置が保存される。30 秒で終わらなければツリーごと止める
param([Parameter(Mandatory = $true)][int]$ProcessId)
. "$PSScriptRoot\uia-lib.ps1"
$w = (Get-ObsWindows $ProcessId) | Where-Object { $_.Current.ClassName -eq "OBSBasic" } | Select-Object -First 1
$pattern = $null
if ($w -and $w.TryGetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern, [ref]$pattern)) { $pattern.Close() }
for ($i = 0; $i -lt 30; $i++) {
	if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { "exited"; exit 0 }
	Start-Sleep -Seconds 1
}
taskkill /PID $ProcessId /T /F | Out-Null
"killed"
