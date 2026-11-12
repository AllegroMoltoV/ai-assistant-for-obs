# 検証用のポータブル版の OBS を起動し、AI アシスタントのドックを表示する。プロセス ID を出力する。
# 初回の自動構成ウィザードが出たら閉じる。ドックが隠れていれば「ドック」のメニューから表示する
param([Parameter(Mandatory = $true)][string]$Root, [int]$WaitSeconds = 20)
. "$PSScriptRoot\uia-lib.ps1"
$p = Start-Process -FilePath "$Root\bin\64bit\obs64.exe" -ArgumentList "--portable", "--disable-shutdown-check", "--disable-updater", "--multi" `
	-WorkingDirectory "$Root\bin\64bit" -PassThru
Start-Sleep -Seconds $WaitSeconds
$cancel = Find-Element $p.Id "キャンセル" "QPushButton"
if ($cancel) { $null = Invoke-Element $cancel; Start-Sleep -Seconds 2 }
if (-not (Find-Element $p.Id "AIアシスタント" "OBSDock")) {
	$menu = Find-Element $p.Id "ドック(D)" "QAction"
	$expand = $null
	$null = $menu.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$expand)
	$expand.Expand()
	Start-Sleep -Seconds 1
	# メニューの項目は、メニューバーの項目の子として表れる
	$popup = $menu.FindFirst($Scope::Children, [System.Windows.Automation.Condition]::TrueCondition)
	$cond = New-Object System.Windows.Automation.PropertyCondition($UIA::NameProperty, "AIアシスタント")
	$item = $popup.FindFirst($Scope::Children, $cond)
	$invoke = $null
	$null = $item.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)
	$invoke.Invoke()
	Start-Sleep -Seconds 2
}
if (-not (Find-Element $p.Id "AIアシスタント" "OBSDock")) { "FAILED: dock not shown"; exit 1 }
$p.Id
