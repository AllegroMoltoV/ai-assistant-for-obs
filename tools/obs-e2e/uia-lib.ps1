# OBS の画面を UI Automation で操作する関数。ドットソースで読み込んで使う
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$script:UIA = [System.Windows.Automation.AutomationElement]
$script:Scope = [System.Windows.Automation.TreeScope]

function Get-ObsWindows([int]$ProcessId) {
	$cond = New-Object System.Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, $ProcessId)
	return $UIA::RootElement.FindAll($Scope::Children, $cond)
}

# プロセスの窓の子孫から、名前とクラス (省略可) が一致する最初の部品を返す
function Find-Element([int]$ProcessId, [string]$Name, [string]$ClassName = "", [string]$ControlType = "") {
	foreach ($w in (Get-ObsWindows $ProcessId)) {
		$all = $w.FindAll($Scope::Subtree, [System.Windows.Automation.Condition]::TrueCondition)
		foreach ($el in $all) {
			$c = $el.Current
			if ($Name -ne "" -and $c.Name -ne $Name) { continue }
			if ($ClassName -ne "" -and $c.ClassName -ne $ClassName) { continue }
			if ($ControlType -ne "" -and $c.ControlType.ProgrammaticName -ne "ControlType.$ControlType") { continue }
			return $el
		}
	}
	return $null
}

function Invoke-Element($el) {
	$pattern = $null
	if ($el.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) { $pattern.Invoke(); return $true }
	if ($el.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) { $pattern.Toggle(); return $true }
	if ($el.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)) { $pattern.Expand(); return $true }
	return $false
}

function Set-ElementValue($el, [string]$Value) {
	$pattern = $null
	if ($el.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) { $pattern.SetValue($Value); return $true }
	return $false
}

# 部品の子孫のうち、名前のある Text の部品の名前を並べて返す。
# 読み取りの途中でドックが作り直されると例外になるため、そのときは $null を返し、呼び出し元で読み直させる
function Get-Texts($el) {
	$cond = New-Object System.Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
	try {
		$out = @()
		foreach ($item in $el.FindAll($Scope::Subtree, $cond)) { if ($item.Current.Name) { $out += $item.Current.Name } }
		return ,$out
	} catch { return $null }
}
