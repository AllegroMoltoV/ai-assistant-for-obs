# ポータブル版の OBS のドックに質問を送り、回答の文が届く様子を 0.5 秒ごとに記録する。
# 引数: プロセス ID、送る文、待つ秒数の上限。出力: 経過時間ごとの状態の文と、最も長い文の長さ
param([int]$ProcessId, [string]$Message, [int]$TimeoutSeconds = 200)
. "$PSScriptRoot\uia-lib.ps1"

$dock = Find-Element $ProcessId "AIアシスタント" "OBSDock"
if (-not $dock) { "FAILED: dock not found"; exit 1 }
# 読み取りの途中でドックが作り直されると例外になるため、$false を返して呼び出し元で読み直させる
function Find-In($name, $type) {
	$c1 = New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name)
	$c2 = New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, $type)
	try { return $dock.FindFirst($Scope::Subtree, (New-Object System.Windows.Automation.AndCondition($c1, $c2))) }
	catch { return $false }
}
$button = [System.Windows.Automation.ControlType]::Button
$null = Invoke-Element (Find-In "新しい会話" $button)
Start-Sleep -Milliseconds 800
$baseline = @(Get-Texts $dock)
$editCond = New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)
$edit = $dock.FindFirst($Scope::Subtree, $editCond)
if (-not (Set-ElementValue $edit $Message)) { "FAILED: could not type"; exit 1 }
$sent = Get-Date
if (-not (Invoke-Element (Find-In "送信" $button))) { "FAILED: could not press send"; exit 1 }

$last = ""
$growth = 0
$previous = -1
$finalTexts = @()
while (((Get-Date) - $sent).TotalSeconds -lt $TimeoutSeconds) {
	Start-Sleep -Milliseconds 500
	$read = Get-Texts $dock
	$cancel = Find-In "中止" $button
	if ($null -eq $read -or $cancel -eq $false) { continue }
	$texts = @($read)
	$new = @($texts | Where-Object { $baseline -notcontains $_ -and $_ -ne $Message })
	$status = ($texts | Where-Object { $_ -like "考えています*" -or $_ -like "提案を*" }) -join ","
	$longest = ($new | Measure-Object -Property Length -Maximum).Maximum
	if ($null -eq $longest) { $longest = 0 }
	$busy = $null -ne $cancel -and -not $cancel.Current.IsOffscreen
	$line = "{0,6:N1}s busy={1} status='{2}' longest={3}" -f ((Get-Date) - $sent).TotalSeconds, $busy, $status, $longest
	if ($line -replace '^\s*[\d.]+s ', '' -ne $last) { $line; $last = $line -replace '^\s*[\d.]+s ', '' }
	if ($longest -gt $previous -and $previous -gt 0) { $growth++ }
	$previous = $longest
	$finalTexts = $new
	if (-not $busy -and ((Get-Date) - $sent).TotalSeconds -gt 3) { break }
}
"growth_steps=$growth"
"--- new texts"
$finalTexts | ForEach-Object { $_ }
