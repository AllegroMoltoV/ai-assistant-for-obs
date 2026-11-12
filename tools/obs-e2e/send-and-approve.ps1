# ポータブル版の OBS のドックに質問を送り、提案が出たら承認して、終わるまでの文を記録する。
# 引数: プロセス ID、送る文、新しい会話にするか、待つ秒数の上限
param([int]$ProcessId, [string]$Message, [switch]$NewConversation, [int]$TimeoutSeconds = 200)
. "$PSScriptRoot\uia-lib.ps1"

$dock = Find-Element $ProcessId "AIアシスタント" "OBSDock"
if (-not $dock) { "FAILED: dock not found"; exit 1 }
$button = [System.Windows.Automation.ControlType]::Button
# 読み取りの途中でドックが作り直されると例外になるため、$false を返して呼び出し元で読み直させる
function Find-In($name, $type) {
	$c1 = New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, $name)
	$c2 = New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, $type)
	try { return $dock.FindFirst($Scope::Subtree, (New-Object System.Windows.Automation.AndCondition($c1, $c2))) }
	catch { return $false }
}
if ($NewConversation) { $null = Invoke-Element (Find-In "新しい会話" $button); Start-Sleep -Milliseconds 800 }
$baseline = @(Get-Texts $dock)
$editCond = New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)
$null = Set-ElementValue ($dock.FindFirst($Scope::Subtree, $editCond)) $Message
$sent = Get-Date
$null = Invoke-Element (Find-In "送信" $button)
$approvals = 0
while (((Get-Date) - $sent).TotalSeconds -lt $TimeoutSeconds) {
	Start-Sleep -Milliseconds 700
	$approve = Find-In "承認して適用" $button
	if ($approve -eq $false) { continue }
	if ($approve) {
		$texts = @(Get-Texts $dock) | Where-Object { $baseline -notcontains $_ }
		"--- proposal at {0:N1}s" -f ((Get-Date) - $sent).TotalSeconds
		$texts | ForEach-Object { "  " + $_ }
		$null = Invoke-Element $approve
		$approvals++
		Start-Sleep -Milliseconds 700
		continue
	}
	$cancel = Find-In "中止" $button
	if ($cancel -eq $false) { continue }
	if ($null -eq $cancel -and ((Get-Date) - $sent).TotalSeconds -gt 3) { break }
}
"approvals=$approvals elapsed={0:N1}s" -f ((Get-Date) - $sent).TotalSeconds
"--- final new texts"
@(Get-Texts $dock) | Where-Object { $baseline -notcontains $_ } | ForEach-Object { "  " + $_ }
